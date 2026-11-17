# Distributed File Storage System

A C++20 learning project that grows from a reliable single-node chunk store into a multi-node distributed file storage system. The emphasis is on correctness, explicit trade-offs, testability, and operational behaviour.

## Project status

**Phase 1 — Foundation (in progress).** The first milestone establishes the build, interfaces, tests, and design constraints. Distributed features in the roadmap are planned, not implemented yet.

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
- Later phases: gRPC / Protocol Buffers, PostgreSQL, Docker, Prometheus

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
