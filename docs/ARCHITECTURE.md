# Architecture

## Local storage and file-manifest path (implemented)

The current repository also includes a storage-node process, TCP RPC, deterministic chunk replication, and replica audit/repair; see the later Project Day sections and the [architecture walkthrough](ARCHITECTURE-WALKTHROUGH.md).

The local storage layer isolates chunk persistence behind ChunkStore. Each chunk is limited to 4 MiB, written to a temporary file, and renamed into place. The record contains a format marker, a SHA-256 digest, and the payload. Chunk IDs are restricted to safe filename characters.

FileTransfer builds on that primitive. Upload reads a source file incrementally into one reusable 4 MiB buffer, stores each non-empty block under a deterministic file-and-sequence chunk ID, and returns an ordered in-memory FileManifest. The manifest records total bytes, per-chunk sizes and checksums, and a SHA-256 checksum for the complete file. Download validates manifest structure before reading chunks, verifies each chunk against both the chunk-store record and manifest, writes to a temporary destination, verifies the complete-file digest, and only then renames the temporary output into place.

ManifestStore can persist a FileManifest in a version-1 binary format. The format has a fixed magic header, explicit little-endian integer fields, length-prefixed identifiers, the file and chunk SHA-256 values, and a final SHA-256 over all preceding manifest bytes. The loader checks the file-size bound before allocation, rejects unsupported versions and invalid lengths/counts, verifies the metadata checksum, and validates the reconstructed manifest before returning it. The caller must explicitly save the manifest; FileTransfer::upload does not implicitly persist metadata. A process restart can recover a file mapping only when the caller saved the manifest and the referenced chunks remain available.

Chunk writes still use temporary sibling files followed by rename and do not claim power-loss durability. ManifestStore::save is hardened on POSIX: it creates a unique temporary sibling with exclusive creation, writes all bytes, fsyncs the temporary file, closes it, renames it over the destination, then fsyncs the containing directory. A successful save therefore requests durable publication in the filesystem ordering supported by these calls; actual hardware/filesystem guarantees vary. If the directory fsync fails after rename, the new manifest may already be visible but durability is uncertain. Stale temporary-file recovery is available as an explicit, target-scoped POSIX sweep with a 24-hour default age threshold and owner-PID liveness check; it ignores symlinks and malformed names. Cross-platform replacement policy, PID-reuse/race hardening, and destination locking still need work. ChunkStore has not yet adopted this durability sequence.

## Upload, persistence and download data flow

~~~text
Upload:
source file -> reusable 4 MiB buffer -> per-chunk SHA-256 + whole-file SHA-256
            -> ChunkStore::put -> ordered FileManifest (memory)
            -> ManifestStore::save -> versioned/checksummed manifest file

Recovery/download:
ManifestStore::load -> bounded parse + metadata checksum + structural validation
                   -> FileManifest -> ChunkStore::get -> per-chunk checks
                   -> temporary output + whole-file SHA-256 -> rename to destination
~~~

## Target architecture

~~~text
Client
  |
  v
API / Coordinator  ---- Metadata Store
  |                       (PostgreSQL)
  |
  +---- Storage Node A
  |       +-- chunk files
  |
  +---- Storage Node B
  |       +-- replicated chunks
  |
  +---- Storage Node C
          +-- chunk files

Coordinator / Repair Worker
  +-- placement decisions
  +-- replica health checks
  +-- repair and rebalancing
~~~

## Planned metadata model

- File: logical file ID, owner, size, chunking parameters, version.
- Chunk: content ID, size, SHA-256, placement and replica state.
- Node: node ID, endpoint, capacity, available bytes, health state.
- Replica: chunk ID, node ID, state, last verified time.

## Design decisions

- Chunk payloads are kept separate from file-level metadata.
- A fixed maximum chunk size bounds the amount of payload data handled by one storage operation.
- The upload path does not retain the entire source file in memory. Manifest metadata still grows with the number of chunks.
- Chunk checksums detect per-chunk corruption; the whole-file checksum detects incorrect order, missing bytes, or a manifest that references a different complete payload.
- The version-1 manifest uses a small explicit binary format instead of adding a serialization dependency before the storage semantics are settled. Integers are little-endian and strings are length-prefixed.
- Manifest parsing is bounded to 64 MiB and 300,000 chunk records before large allocations are attempted.
- A final manifest checksum detects accidental metadata corruption. It is not a MAC or signature and does not authenticate the manifest's source.
- Download publishes output only after every chunk and the complete file checksum pass.
- Client-facing APIs and storage-node protocols are separate boundaries.
- Replication policy, quorum semantics, and consistency guarantees must be explicit before being advertised.
- Failed operations must leave enough state for reconciliation rather than silently losing metadata or data.

## Day 5 recovery hygiene

ManifestStore can explicitly scan only the sibling temporary-file prefix associated with its target manifest. The sweep requires both an age threshold and a dead owner PID, skips symlinks/non-regular files and malformed names, and is idempotent. It is not invoked automatically by save/load. This is a conservative local policy, not a cross-host lock or proof against every PID-reuse race. ChunkStore temporary-file naming is not covered by this sweep. The deletion itself is best-effort and is not directory-fsync'd.


## Day 6: deterministic fault injection

ManifestStore now accepts an injected operations object for temporary-file creation, write, file/directory fsync, close, rename, parent-directory open and recovery removal. Normal construction uses POSIX wrappers. Tests override one operation at a time and assert the pre-rename preservation invariant or the post-rename visible-but-durability-uncertain outcome. This is a test seam, not a new platform-independent durability guarantee. ChunkStore and transfer operations are not covered.

## Compressed 10-day project scope

Historical implementation days 1–6 map to compressed Project Days 1–4: foundation, file transfer, manifest persistence, then manifest durability/recovery/fault injection. Project Day 5 establishes local same-chunk thread safety. Future milestones use one C++ executable with configurable coordinator/storage-node roles, a gRPC interface, a small metadata catalog, replication/repair, bounded operations and a repeatable Compose demo; Kubernetes and a broad microservice split are intentionally out of scope.

## ChunkStore concurrency contract (Project Day 5)

Within one process, put/get for a chunk ID and normalized root path share a mutex registry, including across separate ChunkStore instances. Same-ID writes are serialized and publish complete records by rename; the policy is last successful publication wins. Temporary names include process ID and a process-local sequence to avoid the earlier sequence-only collision between processes. Across processes, readers still rely on atomic rename and writers remain last-writer-wins; no inter-process lock, multi-chunk upload transaction or file-ID versioning is provided. Concurrent uploads that reuse the same file ID can therefore still overwrite the chunk set referenced by an earlier manifest and must be avoided until versioned publication is implemented.


## Project Day 6 — local service facade, metadata catalog and CLI

A small `MetadataCatalog` stores one checksummed manifest per safe file ID under a metadata directory. The `StorageService` facade composes the existing `ChunkStore`, `FileTransfer` and catalog. The `dfs_cli` executable exposes upload, download, list and inspect commands over a local storage root. This is a local interface only; RPC and the storage-node process are later milestones.

Metadata save occurs after chunk ingestion. If metadata publication fails, stored chunks can be orphaned; no cross-component transaction or orphan reclamation exists. Reusing a file ID replaces its manifest/chunk names, so versioning and multi-writer upload transactions remain out of scope. The catalog uses process-local per-manifest locks, not inter-process locking.


## Project Day 7 — configurable local storage-node process

`StorageNode` wraps the existing `ChunkStore` under a node-specific `chunks/` directory. Initialization creates the directories and writes a versioned identity marker (`node-id`); a root registered to a different or malformed node identity is rejected. The node configuration contains a safe node ID, root and payload-byte capacity. The `dfs_node` executable runs in the foreground with a simple console for `status`, `put`, `get`, `help` and `quit`.

Capacity accounting scans regular `.chunk` records and subtracts the fixed record header (7-byte magic plus 64-byte SHA-256) to count payload bytes. A put is serialized by a process-local mutex keyed by normalized node root, scans existing usage, verifies an existing chunk before replacing it, and checks the projected payload usage before delegating to `ChunkStore::put`. Status reports capacity, used, available and chunk count. Reads continue through ChunkStore's checksum verification.

This is a local node boundary only. Quota locks do not coordinate other processes or hosts; temporary files and filesystem overhead are excluded from quota; status does not checksum every stored chunk; the identity marker is not fsync'd; and chunk publication retains ChunkStore's existing durability limitations. The console is intentionally not an RPC server; network transport and placement/replication follow in Project Day 8.


## Project Day 8 — TCP RPC, deterministic placement and replication (merged; CI verified)

The new node RPC adapter uses a small length-prefixed binary TCP protocol over IPv4. Each request has an operation byte, a bounded chunk ID and a bounded payload length. The server exposes PUT, GET and STATUS, and delegates integrity/quota checks to the existing StorageNode API. Frames and payloads are capped at approximately 4 MiB plus protocol overhead; client sockets use a two-second send/receive timeout. The server is a single-request-at-a-time loop per node process.

The DistributedChunkService chooses distinct configured nodes using an FNV-1a hash of the chunk ID and a cyclic offset over the configured node order. It writes to each selected node and reports success only if the requested replica count was reached; a failed write can leave a partial replica set for the next repair milestone. Reads try the selected replica endpoints in order and return the first verified chunk. Placement is stateless, so every client must use the same node membership/order and replication factor. This is a prototype placement policy, not consistent hashing or persisted placement metadata.

The protocol is deliberately a small learning transport rather than gRPC/Protobuf: the API is limited to three operations and a single bounded chunk, so generated code and a dependency were deferred. It is POSIX-only, IPv4, plaintext, unauthenticated, and not designed for internet exposure. It has no concurrent connection workers, inter-process quota coordination, request authentication, encryption, protocol version negotiation or file-level manifest integration. These limitations are explicit; this does not claim production RPC security or availability.


## Project Day 9 — replica audit and repair

ReplicaRepairService uses the same deterministic placement calculation as DistributedChunkService. Audit reads each selected replica through the checksum-verifying RPC GET. It selects the first readable copy as a baseline and classifies subsequent copies as matching or divergent; failures are classified as missing, internally corrupt or unavailable from their error messages. This baseline is a convenience for the prototype, not a trusted metadata checksum or consensus source.

Repair sends a bounded repair-PUT to each unverified target. The StorageNode repair path serializes quota accounting, estimates the existing payload size for replacement, and publishes through ChunkStore's temp-file/rename path. Unlike normal PUT, repair bypasses the existing-record checksum refusal so that a corrupt record can be replaced. Each successful repair is read back and compared with the baseline before the report marks it verified. A node that remains offline, or a cluster with no readable source, remains unresolved.

The repair operation can partially complete and is not transactional across nodes. The RPC is plaintext and unauthenticated; repair-PUT is a privileged operation and must be confined to a trusted network. There is no persistent placement/repair queue, membership epoch, quorum or authoritative file/chunk metadata checksum yet. If the first readable replica is itself incorrect, audit may select and propagate that wrong payload.
