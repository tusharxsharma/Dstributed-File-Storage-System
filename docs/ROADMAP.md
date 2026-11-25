# 10-Day Roadmap

The original 30-day roadmap was compressed to ten project days. Historical original Day 1–6 implementation entries and learning notes remain for traceability; compressed Project Days 1–4 group the original foundation, file transfer, manifest persistence, durability, recovery and fault-injection work. Project Day 5 concurrency safety was completed and merged on 23 November 2026. The remaining milestones are scheduled through 28 November, with 29 November reserved for final buffer and deadline reporting.

| Project day | Focus | Expected outcome | Status |
|---|---|---|---|
| 1 | Foundation | C++20/CMake, CI, SHA-256 and bounded local ChunkStore | Complete |
| 2 | File transfer | Streaming upload, ordered manifest, verified reconstruction | Complete |
| 3 | Metadata persistence | Versioned, bounded and checksummed manifest persistence | Complete |
| 4 | Durability and recovery | Durable manifest publication, stale-temp recovery and deterministic persistence fault injection | Complete |
| 5 | Concurrency safety | Defined same-chunk policy, safe temp naming and race-focused tests | Complete |
| 6 | Metadata and user interface | Local manifest catalog, storage facade, CLI upload/download/list/inspect | Complete |
| 7 | Storage-node process | Single-process storage node, configuration, lifecycle and local node interface | Complete |
| 8 | RPC, placement and replication | RPC boundary, simple placement and replication across configured nodes | Planned |
| 9 | Failure handling and repair | Node-failure simulation, repair and replica integrity verification | Planned |
| 10 | Operations and integration | Repeatable multi-node demo, bounded operations, metrics, docs and final validation | Planned |

## Schedule

- **24 November:** Project Day 6 — metadata catalog and local CLI.
- **25 November:** Project Day 7 — storage-node process and lifecycle (PR CI verified; post-merge CI follow-up).
- **26 November:** Project Day 8 — RPC, placement and replication.
- **27 November:** Project Day 9 — failure handling and repair.
- **28 November:** Project Day 10 — operations, integration and demo validation.
- **29 November:** deadline buffer; no new milestone unless needed to fix a blocker.

## Scope and deployment model

- One C++ codebase and one deployable system with configurable process roles; no Kubernetes and no microservice sprawl.
- Keep a local CLI/catalog as the baseline interface. RPC is introduced only when the node boundary and replication path are ready.
- PostgreSQL is introduced only if durable shared metadata needs justify it; do not add a database merely for the demo.
- Docker Compose and Prometheus (with optional Grafana) are introduced during the operations milestone only if they make the repeatable demo and observability materially better.
- Chunk payloads remain bounded by the 4 MiB limit; metadata growth, queues and RPC message sizes must also have explicit limits.
- SHA-256 integrity checks, durability boundaries, concurrency semantics, fault injection and honest consistency/repair guarantees remain central.

## Definition of done

- Configure/build/test can be reproduced with documented commands.
- A repeatable local multi-node demonstration works with the selected deployment mode.
- Tests cover integrity, bounded-input rejection, publication/recovery and replica failure/repair.
- Failure modes, consistency and durability limitations are explicit.
- Any benchmark states the hardware, workload and measurement method.
- No claim of production-grade durability, availability or fault tolerance without corresponding implementation and evidence.
