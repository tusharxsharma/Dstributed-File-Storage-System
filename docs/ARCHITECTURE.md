# Architecture

## Current milestone: single-node chunk store

The first storage primitive isolates chunk persistence behind `ChunkStore`. Each chunk is limited to 4 MiB, written to a temporary file, and renamed into place. The record contains a format marker, a SHA-256 digest, and the payload. Chunk IDs are restricted to safe filename characters.

This provides atomic visibility on filesystems where rename replaces the destination atomically. It is **not yet a power-loss durability guarantee**: file and directory synchronization will be added with a platform-specific durability contract. Concurrent writers to the same chunk ID and cross-platform replacement semantics also need explicit handling before the store is exposed as a network service.

## Target architecture

```text
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
          +-- replicated chunks

Coordinator / Repair Worker
  +-- placement decisions
  +-- replica health checks
  +-- repair and rebalancing
```

## Planned metadata model

- File: logical file ID, owner, size, chunking parameters, version.
- Chunk: content ID, size, SHA-256, placement and replica state.
- Node: node ID, endpoint, capacity, available bytes, health state.
- Replica: chunk ID, node ID, state, last verified time.

## Design decisions

- Chunk payloads are kept separate from metadata.
- Client-facing APIs and storage-node protocols are separate boundaries.
- Checksums verify integrity; they do not provide authenticity or authorization.
- Replication policy, quorum semantics, and consistency guarantees must be explicit before being advertised.
- Failed operations must leave enough state for reconciliation rather than silently losing metadata or data.
