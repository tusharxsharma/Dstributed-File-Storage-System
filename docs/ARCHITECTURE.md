# Architecture

## Current milestone: single-node chunk store and file transfer

The local storage layer isolates chunk persistence behind ChunkStore. Each chunk is limited to 4 MiB, written to a temporary file, and renamed into place. The record contains a format marker, a SHA-256 digest, and the payload. Chunk IDs are restricted to safe filename characters.

FileTransfer builds on that primitive. Upload reads a source file incrementally into one reusable 4 MiB buffer, stores each non-empty block under a deterministic file-and-sequence chunk ID, and returns an ordered in-memory FileManifest. The manifest records total bytes, per-chunk sizes and checksums, and a SHA-256 checksum for the complete file. Download validates manifest structure before reading chunks, verifies each chunk against both the chunk-store record and manifest, writes to a temporary destination, verifies the complete-file digest, and only then renames the temporary output into place.

The manifest is currently in memory only. Restarting the process loses the file-to-chunk mapping. If upload fails after one or more chunks have been stored, those chunks can remain orphaned. Concurrent modification of the source while it is being read is not detected.

The temporary-file-and-rename pattern provides atomic visibility on filesystems where rename replacement is atomic. It is not a power-loss durability guarantee: file and directory synchronization are not implemented. Concurrent writers to the same chunk ID and cross-platform replacement semantics also need explicit handling before the store is exposed as a network service.

## Upload and download data flow

~~~text
Upload:
source file -> reusable 4 MiB buffer -> per-chunk SHA-256 + whole-file SHA-256
            -> ChunkStore::put -> ordered FileManifest (memory only)

Download:
FileManifest -> structural validation -> ChunkStore::get -> per-chunk checks
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
- Download publishes output only after every chunk and the complete file checksum pass.
- Client-facing APIs and storage-node protocols are separate boundaries.
- Checksums verify integrity; they do not provide authenticity or authorization.
- Replication policy, quorum semantics, and consistency guarantees must be explicit before being advertised.
- Failed operations must leave enough state for reconciliation rather than silently losing metadata or data.
