# Development Progress

## 2026-11-17 — Day 1: foundation and chunk-store prototype

### Completed
- Created the public C++20 project foundation on main.
- Added CMake configuration, strict compiler warnings, optional AddressSanitizer/UndefinedBehaviorSanitizer flags, and GoogleTest-based CI.
- Added a small Result<T> error-return type.
- Added SHA-256 hashing using OpenSSL EVP.
- Added a local ChunkStore prototype with safe chunk-ID validation, a 4 MiB per-chunk limit, temporary-file writes, rename-based commit, and read-time checksum verification.
- Added unit tests for known SHA-256 vectors, empty input, chunk round-trip, invalid IDs, missing chunks, payload corruption, and oversized chunks.
- Added architecture, roadmap, progress and Day 1 learning documentation.

### Verification
- GitHub Actions runs for commits ba1f8152eb375099ae60b436130763bbbf0fa595, f2e063c0a07b71dc5b54e7cf88a7956539ffb675, and b4c694eef841e9773aba57775ea50b080c17feb4 completed successfully. Configure, build and test passed.

## 2026-11-18 — Day 2: bounded file ingestion and reconstruction

### Implemented
- Added FileTransfer::upload, which reads source files incrementally using one reusable 4 MiB input buffer.
- Added an in-memory FileManifest with file ID, total size, whole-file SHA-256, and ordered chunk references (sequence, deterministic chunk ID, size and per-chunk SHA-256).
- Added FileTransfer::download, which validates manifest shape, loads and verifies each chunk, recomputes the whole-file digest, and publishes the reconstructed file only after successful verification.
- Added tests for multi-chunk files, empty files, exact chunk boundaries, a one-byte partial final chunk, invalid file IDs, missing source files, missing chunks, manifest checksum mismatch, chunk checksum mismatch, non-contiguous sequences and total-size mismatch.
- Updated architecture and roadmap to describe the implemented milestone accurately.

### Verification
- GitHub Actions run: https://github.com/tusharxsharma/Dstributed-File-Storage-System/actions/runs/44254974369
- Tested commit: 62682f4149ed7bbba2f64b316ce283cab3f6791d
- Result: configure passed, build passed, and CTest passed all 18/18 tests (100%).
- CI used Debug mode with AddressSanitizer and UndefinedBehaviorSanitizer enabled.

### Known limitations
- FileManifest is in memory only; process restart loses the mapping from file ID to chunk IDs.
- If ingestion fails after storing some chunks, those chunks may remain orphaned. Cleanup/transaction semantics are a later milestone.
- The source file is not snapshotted; concurrent modification during ingestion is not detected.
- Upload uses a reusable 4 MiB input buffer plus one bounded per-chunk vector because ChunkStore currently accepts a vector. Payload memory remains bounded by chunk size; manifest metadata grows with chunk count.
- ChunkStore reads one full chunk into memory during reconstruction.
- Temporary-file rename does not provide power-loss durability; file and directory synchronization are not implemented.
- Replacement semantics vary by filesystem/OS. Same-chunk concurrent writes remain undefined.

### Next milestone at the end of Day 2
Day 3 was planned to define a versioned manifest representation and persist/reload it safely. The completed Day 3 implementation is recorded below.

## 2026-11-19 — Day 3: versioned manifest persistence

### Implemented
- Added ManifestStore with deterministic version-1 binary serialization and reload.
- Defined the DFSMAN01 magic, little-endian version/flags/size/count fields, length-prefixed IDs, existing SHA-256 metadata, and a trailing SHA-256 over the encoded manifest payload.
- Bounded manifest files to 64 MiB and manifests to 300,000 chunk records. The loader checks the file size before allocation and checks declared record counts against remaining bytes before reserving chunk metadata.
- Added structural validation for file/chunk IDs, digest shape, contiguous sequence numbers, chunk-size limits, integer overflow and total-size consistency.
- Published manifests through a temporary sibling file and rename. The API is explicit: callers save the manifest returned by FileTransfer::upload.
- Added eight GoogleTest cases for deterministic round-trip, empty files, upload/save/reload/download integration, invalid metadata, truncation, unsupported versions, checksum corruption and oversized files.
- Updated architecture and roadmap to reflect that manifest persistence is implemented while durability is not.

### Verification
- Local Debug library configure/build with GCC 14.2, C++20, OpenSSL 3.5.7, strict warnings, AddressSanitizer and UndefinedBehaviorSanitizer: passed with BUILD_TESTING=OFF.
- Local ASan/UBSan smoke executable: passed multi-chunk upload → manifest save → new ManifestStore reload → verified download and byte-for-byte comparison; unsupported-version and oversized-file guards also passed.
- GitHub Actions run: https://github.com/tusharxsharma/Dstributed-File-Storage-System/actions/runs/44473663953
- Final tested commit: ecd66a447bbf1f654608e0658cca4ccf9c64be2e (includes the implementation commit 931443c23ee28cb24d93d239b9d1947df349c30e and a correction to the GoogleTest FetchContent URL).
- Result: configure passed, build passed, and CTest passed all 26/26 tests (100%) in 3.52 seconds. CI used Debug mode with AddressSanitizer and UndefinedBehaviorSanitizer enabled.
- The first run (44473539051) failed during configure because the GoogleTest archive URL was missing the organization segment. The URL was corrected in the follow-up commit; the complete workflow then passed.

### Known limitations
- Manifest metadata is read into memory, bounded to 64 MiB; it is not streamed.
- Save uses flush/close/rename but does not fsync the manifest or parent directory, so power-loss durability is not promised.
- Temporary names use a process-local sequence; cross-process collision/locking and stale-temp recovery are not implemented.
- Rename replacement semantics differ across operating systems.
- The manifest checksum detects accidental corruption but does not authenticate a writer.
- FileTransfer does not automatically call ManifestStore; the caller must persist the returned manifest.
- Failed uploads may leave orphan chunks; no garbage collector or transaction state exists.
- Metadata still has no database index, version history, ownership or access-control model.

### Next milestone — Days 4–6
Harden local persistence semantics: define the exact visibility/durability contract, add platform-appropriate file and directory synchronization where supported, and introduce fault-injection tests for write, flush, close, rename and recovery paths. Do not claim crash durability until those tests demonstrate it.

## 2026-11-20 — Day 4: durable manifest publication

### Implemented
- Reworked POSIX ManifestStore::save to create a unique temporary sibling using exclusive creation (O_EXCL), a process ID and an atomic sequence; temporary files are created with mode 0600.
- Handles short writes and EINTR while writing the encoded manifest.
- Establishes the write ordering: write temporary bytes → fsync temporary file → close → rename over the destination → fsync containing directory.
- Returns a distinct error message when directory synchronization fails after rename, because the new manifest may already be visible while durability is uncertain.
- Added tests for replacing an existing manifest with a different valid manifest, temporary-file cleanup after successful publication, and validation failure preserving the previous manifest.
- Updated architecture and roadmap to distinguish durable manifest publication from ChunkStore's still-atomic-but-not-durable write path.

### Verification
- GitHub Actions run: https://github.com/tusharxsharma/Dstributed-File-Storage-System/actions/runs/44688113308
- Tested implementation commit: 97ae664d14aada2df0f565d0d6a06ec7123c921e
- Result: configure passed, build passed, CTest passed 28/28 tests (100%) in 5.25 seconds. Debug build with AddressSanitizer and UndefinedBehaviorSanitizer enabled.
- Local checkout/build was not available in this environment: cloning from github.com failed due to DNS resolution. The full build and test suite were verified on GitHub Actions instead.

### Known limitations
- Durability support is currently limited to POSIX platforms for ManifestStore; other platforms return an explicit unsupported-platform error.
- ChunkStore still does not fsync its temporary file or parent directory.
- A directory-sync failure after rename is an ambiguous outcome: the destination can be visible, but durable commit is not confirmed.
- No stale-temp recovery, destination locking, transaction protocol for chunk+manifest commit, or injected syscall failures yet.
- Actual persistence guarantees depend on the filesystem, operating system and storage hardware.

### Next milestone
Day 5: identify and safely clean stale manifest/chunk temporary files, document crash points and recovery policy, and avoid deleting active writers' files.


## 2026-11-21 — Day 5: conservative manifest temporary-file recovery

### Implemented
- Added an explicit ManifestStore::cleanup_stale_temporary_files operation scoped to the sibling temporary-name prefix of one manifest target; it is not automatically invoked by save/load.
- The sweep uses a configurable minimum age (24 hours by default), validates the PID and sequence in the temp filename, skips files whose owner process appears alive, skips malformed names, symlinks and non-regular files, and returns the removal count.
- Missing parent directories are treated as an idempotent zero-removal result. Negative grace periods are rejected. The implementation is POSIX-only and fails explicitly elsewhere.
- Added tests for stale-temp removal/idempotence, recent-file retention, active-owner retention, malformed names, unrelated target prefixes, symlinks and invalid age.
- Updated architecture, roadmap and learning notes to describe the bounded scope and race limitations.

### Verification
- The original Day 5 run was not confirmed at the time. Day 6 PR CI run 44994371354 subsequently included and passed all Day 5 recovery tests as part of the 40-test suite.
- Local clone/build could not be run in this environment because github.com DNS resolution failed.

### Known limitations
- This sweeps manifest temporary siblings only; ChunkStore temp naming is not covered.
- PID liveness is local to the current PID namespace and cannot eliminate all PID reuse or scan/unlink races.
- Cleanup is explicit, not automatically run during load/save; callers must choose when to invoke it.
- The 24-hour default avoids aggressive cleanup but is configurable; callers can choose a shorter age and assume more risk.
- Cleanup removals are not directory-fsync'd, so their own power-loss persistence is not promised.
- No cross-process destination lock, journal, chunk+manifest transaction, or power-loss recovery protocol exists.

### Next milestone
Day 6: introduce syscall fault injection around manifest write, file fsync, close, rename, directory open/fsync and recovery cleanup. Verify which cases preserve the old destination, which may already publish the new file, and how stale temp recovery should behave.


## 2026-11-22 — Day 6: deterministic manifest fault injection

### Implemented
- Introduced an injectable ManifestStoreSyscalls operations object for temporary-file creation, write, fsync, close, rename, parent-directory open and removal; normal construction uses POSIX wrappers.
- Routed save publication and stale-temp removal through the seam.
- Hardened write_all against EINTR, short writes, zero-byte progress and impossible callback byte counts.
- Added seven tests: partial write failure, file fsync failure, close failure, rename failure, directory-open failure after publication, directory-fsync failure after publication, and stale-temp removal failure.
- Updated architecture, roadmap and Day 6 learning notes.

### Verification
- The implementation and tests were pushed in commit f67880d55a2a7a64c4569ade3a6776f14d4e4648.
- Expected total: 40 tests (33 existing + 7 new), if configure/build/test discovery succeed.
- Local configure/build/CTest was not performed: github.com DNS resolution failed during clone.
- GitHub Actions run 44994371354 (C++ CI) verified commit 449f013c773fe763b65c846b9079f5bd5b9bea3a: configure passed, build passed, CTest passed 40/40 tests in 3.69 seconds. Debug CI used AddressSanitizer and UndefinedBehaviorSanitizer.

### Failure-state invariants
- Write, file fsync, close and rename failures before publication preserve the previous manifest bytes; temp cleanup is attempted.
- Parent-directory open/fsync failures after rename return an explicit uncertainty error; the new manifest is already potentially visible.
- Injected recovery-removal failure returns an error and leaves the candidate when removal is denied.

### Known limitations
- The injection seam covers manifest save and stale-temp removal only, not ChunkStore or FileTransfer.
- The seam does not prove actual power-loss durability or cross-filesystem semantics.
- No destination locking or concurrent-writer policy exists yet.
- PID cleanup limitations from Day 5 remain.
- No chunk+manifest transaction protocol exists.

### Next milestone
Day 7: define and test same-chunk and same-manifest concurrency semantics, starting with the smallest local synchronization policy and race-focused tests.

## 2026-11-23 — Rescope to 10 project days and Day 5 concurrency

### Numbering reconciliation
- The user requested a 10-day practical scope with four compressed project days complete and six remaining. The repository history actually records six original implementation days.
- Compressed Project Days 1–4 now group original Day 1 (foundation), Day 2 (file transfer), Day 3 (manifest persistence), and original Days 4–6 (manifest durability, stale-temp recovery and deterministic fault injection).
- The original chronological entries and learning notes remain as historical evidence; compressed Project Day numbering is authoritative for the remaining six milestones (Days 5–10).

### Project Day 5: concurrency safety (verified and merged)
- Add process-local per-chunk mutexes keyed by normalized storage root and chunk ID; lock put/get operations across ChunkStore instances sharing that path.
- Include process ID and atomic sequence in chunk temporary filenames to reduce collisions across processes; publication remains rename-based and same-ID writers use last-writer-wins.
- Add a concurrent writer/reader test using two ChunkStore instances and bounded small payloads.
- Link the Threads CMake target.
- Update architecture, README and the compressed roadmap.
- Local clone/build remains blocked by DNS resolution of github.com. PR-head GitHub Actions run 45124844503 verified configure, build and CTest: 41/41 tests passed in 4.76 seconds, with Debug AddressSanitizer and UndefinedBehaviorSanitizer enabled.

### Project Day 5 verification follow-up (23 November 2026)
- PR #3 (https://github.com/tusharxsharma/Dstributed-File-Storage-System/pull/3) was squash-merged after PR-head run 45124844503 passed 41/41 tests in 4.76 seconds.
- Merge commit: 3db133206236686ea355b0d0e5051e8536ee6bef.
- Post-merge main run 45124951840 also completed successfully; configure, build and test steps passed.
- The concurrent test covers eight writers across two ChunkStore instances and a reader for one chunk key. This verifies the local process policy, not inter-process locking or multi-chunk upload transactions.
- The detailed Project Day 5 learning note is local-only at `/mnt/data/dfs-project-notes/docs/learning/DAY-05.md`; it is not committed to the repository pending study.


## 2026-11-24 — Project Day 6: metadata catalog and local CLI (verified)

### Reconciliation and scope
- Project Day 5 concurrency safety was already completed and merged on 23 November; it is not recreated.
- Today's compressed Project Day 6 introduces a small metadata catalog, a storage-service facade over the existing primitives, and a local CLI. RPC and the storage-node process are deliberately deferred to Project Days 7–8.

### Implemented on the milestone branch
- Add `MetadataCatalog` for bounded/checksummed manifest save/load and sorted file-ID listing; catalog keys are safe IDs and loads verify the manifest key matches the requested ID.
- Serialize same-manifest catalog save/load within the process using a path-keyed mutex registry; this is not inter-process locking.
- Add `StorageService` upload/download/metadata/list operations, composing the existing chunk store, transfer and catalog.
- Add `dfs_cli` commands: `list`, `upload`, `download`, and `inspect`.
- Add catalog and storage-service tests for persistence, listing, unsafe/mismatched IDs, empty files, upload/download reconstruction and error propagation.
- Update CMake, README, architecture and roadmap.

### Verification
- Local configure/build/CTest could not be performed because DNS resolution of github.com is unavailable in this environment.
- GitHub Actions run 45340961824 verified commit 4e216c70c6f4701b2c98de8d3e5840d10f98c40c: configure and build passed; CTest passed all 48/48 tests in 3.66 seconds. Debug CI enabled AddressSanitizer and UndefinedBehaviorSanitizer.
- Two initial PR CI attempts exposed stray closing namespace lines in the new test files; both were corrected before this successful run.

### Limitations
- Metadata is one local manifest file per ID; no database, versions, ACLs, transaction across chunk and manifest publication, or orphan-chunk collection.
- Catalog locking is process-local. The CLI is a local interface, not a network service.
- Reusing a file ID replaces chunk and manifest names. Concurrent file-ID uploads and cross-process writers are not coordinated.

### Next milestone
Project Day 7: make the storage-node process configurable and lifecycle-managed, preserving the CLI/service facade and storage/catalog primitives rather than rewriting them.


### Project Day 6 post-merge verification follow-up
- PR #4 (https://github.com/tusharxsharma/Dstributed-File-Storage-System/pull/4) was squash-merged into main as commit 0e19f91123c909be6a60dd2e32b3ae38e850a1eb.
- Post-merge GitHub Actions run 45341128048 completed successfully: configure and build passed; CTest passed 48/48 tests in 3.46 seconds, with Debug AddressSanitizer and UndefinedBehaviorSanitizer enabled.


## 2026-11-25 — Project Day 7: configurable storage-node process (PR CI verified)

### Implemented on the milestone branch
- Add `StorageNode` as a small wrapper around the existing `ChunkStore`, with safe node IDs, root initialization, a persisted node-identity marker and a configurable payload-byte capacity.
- Add node status reporting for payload usage, available capacity and chunk count by scanning regular chunk records.
- Serialize quota checks and writes across same-process `StorageNode` instances sharing a normalized root. Verify an existing chunk before allowing replacement; reject corrupt/unreadable records rather than silently replacing them.
- Add `dfs_node`, a foreground console process with `status`, `put`, `get`, `help` and `quit`; the CLI bounds file input to one 4 MiB chunk and publishes retrieved output via a temporary file.
- Add tests for identity persistence/mismatch, replacement-aware quota accounting, invalid IDs and size, corrupt-chunk replacement rejection, missing chunks and concurrent same-root quota enforcement.
- Update CMake, README and architecture docs.

### Verification
- Local configure/build/CTest was unavailable because github.com DNS resolution failed in this environment.
- PR-head GitHub Actions run 45562363411 verified commit 9ba309c23be5b65ef91e3f915bceb1e3652bcf47: configure and build passed; CTest passed 54/54 tests (100%) in 6.92 seconds. Debug CI enabled AddressSanitizer and UndefinedBehaviorSanitizer.
- PR #5 is ready to merge; post-merge CI verification will be recorded separately.

### Limitations
- Quota locking is process-local; separate processes or hosts can race capacity checks. The scan counts payload bytes from record sizes and ignores filesystem overhead and temporary files. It does not validate every checksum during status.
- Node identity creation is atomic-name publication but not fsync'd; ChunkStore retains its existing durability contract. The console is not a network listener and does not provide RPC, replication or file-level manifest operations.

### Next milestone
Project Day 8: introduce RPC, simple placement and replication across the storage-node boundary, keeping the deployment model single-system and bounded.


### Project Day 7 post-merge verification
- PR #5 was squash-merged into main as commit a7926d1448b559d4e864f6a2fa2911cc2b8ce86e.
- Post-merge GitHub Actions run 45562816481 completed successfully: configure and build passed; CTest passed 54/54 tests (100%) in 5.21 seconds. Debug CI enabled AddressSanitizer and UndefinedBehaviorSanitizer.


## 2026-11-26 — Project Day 8: RPC, placement and replication (implementation staged; CI pending)

### Implemented on the milestone branch
- Add a bounded length-prefixed TCP RPC adapter for storage-node PUT, GET and STATUS; delegates data integrity and quota enforcement to StorageNode.
- Add an RPC client with bounded frames and two-second send/receive socket timeouts.
- Add deterministic distinct-node placement (FNV-1a chunk-ID hash plus cyclic selection over the configured node order), replica writes and read fallback across selected replicas.
- Add `dfs_rpc_node` and `dfs_cluster_cli` for foreground node service and one-chunk replicated write/read demonstration.
- Add TCP integration tests for put/get/status, placement across distinct nodes, fallback after stopping a replica, invalid replication factors, oversized chunks and error propagation.
- Update CMake and documentation. The implementation uses a small native TCP protocol instead of gRPC/Protobuf because the current bounded chunk RPC surface is only three operations; this avoids generated-code/dependency overhead for the prototype.

### Verification status
- Local configure/build/CTest is unavailable in this environment because GitHub DNS resolution fails.
- PR CI is pending. No test pass, merge or push to main is claimed at this stage.

### Limitations
- IPv4/POSIX-only transport, plaintext and unauthenticated; do not expose it to untrusted networks.
- Single-threaded accept/handling loop per server and two-second client I/O timeout; no server-side timeout, concurrent workers, protocol negotiation or inter-process quota lock.
- Stateless placement depends on stable ordered membership and replication factor; no placement metadata or membership epochs.
- A partial replication failure may leave a subset of replicas; no rollback or repair yet. Reads try selected replicas in configured placement order.
- Only individual chunks are replicated. Distributed file-manifest ingestion and metadata placement remain future integration scope.

### Next milestone
Project Day 9: simulate node failure and implement replica verification and repair from surviving copies; test that failed writes and missing/corrupt replicas can be reconciled.


## 2026-11-27 — Project Day 8 verification follow-up

- PR #6 (https://github.com/tusharxsharma/Dstributed-File-Storage-System/pull/6) was squash-merged into main as commit 1066cab11eeb9592b8a048753d71ddc4d2c41e9c.
- PR-head GitHub Actions run 45769571199 verified commit f9d22bbcc224d63281609eeaf106ffcfd3950dd8: configure and build passed; CTest passed 59/59 tests in 4.78 seconds. Debug CI enabled AddressSanitizer and UndefinedBehaviorSanitizer. Post-merge main run is not separately confirmed here.


## 2026-11-27 — Project Day 9: replica audit and repair (PR CI verified)

### Implemented on the milestone branch
- Add a repair-only storage-node write path that replaces a missing, corrupt or divergent record while enforcing the existing node quota. Normal PUT retains its refusal to replace an internally corrupt record.
- Extend the existing TCP protocol with a repair PUT operation; the client and server still use bounded frames and the existing checksum-verified read path.
- Add ReplicaRepairService audit: inspect the selected replica set, choose the first readable copy as a comparison source, classify missing/unavailable/corrupt/divergent replicas, and report unresolved nodes.
- Add repair: restore unverified replicas from the source, read each repaired copy back, and only mark it verified if the bytes match. Reports remain incomplete when a node is offline or no source exists.
- Add dfs_repair_cli audit|repair and integration tests for healthy audit, missing replica, payload corruption, readable divergence, offline-node reporting and total-source loss.
- Update README, architecture, roadmap and progress.

### Verification status
- Local configure/build/CTest is unavailable because GitHub DNS resolution failed.
- PR-head GitHub Actions run 45952748205 verified commit bde12b7c62fb6d63f04ad18ca9bfa16b2e5d8489: configure and build passed; CTest passed 65/65 tests in 4.12 seconds. Debug CI enabled AddressSanitizer and UndefinedBehaviorSanitizer. Post-merge main run is not separately confirmed yet.

### Limitations
- The first readable replica is the repair source. Without a stored expected checksum or authoritative metadata, the service cannot prove that source is the original/correct content; a readable but wrong first replica could propagate.
- Repair targets must be reachable during repair; an offline node remains unresolved until it returns.
- Repair publication is a single-node temporary-file/rename operation, not a distributed transaction. A partial repair can occur if nodes fail midway.
- Repair RPC is unauthenticated and plaintext like the existing RPC; it must not be exposed to untrusted networks.
- Invalid-size chunk records are rejected by capacity scanning and are outside this repair path. Membership and placement are stateless/configured; no consensus, placement epoch or durable repair queue.

### Next milestone
Project Day 10: operations, repeatable multi-node demo, bounded health/metrics, documentation and final integration validation.


### Project Day 9 verification follow-up
- PR #7 was tested on commit bde12b7c62fb6d63f04ad18ca9bfa16b2e5d8489; run 45952748205 completed successfully: configure, build and CTest passed 65/65 tests in 4.12 seconds, with Debug ASan and UBSan enabled.


### Project Day 9 latest PR-head verification and merge
- PR #7 final head c634b6e44d0a9ac83def0775a08c224a1e12a799 was verified by GitHub Actions run 45952920608: configure, build and CTest passed 65/65 tests in 7.05 seconds; Debug ASan and UBSan enabled.
- PR #7 was squash-merged into main as 043a5a8781921a0bd9b0cc624a03063939964220. Post-merge main CI is not separately confirmed.


## 2026-11-27 — Project Day 10: operations/demo integration (review branch)

### Added
- A repeatable Bash smoke demo starts three local RPC nodes, performs replicated PUT and GET, compares the reconstructed bytes, and runs audit/repair/audit.
- Added operations demo documentation and a Day 10 learning note.
- Reconciled README and roadmap schedule/status text.

### Verification
- Implementation is submitted for review on a branch. The demo has not yet been executed in this environment; CI verification is pending. No local execution is claimed.
