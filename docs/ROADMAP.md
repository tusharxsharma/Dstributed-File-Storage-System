# 30-Day Roadmap

A guided implementation plan. Milestones are intentionally small; correctness takes priority over checking off features.

| Days | Focus | Expected outcome |
|---|---|---|
| 1–3 | Foundation | C++20/CMake project, CI, tests, coding conventions |
| 4–6 | Local chunk store | Atomic visibility, checksum verification, bounded chunk API |
| 7–9 | File manifests | Split/reassemble files, metadata model, resumable upload state |
| 10–12 | Node service | Storage-node process and health endpoint |
| 13–15 | RPC | Protocol Buffers and gRPC between coordinator and nodes |
| 16–18 | Placement | Chunk placement across nodes and replication factor |
| 19–21 | Failure handling | Node heartbeats, failure simulation, replica repair |
| 22–24 | Metadata and concurrency | PostgreSQL persistence, locking/idempotency, concurrent operations |
| 25–27 | Operations | Docker Compose, metrics, structured logs, graceful shutdown |
| 28–30 | Validation and demo | Fault-injection tests, benchmarks, architecture docs, demo walkthrough |

## Definition of done

- All unit and integration tests pass.
- Failure modes and consistency limitations are documented.
- A repeatable local multi-node demo works.
- Benchmarks state the hardware, workload, and measurement method.
- No claim of production-grade durability or fault tolerance without corresponding implementation and tests.
