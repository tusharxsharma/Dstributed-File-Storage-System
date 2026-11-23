# Architecture

## Current milestone: single-node chunk store, file transfer, manifest persistence and local recovery

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
