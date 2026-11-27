# Project interview guide

Use this to rehearse from the code. This is a C++20 learning prototype, not a production storage service.

## 30-second pitch

I built a C++20 distributed file-storage learning project that grows from a bounded, checksummed local chunk store into file manifests, a storage-node process, TCP RPC, deterministic replica placement, and explicit audit/repair workflows. The local file path streams in 4 MiB chunks and verifies chunk and whole-file SHA-256 digests. The distributed path replicates individual chunks across configured nodes and supports read fallback and repair. I focused on making bounds, failures, and trade-offs explicit. GitHub Actions run 45978986022 passed the final build, 65 tests, and a three-node demo.

## Two-minute walkthrough

1. Start at `ChunkStore`: bounded records, safe IDs, SHA-256 verification and temp-file publication.
2. Show `FileTransfer`: streaming upload, ordered manifest and verified download; then `ManifestStore`, `MetadataCatalog`, `StorageService` and `dfs_cli`.
3. Explain `StorageNode` identity and payload quota around the same chunk store.
4. Trace `dfs_cluster_cli` through FNV-1a/cyclic placement, RPC client, node server and chunk store.
5. Explain GET fallback and why PUT can report incomplete after leaving prior successes in place.
6. Trace audit/repair: checksummed reads, first-readable baseline, repair-PUT and read-back verification.
7. Close with the demo evidence and limitations. The demo is a healthy-path smoke test; unit/integration tests cover failure cases.

## Demo script

On POSIX with Bash, CMake, a C++20 compiler and OpenSSL development files:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
bash scripts/demo-multi-node.sh demo-run build
```

The script starts three local RPC nodes, replicates one chunk at factor three, retrieves it, compares bytes, runs audit/repair/audit, stops processes and retains logs/data. It is not fault injection or a benchmark. Run 45978986022 passed configure/build, 65/65 tests and this demo on GitHub-hosted Ubuntu; this is not a local execution claim.

## Design trade-offs

| Choice | Benefit | Cost |
|---|---|---|
| 4 MiB chunk limit | Bounded operation payload | Distributed CLI reads one chunk into memory; no distributed file streaming |
| SHA-256 | Detects accidental corruption/mismatch | No sender authentication or authority among divergent replicas |
| Binary manifest | Small dependency surface, explicit bounded format | Custom versioned format needs compatibility care |
| FNV-1a cyclic placement | Easy to explain and deterministic | Node ordering/membership changes can remap data |
| Small length-prefixed TCP | Inspectable, bounded protocol | POSIX IPv4 only, plaintext, no auth, sequential server |
| Process-local locks | Coordinates same-process users | No cross-process or cross-host coordination |
| First-readable repair source | Demonstrates repair simply | Can propagate a wrong source; no consensus |
| Separate local and distributed paths | Incremental learning and clear boundaries | Whole-file manifests are not distributed through RPC |

## Likely interview questions

**What is distributed?**  
Individual chunk PUT/GET and repair across configured storage nodes. The local file-manifest path is backed by a local catalog/store; distributed file-manifest upload is not integrated.

**What happens when one node is down?**  
GET tries selected replicas in order. PUT reports incomplete if a selected write fails, but it does not roll back earlier successful writes. Audit/repair can reconcile if a readable source and reachable targets exist.

**How is placement computed?**  
FNV-1a over chunk ID, then a cyclic run of distinct endpoints from the configured ordered list. It is not consistent hashing, and callers must use the same configuration.

**What consistency guarantee exists?**  
No general distributed consistency protocol. A successful PUT means all selected targets acknowledged. Failure can leave partial writes; there is no quorum, rollback or version conflict resolution.

**Can repair identify the correct version?**  
No. The first readable replica is the baseline. An authoritative metadata checksum or consensus-backed version would be needed.

**What does SHA-256 guarantee?**  
It detects accidental changes relative to stored checksums. It is neither a MAC nor a signature and does not secure the RPC channel.

**What is bounded?**  
Chunk payload and RPC frames are bounded around 4 MiB. FileTransfer streams payloads but manifest metadata grows with chunk count. Distributed CLI input is one chunk in memory.

**What durability exists?**  
Manifest save has a POSIX temp/fsync/rename/parent-fsync sequence. ChunkStore uses temp-file and rename without claiming the same power-loss durability. Filesystem guarantees vary.

**Why not add gRPC, a database or containers?**  
The current goal was a bounded learning prototype. These dependencies would not resolve semantic gaps such as authoritative replica state, transactions, security or membership.

**How was it verified?**  
Run 45978986022 passed configure/build, 65 CTest tests and the three-node smoke demo. CI enables Debug ASan/UBSan. The demo covers healthy replicated PUT/GET, byte comparison and audit/repair/re-audit; it does not inject failure.

## Final review checklist

### Before presenting
- [ ] Build using the documented instructions on the demo machine.
- [ ] Run the demo and check node logs and cleanup.
- [ ] Explain the difference between local file upload and distributed chunk replication.
- [ ] Explain partial PUT, read fallback, and the first-readable repair limitation.
- [ ] Separate CI evidence from local verification.

### Repository and claims
- [ ] README, roadmap, architecture, and demo instructions agree.
- [ ] Executable names and commands match source.
- [ ] Do not claim distributed file upload, consensus, quorum, encryption, automatic membership, background repair, or production availability.
- [ ] CI evidence states exactly which tests and demo were exercised.
- [ ] State limitations near the capability they qualify.

### Only if scope changes
- [ ] Authoritative metadata/versioning and a defined consistency model.
- [ ] Cross-process quota coordination and safe concurrent file-ID publication.
- [ ] Authenticated/encrypted transport and protocol versioning.
- [ ] Membership management and durable repair scheduling.
- [ ] Cross-platform durability policy and operational packaging.
