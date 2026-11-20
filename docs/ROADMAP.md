# 30-Day Roadmap

A guided implementation plan. Milestones are intentionally small; correctness takes priority over checking off features.

| Days | Focus | Expected outcome |
|---|---|---|
| 1 | Foundation | C++20/CMake project, CI, checksum primitive, local chunk-store prototype |
| 2 | File transfer | Bounded-memory file ingestion, ordered in-memory manifest, verified reconstruction |
| 3 | Manifest persistence | Versioned, bounded binary manifest format; metadata checksum; safe save/reload validation |
| 4 | Local durability | Durable ManifestStore publication: file fsync, rename, parent-directory fsync, and unique temp creation |
| 5 | Recovery hygiene | Stale-temp discovery/cleanup policy and documented crash states |
| 6 | Fault injection | Inject failures at write/sync/rename boundaries and verify recovery/visibility invariants |
| 7 | Concurrency | Explicit same-chunk write policy and concurrency tests |
| 8–9 | Metadata and interface | Metadata model and CLI/API prototype |
| 10–12 | Node service | Storage-node process and health endpoint |
| 13–15 | RPC | Protocol Buffers and gRPC between coordinator and nodes |
| 16–18 | Placement | Chunk placement across nodes and replication factor |
| 19–21 | Failure handling | Node heartbeats, failure simulation, replica repair |
| 22–24 | Reliability | Rebalancing, idempotency, bounded queues and backpressure |
| 25–27 | Operations | Docker Compose, metrics, structured logs, graceful shutdown |
| 28–30 | Validation and demo | Fault-injection tests, benchmarks, architecture docs, demo walkthrough |

## Definition of done

- All unit and integration tests pass.
- Failure modes and consistency limitations are documented.
- A repeatable local multi-node demo works.
- Benchmarks state the hardware, workload, and measurement method.
- No claim of production-grade durability or fault tolerance without corresponding implementation and tests.