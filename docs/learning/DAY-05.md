# Day 5 — Conservative temporary-file recovery

**Date:** 21 November 2026  
**Milestone:** Explicit stale temporary-file cleanup for one manifest target  
**Status:** Implementation submitted; CI verification pending

## 1. Goal

Day 4 made manifest publication request durable synchronization, but a process crash before rename can leave its temporary sibling behind. Recovery hygiene must not blindly delete a file that an active save is still writing. Today adds a small, explicit cleanup API that applies conservative checks to manifest temporary files only.

## 2. What changed

- Added `ManifestStore::cleanup_stale_temporary_files(minimum_age)`, with a 24-hour default grace period.
- The operation scans only the directory containing the target and only filenames beginning with that target's exact `.tmp.` prefix.
- The suffix must encode a positive PID and numeric sequence. Cleanup skips malformed names, symlinks and non-regular files.
- A candidate must be older than the configured age and its owner PID must appear absent (`kill(pid, 0)` returning ESRCH). EPERM and other uncertain errors are treated as alive.
- A missing parent directory returns success with zero removals; negative grace periods fail.
- Cleanup is explicit and is not automatically called by save/load, so the caller controls when a sweep occurs.
- Added five tests for stale removal/idempotence, recent retention, active PID retention, malformed/unrelated/symlink exclusion and negative age.

## 3. Concepts to understand

### Recovery is a policy, not just directory deletion

Temporary files may represent an incomplete operation, but their existence alone does not mean they are abandoned. A cleanup routine must establish that the file is in scope, old enough and not owned by a process that still appears active. A wrong assumption can delete bytes from a live writer.

### Age threshold and process liveness

The 24-hour default is a conservative grace period. It reduces the chance of cleaning a slow or paused writer, but it is not a correctness proof. A caller can configure a shorter threshold. The owner PID check provides a second signal: on POSIX, `kill(pid, 0)` checks process existence/permission without sending a signal. ESRCH indicates no such process in the current PID namespace; EPERM is treated as alive.

### Exact target prefix

A store for `index.manifest` cleans only `index.manifest.tmp.PID.SEQUENCE`. It doesn't sweep every `.tmp` in the directory, because files may belong to another operation. The parser requires numeric suffix fields, and symlinks/non-regular entries are excluded.

### Explicit invocation

Cleanup is not run inside `save()` or `load()`. Automatic cleanup would add deletion side effects to normal persistence operations and could complicate concurrency/recovery reasoning. The caller can invoke the operation at a controlled point, for example during a maintenance pass.

### Limitations of PID checks

PID liveness is local to the machine/container PID namespace. PID reuse and the race between scanning and unlinking are not eliminated. A 24-hour age threshold is a conservative heuristic, not a lease or lock. This routine is not suitable as a cross-host cleanup mechanism for shared storage.

## 4. Code map

| File | Responsibility |
|---|---|
| `include/dfs/storage/manifest_store.hpp` | Declares explicit cleanup API and grace-period default |
| `src/core/manifest_store.cpp` | Parses target-specific temp names, checks age/PID/symlink/regular-file status, removes eligible entries |
| `tests/unit/manifest_store_test.cpp` | Tests deletion eligibility, idempotence and conservative skips |
| `docs/ARCHITECTURE.md` | Recovery hygiene scope and limitations |
| `docs/ROADMAP.md` | Day 5 and Day 6 scope clarified |
| `docs/PROGRESS.md` | Implementation and verification record |

## 5. Data flow

```text
caller chooses maintenance window
       |
       v
ManifestStore(target).cleanup_stale_temporary_files(age)
       |
       v
scan target's parent directory for exact temp prefix
       |
       v
parse PID + sequence; ignore malformed/non-regular/symlink
       |
       v
check file age and owner process liveness
       |
       +-- recent or alive/uncertain -> skip
       |
       +-- old and owner absent -> unlink candidate
       |
       v
return number removed (repeat calls are idempotent)
```

## 6. Design decisions and trade-offs

1. **Target-scoped rather than directory-wide sweep:** avoids deleting temporary files owned by unrelated store instances or future features.
2. **24-hour default age:** reduces risk from long-running/paused writers. It is configurable, so shorter windows carry more risk.
3. **PID check:** a cheap POSIX signal-zero check, treating uncertain errors as active. It is not a lock and cannot eliminate PID reuse.
4. **Ignore symlinks and malformed names:** cleanup never follows symlinks and doesn't guess ownership from an invalid filename.
5. **Explicit API:** cleanup has destructive side effects, so it is not hidden inside load/save.
6. **Return removal count:** the caller can observe what was cleaned; repeat sweeps return zero once eligible files are gone.
7. **Small milestone:** chunk-store temp recovery and transaction-wide orphan cleanup are deferred; this only covers manifest temp names created by Day 4.

## 7. Failure modes and behaviour

| Condition | Behaviour |
|---|---|
| Invalid target path | Error |
| Negative age threshold | Error |
| Parent directory missing | Success with zero removed |
| Name doesn't match target prefix | Ignore |
| PID/sequence suffix malformed | Ignore |
| Candidate is a symlink/non-regular entry | Ignore |
| Candidate younger than threshold | Ignore |
| Owner PID exists or liveness is uncertain | Ignore |
| Old file with absent PID | Remove |
| Directory iteration/removal error | Return error; earlier removals may already have happened, so rerun is safe |
| PID reused or scan/unlink race | Not fully eliminated; age threshold reduces risk but is not a lock |
| Temp file belongs to ChunkStore | Not covered |
| Non-POSIX platform | Explicit unsupported error |

## 8. Verification evidence

- **Build/test:** pending GitHub Actions for the implementation commit; record the exact run and count after it completes.
- **Local verification:** not performed; git clone failed because github.com could not be resolved in this environment.
- Added tests:
  1. `RemovesOldTempsOwnedByDeadProcessesAndIsIdempotent`
  2. `PreservesRecentTempsEvenWhenOwnerIsDead`
  3. `PreservesOldTempsOwnedByAnActiveProcess`
  4. `IgnoresMalformedNamesOtherTargetsAndSymlinks`
  5. `RejectsNegativeCleanupAge`



### Verification follow-up (22 November 2026)

The Day 5 implementation and its five recovery tests were included in the Day 6 pull-request CI suite. GitHub Actions run 44994371354 passed all 40 tests, including the Day 5 recovery cases. This is later verification of the code on the Day 6 tested commit, not a claim that the original Day 5 commit's run was confirmed at the time.

## 9. Interview questions and answer outlines

1. **Why can't recovery delete every temp file it finds?** A writer may still be using it. Cleanup needs scoped eligibility checks and must avoid active files.
2. **Why combine an age threshold with PID liveness?** Age provides a grace period; PID liveness provides a signal that the owning process still exists. Neither is a perfect lock.
3. **What does `kill(pid, 0)` do?** It checks whether a process exists and whether the caller can signal it, without delivering a signal. ESRCH means absent; EPERM implies existence but insufficient permission.
4. **Why treat unknown errors as alive?** Recovery is destructive; uncertain state should be skipped rather than risking deletion.
5. **Why only the target prefix?** A manifest store should not delete unrelated temporary files from other subsystems.
6. **Why ignore symlinks?** Following or deleting a link target can cross the intended directory boundary or act on an unexpected object. The sweep conservatively excludes symlinks.
7. **Is PID-based cleanup safe on shared network storage?** No. PID namespaces and host identity may differ, and PID reuse/races remain. A lease, lock, owner UUID and protocol may be needed.
8. **Why is cleanup explicit?** Deletion is a maintenance side effect. Keeping it separate from save/load makes invocation and recovery policy visible.
9. **What does idempotence mean here?** Repeating cleanup after eligible files are removed should not change the result; subsequent passes return zero removals.
10. **What remains for complete crash recovery?** Transaction state, chunk/manifest commit ordering, fault injection, cross-process coordination and recovery reconciliation.

## 10. Hands-on exercises

1. Read the cleanup API and trace the candidate's checks in the exact order.
2. Create an old temp filename with a malformed suffix and explain why it is ignored.
3. Explain the difference between ESRCH and EPERM from `kill(pid, 0)`.
4. Reduce the age threshold to one second in a test and discuss why that would be risky for production.
5. Add a test for directory iteration failure or a permission-restricted directory where practical.
6. Propose a lease-based or lock-file-based scheme that works better than PID checks for a shared filesystem.
7. Identify the TOCTOU window between checking PID liveness and unlinking. Propose mitigations (for example, a lock/lease or atomic claim protocol) and explain trade-offs.
8. Explain why the chunk-store's current temp filename scheme cannot use this PID-based cleanup without changing its naming and ownership protocol.

## 11. Revision flashcards

- **Q:** Default cleanup grace period? **A:** 24 hours.
- **Q:** Which files are in scope? **A:** Regular files matching the exact target manifest's temp prefix and numeric PID/sequence suffix.
- **Q:** What must be true before deletion? **A:** Candidate is older than the threshold and owner PID is absent.
- **Q:** How are EPERM and unknown PID-check errors treated? **A:** Conservatively as alive.
- **Q:** Is cleanup automatic? **A:** No; the caller explicitly invokes it.
- **Q:** Does it clean ChunkStore temp files? **A:** No.
- **Q:** Does PID liveness prove safe deletion? **A:** No; PID reuse, namespace and scan/unlink races remain.
- **Q:** What does idempotent mean? **A:** Repeating the sweep after cleanup doesn't remove the same file again.

## 12. Known limitations

- POSIX-only PID check and implementation.
- Cleanup covers only manifest temp siblings; ChunkStore temp names are not covered.
- PID namespaces/shared hosts and PID reuse are not fully handled.
- Scan/unlink race remains; this is not a lock or lease.
- Default 24-hour threshold is a heuristic, not a proof of abandonment.
- Cleanup is not automatic and has no scheduling/maintenance coordinator.
- Cleanup removals are not directory-fsync'd; power-loss persistence of the cleanup is not guaranteed.
- No transaction journal, cross-process locking, orphan-chunk reclamation or fault injection yet.

## 13. Day 6 preview

Introduce an injectable syscall boundary for manifest publication. Simulate write, file-fsync, close, rename, directory-open and directory-fsync failures, plus recovery cleanup failure. Define which failures preserve the old destination, which may already publish the new file, and how stale temp recovery should behave. Keep tests deterministic and avoid real hardware-fault assumptions.
