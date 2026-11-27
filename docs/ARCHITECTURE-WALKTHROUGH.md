# Architecture walkthrough

This guide follows the implementation in this repository. It has two paths: local file manifests and distributed single-chunk RPC. The distributed CLI does not currently upload a complete file through the manifest/catalog layer.

## System map

```text
Local file path
dfs_cli -> StorageService -> FileTransfer <-> ChunkStore
                         -> MetadataCatalog -> ManifestStore

Distributed chunk path
dfs_cluster_cli / dfs_repair_cli
  -> DistributedChunkService / ReplicaRepairService
  -> deterministic placement over configured endpoint order
  -> StorageNodeRpcClient -- TCP --> StorageNodeRpcServer
                                  -> StorageNode -> ChunkStore
```

The endpoint list and replication factor are command-line inputs. The executables are separate entry points, not a single coordinator process.

## Local file upload and download

1. `dfs_cli upload FILE_ID SOURCE` invokes the local storage service.
2. `FileTransfer` reads incrementally through a reusable buffer capped at 4 MiB. It stores each non-empty chunk and builds an ordered manifest with sizes and SHA-256 checksums.
3. The manifest includes a digest of the complete file. The service saves it to the local catalog after chunk ingestion.
4. Download validates the manifest, reads and verifies each chunk against the record and manifest, and writes into a temporary destination.
5. Only after the complete-file digest passes is the temporary output renamed to the requested path.

Payload processing is streamed, while manifest memory grows with chunk count. Manifest saving has a POSIX fsync/rename/directory-fsync path. Do not generalize that guarantee to ChunkStore records.

## Distributed chunk PUT and GET

1. The caller supplies ordered node IDs, IPv4 hosts and ports, plus a replication factor.
2. `DistributedChunkService` hashes the chunk ID with FNV-1a, then selects a cyclic slice of distinct endpoints.
3. PUT sends the same bounded chunk to each selected node. TCP frames are length-prefixed and bounded; the node delegates integrity and quota behavior to `StorageNode`.
4. PUT succeeds only if every selected endpoint acknowledges. A failure reports incomplete, but earlier writes are not rolled back.
5. GET tries selected replicas in configured order and returns the first readable checksum-verified chunk. It does not compare every replica during a normal read.

Placement remains stable only when callers use identical membership, ordering and replication factor; these inputs are not stored as cluster metadata.

## Audit and repair

1. Audit recomputes placement and reads every selected replica through the checksum-verifying GET.
2. The first readable payload becomes the comparison baseline. Other readable payloads are compared byte-for-byte; errors are classified as missing, corrupt or unavailable.
3. Repair sends repair-PUT to unresolved targets. Unlike normal PUT, this path can replace an internally corrupt record while retaining quota checks.
4. Each successful repair is read back and compared with the baseline.
5. Offline nodes, failed writes/read-back, and no-source cases remain unresolved. Repair can partially complete across nodes.

There is no authoritative distributed checksum or consensus. If the first readable copy is wrong, it can become the baseline and be propagated.

## Trade-offs and boundaries

| Choice | Reason | Limit |
|---|---|---|
| 4 MiB chunk bound | Predictable payload size per operation | Distributed CLI handles a single chunk, not a streaming file |
| SHA-256 | Detect accidental record/file corruption | Not authentication; cannot decide which divergent replica is correct |
| Explicit binary manifest | Small dependency surface, bounded parsing | Custom format requires compatibility care |
| POSIX TCP protocol | Small learning surface for PUT/GET/STATUS/repair | POSIX IPv4, plaintext, unauthenticated, sequential server |
| FNV-1a plus cyclic placement | Simple deterministic placement | Membership changes can remap chunks; no epochs or persisted map |
| Process-local locks | Same-process safety for shared roots | No cross-process/host coordination |
| Manifest fsync path | Stronger metadata publication ordering | Filesystem dependent and not used for chunk records |

## Source map

| Concern | Main implementation |
|---|---|
| Chunk records/checksums | `src/core/chunk_store.cpp` |
| Streaming file transfer | `src/core/file_transfer.cpp` |
| Manifest persistence/recovery | `src/core/manifest_store.cpp` |
| Local catalog/service | `src/service/metadata_catalog.cpp`, `src/service/storage_service.cpp` |
| Node quota and storage | `src/service/storage_node.cpp` |
| TCP framing, placement, repair | `src/service/storage_node_rpc.cpp` |
| Executable command parsing | `src/app/` |

For failure behavior and interview practice, see [failure scenarios](FAILURE-SCENARIOS.md) and [interview guide](INTERVIEW-GUIDE.md).
