# 10-Day Roadmap

The original 30-day roadmap has been compressed into a practical 10-project-day plan without dropping the agreed capabilities. Historical Day 1–6 implementation entries and notes are retained for traceability; the authoritative compressed numbering groups the original Day 4–6 durability, recovery and fault-injection work into Project Day 4.

| Project day | Focus | Expected outcome | Status |
|---|---|---|---|
| 1 | Foundation | C++20/CMake, CI, SHA-256 and bounded local ChunkStore | Complete |
| 2 | File transfer | Streaming upload, ordered manifest, verified reconstruction | Complete |
| 3 | Metadata persistence | Versioned, bounded and checksummed manifest persistence | Complete |
| 4 | Durability and recovery | Durable manifest publication, stale-temp recovery and deterministic persistence fault injection | Complete |
| 5 | Concurrency safety | Defined same-chunk policy, safe temp naming and race-focused tests | In progress |
| 6 | Service/RPC and metadata interface | Storage service process, protobuf/gRPC API and catalog interface | Next |
| 7 | Placement and replication | Small placement policy and replication across configured storage nodes | Planned |
| 8 | Failure handling and repair | Node-health/failure simulation, repair and verification of replica recovery | Planned |
| 9 | Reliability and operations | Idempotency, bounded work/backpressure, deployment, metrics and operational guidance | Planned |
| 10 | Integration and demo | Repeatable multi-node demo, integration/fault validation, final docs and measured limits | Planned |

## Scope and deployment model

- One C++ codebase and one deployable executable with coordinator and storage-node roles/configuration; no Kubernetes and no microservice sprawl.
- gRPC/Protobuf is introduced on Project Day 6, once the storage interface is ready.
- PostgreSQL is introduced only if the metadata catalog needs durable multi-node state; its integration is scoped to the same system and Compose deployment.
- Docker Compose and Prometheus (with a small optional Grafana dashboard) are introduced during Project Day 9 when the node topology and metrics exist.
- Chunk payloads remain bounded by the 4 MiB chunk limit; metadata growth, queues and RPC message sizes must also have explicit limits.
- SHA-256 integrity checks, durability boundaries, concurrency semantics, fault injection and honest consistency/repair guarantees remain central.

## Definition of done

- Configure/build/test can be reproduced with documented commands.
- A repeatable local multi-node demonstration works with the selected deployment mode.
- Tests cover integrity, bounded-input rejection, publication/recovery and replica failure/repair.
- Failure modes, consistency and durability limitations are explicit.
- Any benchmark states the hardware, workload and measurement method.
- No claim of production-grade durability, availability or fault tolerance without corresponding implementation and evidence.
