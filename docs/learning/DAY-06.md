# Day 6 — Deterministic fault injection for manifest persistence

**Date:** 22 November 2026  
**Milestone:** Inject failures at manifest-publication and recovery-cleanup boundaries  
**Status:** Implemented and verified by GitHub Actions (40/40 tests passed)

## 1. Goal

The previous days implemented manifest publication and a conservative stale-temp sweep. A happy-path test cannot tell us what the API promises if the write, synchronization, close, rename or directory-sync operation fails. Today adds a small injectable operations boundary so those failures can be reproduced without depending on hardware faults, filesystem timing or unreliable permission tricks.

## 2. What changed

- Added ManifestStoreSyscalls, a narrow set of callbacks for temporary-file open, write, file/directory fsync, close, rename, parent-directory open and temporary-file removal.
- Added make_default_manifest_store_syscalls(); production construction uses wrappers around POSIX system calls and std::filesystem::remove.
- Added a ManifestStore overload that accepts a shared operations object. Tests copy the default operations and replace only the operation being faulted.
- Routed manifest publication and stale-temp removal through the seam.
- Hardened the write loop against EINTR, short writes, zero-byte writes and a callback returning an impossible byte count.
- Added seven deterministic fault-injection tests covering partial-write failure, file fsync failure, close failure, rename failure, parent-directory open failure, parent-directory fsync failure and stale-file removal failure.

## 3. Concepts to learn

### Fault injection

Fault injection deliberately forces an operation to fail at a selected boundary. It makes failure paths reproducible and allows tests to assert invariants instead of waiting for a disk, kernel or permission failure to happen naturally.

### Dependency injection and seams

ManifestStore now depends on a small operations object. Production receives the default POSIX implementation; a test can override one callback while keeping the rest real. The seam is deliberately narrow: serialization, checksum validation and the ordering of save operations remain in ManifestStore.

### Atomic visibility versus durable publication

The key state transition is rename. Before a successful rename, a failure should leave the previous destination in place. After rename, the new destination may be visible—even if opening or synchronizing the parent directory fails. A post-rename error is therefore an uncertain durability outcome, not necessarily a rollback.

### Partial writes and EINTR

A write may transfer fewer bytes than requested. The loop advances by the actual byte count and retries until all bytes are written. If interrupted with EINTR, it retries without advancing. Zero bytes for a non-empty request is treated as failure to avoid an infinite loop.

## 4. Code map

| File | Responsibility |
|---|---|
| include/dfs/storage/manifest_store.hpp | Declares injectable operation callbacks and the test constructor |
| src/core/manifest_store.cpp | Default POSIX wrappers, injected write/fsync/close/rename/open/remove calls, bounded write loop |
| tests/unit/manifest_store_test.cpp | Seven failure-injection tests asserting publication and cleanup invariants |
| docs/ARCHITECTURE.md | Describes the failure seam and publication outcome boundary |
| docs/ROADMAP.md | Refines Day 6 scope and Day 7 concurrency focus |
| docs/PROGRESS.md | Records implementation and verification state |

## 5. Data flow

~~~text
save(manifest)
    |
    v
encode + validate in memory (bounded to 64 MiB)
    |
    v
exclusive temporary-file open
    |
    v
write_all (partial writes / EINTR)
    |
    v
fsync temporary file
    |
    v
close temporary file
    |
    v
rename(temp, destination)  <-- publication boundary
    |
    v
open parent directory
    |
    v
fsync parent directory    <-- durability request boundary
~~~

The injected callbacks let tests fail one step at a time. The recovery sweep also uses the injected remove callback, so its deletion failure is deterministic.

## 6. Failure-state table

| Injected failure | Expected invariant / result |
|---|---|
| Write after a partial write | Save returns failure; old destination bytes remain; temporary sibling is removed |
| File fsync | Save returns failure before rename; old destination remains; temp cleanup is attempted |
| Close temporary file | Save returns failure before rename; old destination remains; temp cleanup is attempted |
| Rename | Save returns failure; old destination remains; temp cleanup is attempted |
| Open parent directory, after rename | Failure reports that publication already happened; new manifest is visible; durability is uncertain |
| Parent-directory fsync, after rename | Failure reports uncertain durability; new manifest is visible |
| Recovery unlink/removal | Cleanup returns an error and the candidate remains when removal is denied |

These are API-level invariants under the injected conditions, not proof about every kernel, filesystem, device or power-loss scenario.

## 7. Design decisions and trade-offs

1. **Callbacks instead of a broad storage abstraction:** only the operations that need fault control are exposed; encoding, validation and the state machine stay in the class.
2. **Tests start with real default operations:** each test overrides one function, reducing the amount of fake behavior and keeping the successful parts of the operation real.
3. **Post-rename errors are not treated as rollback:** rename already changed the visible name. The API communicates uncertainty rather than attempting an unsafe compensating rename.
4. **Cleanup uses the same removal callback:** stale-temp removal failure can be tested without relying on filesystem permissions that vary across CI runners.
5. **Write-loop validation:** injected callbacks are also checked for impossible byte counts so a malformed test seam cannot silently advance beyond the requested buffer.
6. **No production fault switch:** callers inject an operations object at construction; normal construction always uses the default system-call wrapper set.

## 8. Failure modes and behavior

- A pre-rename failure preserves the prior manifest if rename has not succeeded.
- Temporary-file cleanup is best-effort after a pre-rename error; a cleanup error can leave a temp file behind for the explicit recovery sweep.
- A post-rename directory-open or directory-fsync error leaves the new manifest potentially visible and durability unconfirmed.
- The cleanup sweep can partially remove earlier candidates before a later candidate fails; repeating the sweep is safe for already removed candidates.
- The syscall seam is intended for tests and local POSIX behavior. It does not make the storage API portable to non-POSIX systems.

## 9. Verification evidence

- **Added test cases:**  
  1. InjectedWriteFailurePreservesOldManifestAndCleansTemp  
  2. InjectedFileSyncFailurePreservesOldManifest  
  3. InjectedCloseFailurePreservesOldManifest  
  4. InjectedRenameFailurePreservesOldManifest  
  5. InjectedParentDirectoryOpenFailureReportsPublishedButUncertain  
  6. InjectedParentDirectorySyncFailureReportsVisibleManifest  
  7. InjectedRecoveryRemoveFailureLeavesCandidateAndReportsError
- **Local configure/build/CTest:** not run. Cloning from github.com failed because DNS resolution was unavailable in this environment.
- **GitHub Actions:** run 44994371354 (C++ CI) verified commit 449f013c773fe763b65c846b9079f5bd5b9bea3a. Configure, build and CTest completed successfully; all 40/40 tests passed in 3.69 seconds. Debug CI enabled AddressSanitizer and UndefinedBehaviorSanitizer.
- Local build was not performed because cloning from github.com failed due to DNS resolution. CI is the verification evidence.

## 10. Interview questions and answer outlines

1. **What is fault injection?** Force a selected operation to fail deterministically so the test can validate invariants and failure handling.
2. **Why use dependency injection for syscalls?** It separates the behavior under test from OS failure timing and makes one boundary replaceable without mocking the whole storage class.
3. **Why does rename define an important visibility boundary?** Before rename, the temporary file is not the destination. After successful rename, the new name is visible, even if later durability work fails.
4. **Why does a directory-fsync failure not mean the rename was rolled back?** Rename already happened. Synchronization failure means durable persistence of the directory entry is uncertain, not that the visible state reverted.
5. **What is a short write?** A write call returns fewer bytes than requested. The caller must advance by the actual count and continue writing.
6. **Why retry EINTR but not a zero-byte write?** EINTR indicates the operation was interrupted and can be retried. Zero bytes on a non-empty request is treated as no progress.
7. **Why is cleanup best-effort in the destructor?** Destructors should not throw during error handling. Cleanup errors are ignored there; explicit recovery later can report its own errors.
8. **Why test a partial write followed by failure?** It proves that bytes written to a temporary sibling don't corrupt the previously published destination.
9. **What does a post-rename error mean to the caller?** The new manifest may already be visible; the operation did not confirm durability. The caller must reconcile state rather than assume rollback.
10. **What remains beyond this seam?** Concurrency policy, chunk/manifest transaction coordination, platform-specific durability guarantees, crash recovery reconciliation and broader fault injection.

## 11. Hands-on exercises

1. Trace the save() calls and write down exactly where the destination becomes visible.
2. Change the injected write test to return a short positive count several times before failing; explain how write_all advances.
3. Inject EINTR once and then allow the real write to succeed. Verify that the operation retries.
4. Make rename succeed but directory fsync fail; inspect the destination and explain why the error is ambiguous.
5. Propose a result type that distinguishes NotPublished, PublishedDurabilityUnconfirmed and Durable without relying only on error-message text.
6. Design a process-safe destination lock. Explain how it affects same-file writers and recovery.
7. Add a failure case for temporary-file creation and identify which cleanup steps should not run.
8. Explain why simulated syscall failures cannot prove behavior during actual sudden power loss.

## 12. Revision flashcards

- **Q:** Where is the manifest publication boundary? **A:** Successful rename.
- **Q:** What is the pre-rename failure invariant? **A:** The prior destination remains published; temporary cleanup is attempted.
- **Q:** What is the post-rename directory-sync failure state? **A:** New file may be visible, but durability is uncertain.
- **Q:** What is a short write? **A:** A write returns fewer bytes than requested; continue from the returned byte count.
- **Q:** Why retry EINTR? **A:** The write was interrupted; retry without advancing.
- **Q:** Why use an injected operations object? **A:** Deterministic testing of failure boundaries.
- **Q:** Does this seam make the storage portable? **A:** No; the production default still uses POSIX operations.
- **Q:** Does CI passing prove power-loss durability? **A:** No; it validates the tested behavior, not all hardware/filesystem crash states.

## 13. Known limitations

- CI verification is not yet confirmed for the pushed code.
- No local build was possible in this environment due to DNS failure.
- The seam covers manifest publication and recovery removal; it does not cover ChunkStore or FileTransfer syscalls.
- No destination locking or same-manifest concurrent-writer policy exists.
- PID-based cleanup still has PID reuse, namespace and scan/unlink race limitations.
- Directory-close errors after a successful directory fsync are still ignored.
- Actual crash/power-loss behavior depends on OS, filesystem, storage device and hardware.
- The injected test close failure closes the actual descriptor before returning a simulated error; it doesn't model every close-error behavior.

## 14. Day 7 preview

Define same-chunk and same-manifest concurrency semantics. Introduce a narrowly scoped synchronization policy and tests for concurrent writes to the same chunk ID, conflicting manifest publication and cleanup interactions. Avoid promising cross-process locking until the design actually implements it.
