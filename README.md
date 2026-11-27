# Distributed File Storage System

A C++20 learning project that grows from a reliable single-node chunk store into a multi-node distributed file storage system. The emphasis is on correctness, explicit trade-offs, testability, and operational behaviour.

## Project status

**Project Days 1–9 complete; Project Day 10 operations and final integration remains.** The single-node storage foundation, file transfer, manifest persistence, manifest durability/recovery, persistence fault-injection and local same-chunk concurrency policy are complete. The storage-node process, RPC, placement/replication, repair, operations and demo milestones remain; see docs/ROADMAP.md and docs/PROGRESS.md.

## Planned capabilities

- Stream files into bounded-size chunks rather than loading entire files into memory.
- Verify chunk integrity with SHA-256 checksums.
- Expose a storage-node service and metadata model.
- Replicate chunks across independent nodes and recover after node failures.
- Add gRPC communication, metadata persistence, background repair, and observability.

## Technology

- C++20 and CMake
- GoogleTest
- OpenSSL Crypto for SHA-256
- Later milestones: gRPC / Protocol Buffers, PostgreSQL if justified, Docker Compose, Prometheus (and optional Grafana)

## Build

Requirements: a C++20 compiler, CMake 3.20+, and OpenSSL development headers/libraries.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

GoogleTest is fetched by CMake when `BUILD_TESTING` is enabled. The first configure requires network access; use `-DBUILD_TESTING=OFF` for an offline build configuration.

## Repository layout

```text
include/dfs/       Public interfaces
src/               Implementation
tests/             Unit and integration tests
docs/              Architecture, decisions, and roadmap
```

## Design principles

1. Bound memory use; large files must be streamed.
2. Verify data using cryptographic checksums.
3. Make failures observable and test recovery through fault injection.
4. Do not claim distributed guarantees until replication, recovery, and consistency behaviour are implemented and tested.

See [docs/ROADMAP.md](docs/ROADMAP.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).


## Local CLI (Project Day 6)

After building, the `dfs_cli` executable accepts a storage-root directory and one command:

```bash
# List metadata catalog entries
./build/dfs_cli ./data list

# Upload a local file using a safe file ID
./build/dfs_cli ./data upload file-001 ./input.bin

# Inspect persisted metadata and chunk references
./build/dfs_cli ./data inspect file-001

# Reconstruct a file from its manifest and verified chunks
./build/dfs_cli ./data download file-001 ./restored.bin
```

The root contains separate `chunks/` and `metadata/` directories. This interface is local and single-process for now; the network storage-node/RPC boundary is a later milestone.


## Storage node console (Project Day 7)

`dfs_node` runs a configurable storage-node process in the foreground. It persists its node identity in the root and enforces a payload-byte capacity across chunk files (capacity checks are serialized among instances in the same process using the same normalized root).

```bash
# Start a node with a 1 GiB payload capacity (default)
./build/dfs_node node-a ./nodes/node-a

# Optional capacity argument, in payload bytes
./build/dfs_node node-b ./nodes/node-b 10485760
```

At the `dfs-node>` prompt, use `status`, `put CHUNK_ID SOURCE_FILE`, `get CHUNK_ID DESTINATION_FILE`, `help`, or `quit`. The console accepts paths without spaces. `put` and `get` operate on one bounded chunk (maximum 4 MiB), not file-level manifests. This is a local node API and process lifecycle; network transport, cross-process quota coordination, and power-loss durability of the node identity are not provided yet.


## TCP RPC storage nodes (Project Day 8 — CI verified and merged)

Start a node RPC server in the foreground (IPv4 host and port; port 0 selects a free port and reports it):

```bash
./build/dfs_rpc_node node-a ./nodes/node-a 127.0.0.1 7101
./build/dfs_rpc_node node-b ./nodes/node-b 127.0.0.1 7102
```

The server accepts `quit` on stdin to stop. For a one-chunk replicated write/read, configure endpoints in the command line:

```bash
# Replicate chunk-001 to two of the three nodes in the supplied order
./build/dfs_cluster_cli put chunk-001 ./chunk.bin 2 node-a 127.0.0.1 7101 node-b 127.0.0.1 7102 node-c 127.0.0.1 7103
./build/dfs_cluster_cli get chunk-001 ./restored.bin 2 node-a 127.0.0.1 7101 node-b 127.0.0.1 7102 node-c 127.0.0.1 7103
```

PR CI verified this milestone with 59/59 tests passing (Debug, ASan and UBSan); the code is merged into main. This RPC prototype handles individual chunks, not a distributed file-manifest upload. Placement is deterministic for the same ordered node list and replication factor; it is not persisted or versioned. Reads fall back across selected replicas. A replication write reports incomplete if any selected replica fails, but it does not roll back partial successes. The custom TCP protocol is plaintext and unauthenticated; do not expose it to untrusted networks.


## Replica audit and repair (Project Day 9 — CI verified)

Audit or reconcile a chunk across its deterministically selected replica set:

```bash
./build/dfs_repair_cli audit chunk-001 3 node-a 127.0.0.1 7101 node-b 127.0.0.1 7102 node-c 127.0.0.1 7103
./build/dfs_repair_cli repair chunk-001 3 node-a 127.0.0.1 7101 node-b 127.0.0.1 7102 node-c 127.0.0.1 7103
```

PR CI run 45952920608 verified the final Day 9 PR head with 65/65 tests passing in 7.05 seconds (Debug, ASan and UBSan). Repair uses the first readable replica as its comparison source, identifies missing, unavailable, internally corrupt or divergent copies, then writes and reads back replacements on reachable targets. If no source can be read, or a node remains offline, the report stays incomplete. The repair RPC is privileged but the prototype transport has no authentication; use only on a trusted network. It is not a distributed consensus protocol and does not persist placement or membership epochs.
