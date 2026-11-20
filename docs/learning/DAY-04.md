# Day 4 — Durable manifest publication

**Date:** 20 November 2026  
**Milestone:** Define and implement a durability sequence for ManifestStore on POSIX  
**Implementation status:** GitHub Actions passed 28/28 tests  
**Implementation commit:** 97ae664d14aada2df0f565d0d6a06ec7123c921e  
**CI run:** https://github.com/tusharxsharma/Dstributed-File-Storage-System/actions/runs/44688113308

## 1. Goal

Day 3 could publish a manifest with a temporary file and rename, but that protects readers from partial publication without guaranteeing that the file's bytes or directory entry survive a sudden power loss. Day 4 makes ManifestStore::save use an ordered POSIX synchronization sequence.

This milestone applies only to manifests. ChunkStore still uses its earlier temporary-file-and-rename pattern without fsync, and the project is not yet a transactional storage system.

## 2. What changed

- ManifestStore creates a unique temporary sibling using a process ID and an atomic sequence.
- The temporary file is opened with O_CREAT | O_EXCL so an existing path is not silently reused, and mode 0600 limits permissions to the owner by default (subject to umask).
- A write loop handles partial writes and retries when write returns EINTR.
- The temporary file is fsync'd before close.
- The file is renamed over the destination, then the containing directory is opened and fsync'd.
- If directory synchronization fails after rename, save reports that publication may already have happened and durability is uncertain; it does not pretend that the file was rolled back.
- Added two tests: replacing a prior manifest and ensuring no temporary file remains after success; rejecting an invalid replacement while retaining the prior manifest.

## 3. Concepts to understand

### Atomic visibility versus durability

Atomic visibility is about what concurrent readers observe: under suitable same-filesystem rename semantics, a reader sees the previous complete file or the newly published complete file, rather than a half-written destination.

Durability is about whether committed bytes and the name that points to them survive a crash or power loss. Rename alone does not establish that guarantee. The file's data must be synchronized, and the directory entry created/changed by rename must also be synchronized.

### Why the sequence matters

~~~text
create unique temporary sibling (exclusive)
       |
       v
write all bytes (short writes / EINTR handled)
       |
       v
fsync temporary file
       |
       v
close temporary file
       |
       v
rename temporary -> destination
       |
       v
fsync containing directory
       |
       v
save reports success
~~~

The file fsync is before rename so the new file's content is synchronized before it is published. The directory fsync is after rename so the directory's updated entry is synchronized. The temporary file is in the destination directory, keeping rename within the same filesystem in normal path resolution.

### Short writes and EINTR

A write system call may write fewer bytes than requested. The loop advances by the number actually written and keeps going until the entire encoded manifest is written. If write is interrupted and reports EINTR, the call is retried. A zero-byte write for a non-empty remaining buffer is treated as a failure instead of looping forever.

### Exclusive temporary creation

O_EXCL with O_CREAT prevents this save from silently opening an existing temporary path. The PID and process-local atomic sequence make names less likely to collide; exclusive creation is the final collision guard. This is not a lock on the destination: two writers can both publish, and whichever rename happens last wins.

### Why fsync the directory?

The rename changes a directory entry. Synchronizing file contents does not by itself guarantee that the new name or replacement is durable. The directory synchronization requests persistence of that metadata change. Filesystems and hardware may have additional caveats; the project avoids claiming a universal power-loss guarantee.

### An error after rename is different

If the directory can be opened or fsync'd only after rename, failure in that step cannot safely be described as "nothing changed." The destination may already be visible. The caller receives an error with an explicit uncertainty message, but this implementation does not yet have a transaction ID or recovery log to resolve the ambiguous outcome automatically.

## 4. Code map

| File | Responsibility |
|---|---|
| include/dfs/storage/manifest_store.hpp | Public save/load contract and durability caveat |
| src/core/manifest_store.cpp | Exclusive temp creation, write loop, file fsync, close, rename and directory fsync; existing bounded format/parser retained |
| tests/unit/manifest_store_test.cpp | Replacement, temp cleanup, invalid replacement preservation, plus prior format/integrity tests |
| docs/ARCHITECTURE.md | Updated distinction between manifest durability and ChunkStore publication |
| docs/ROADMAP.md | Split local hardening into Days 4, 5 and 6 |
| docs/PROGRESS.md | Day 4 implementation and verification record |

## 5. Data flow and design decisions

The manifest still passes through the same bounded, versioned encoder (maximum 64 MiB and 300,000 chunk records). The save path then writes the encoded byte vector to the exclusive temporary file. Parser, format and checksum behaviour are unchanged.

1. **POSIX calls rather than streams for save:** streams do not expose the file descriptor needed for fsync. The POSIX path is explicit and isolated; unsupported platforms return an error rather than silently falling back to weaker durability.
2. **Same-directory temp:** keeps rename on the same filesystem and avoids copy-and-delete publication.
3. **Unique + exclusive temp:** reduces collision risk and prevents truncating another existing temp file.
4. **0600 mode:** manifests may reveal file layout metadata; the temp starts owner-only by default. This does not replace access control or encryption.
5. **File sync before rename:** synchronized bytes are published only after the write/flush boundary is confirmed.
6. **Directory sync after rename:** requests durability of the name change.
7. **Ambiguous post-rename errors:** error text states that the manifest may be visible. Retrying may replace it again; automatic idempotency/reconciliation is not yet implemented.
8. **Small milestone:** only ManifestStore is hardened; chunk writes and the chunk+manifest transaction are deferred.

## 6. Failure modes and current behaviour

| Failure | Behaviour |
|---|---|
| Invalid path or manifest | Rejected before publication |
| Parent directory creation fails | Error; no destination publication |
| Temporary filename collision | O_EXCL rejects it and another suffix is tried, up to 64 attempts |
| Temporary file cannot be created | Error |
| Write fails or is interrupted | EINTR retries; other failures return an error and best-effort temp cleanup occurs |
| File fsync fails | Temp is closed and cleaned; destination is not renamed |
| Close fails | Temp cleanup attempted; destination is not renamed |
| Rename fails | Temp cleanup attempted; destination is not reported as updated |
| Parent directory open fails after rename | New file may already be visible; durability uncertain |
| Directory fsync fails after rename | New file may already be visible; durability uncertain |
| Process crashes before rename | Old destination remains; temp file may remain |
| Process crashes after rename but before directory fsync | New destination may be visible, but the directory update's durability is not confirmed |
| Two concurrent writers to same destination | No destination lock; last successful rename wins |
| Non-POSIX platform | Durable save returns unsupported-platform error |
| ChunkStore write | Still no fsync sequence; durability not promised |

## 7. Verification evidence

**GitHub Actions run:** https://github.com/tusharxsharma/Dstributed-File-Storage-System/actions/runs/44688113308  
**Tested commit:** 97ae664d14aada2df0f565d0d6a06ec7123c921e  
**Build command:** cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DDFS_ENABLE_SANITIZERS=ON  
**Result:** configure passed, build passed, and CTest passed 28/28 (100%) in 5.25 seconds. AddressSanitizer and UndefinedBehaviorSanitizer were enabled.

New tests:
- ManifestStoreTest.ReplacesExistingManifestAndRemovesTemporaryFile
- ManifestStoreTest.RejectsInvalidReplacementAndKeepsPreviousManifest

Local clone/build could not be performed in this environment because the GitHub hostname could not be resolved. The full configure, build and test suite was verified remotely through GitHub Actions instead; no local test success is claimed.

## 8. Interview questions and answer outlines

1. **Atomicity versus durability?** Atomicity/visibility describes whether readers observe a partial update; durability concerns surviving a crash. Rename helps visibility, while file and directory sync establish an ordered durability request.
2. **Why fsync the file before rename?** It requests synchronization of the new file's data before publishing the new name.
3. **Why fsync the directory after rename?** Rename modifies directory metadata; syncing the directory requests persistence of the entry change.
4. **Why does the temp file need to be in the same directory?** It normally keeps the rename on the same filesystem, where POSIX rename can provide atomic replacement semantics.
5. **What is a short write?** A write call reports fewer bytes than requested; the caller must advance and write the remainder.
6. **What is EINTR?** A syscall was interrupted; for write, retry when no progress is reported through that call.
7. **Why O_EXCL?** Prevents an existing temp name from being opened/truncated by this writer; it is a collision guard, not destination locking.
8. **What if directory fsync fails after rename?** The destination may already be visible. The result is ambiguous, and recovery/reconciliation is needed; it is not safe to claim rollback.
9. **Does fsync guarantee data on every device?** No universal guarantee; filesystem, kernel, hardware and cache behaviour matter.
10. **Why does ChunkStore remain a separate milestone?** The project is adding and testing guarantees incrementally; the chunk+manifest commit protocol and fault injection still need design.

## 9. Hands-on exercises

1. Draw the six-step save sequence from exclusive temp creation through directory fsync.
2. Explain a crash before file fsync, after file fsync but before rename, after rename but before directory fsync, and after directory fsync.
3. Use strace (if available) to observe open, write, fsync, close, rename and the directory fsync during save.
4. Compare O_CREAT|O_EXCL with opening a temporary path using O_TRUNC.
5. Explain why two saves to the same destination can race despite unique temp files.
6. Modify a test to save two distinct manifests to one destination and assert that the final loaded manifest is valid.
7. Propose a transaction/reconciliation protocol for chunk data and its manifest, including the ambiguous directory-sync case.
8. Design an injectable syscall adapter so tests can force write, file-fsync, close, rename, directory-open and directory-fsync failures without relying on flaky real I/O faults.

## 10. Revision flashcards

- **Q:** What does rename provide? **A:** Atomic publication/visibility on suitable same-filesystem semantics; not durability by itself.
- **Q:** What is synced before rename? **A:** The temporary file.
- **Q:** What is synced after rename? **A:** The containing directory.
- **Q:** What does O_EXCL prevent? **A:** Reusing/truncating an existing temp path during exclusive creation.
- **Q:** Why loop on write? **A:** Short writes and EINTR are possible.
- **Q:** What does a directory-fsync error mean after rename? **A:** Publication may already be visible; durable outcome is uncertain.
- **Q:** Is the whole system durable now? **A:** No. ManifestStore's POSIX sequence is hardened; ChunkStore and chunk+manifest transaction are not.
- **Q:** Are concurrent destination writers serialized? **A:** No; last rename wins.

## 11. Known limitations

- This is POSIX-only durable save behaviour; unsupported platforms return an error.
- ChunkStore remains atomic-visibility only and has no fsync sequence.
- No recovery protocol coordinates chunk writes with manifest publication.
- Stale temporary-file recovery is not implemented; crashes can leave temp files.
- No destination lock or writer versioning; concurrent saves can overwrite each other.
- Directory fsync failure after rename leaves an ambiguous visible/durable state.
- Fault-injection tests are not yet present; the tests exercise successful publication/replacement and validation failure, not actual syscall failure points.
- Filesystem/hardware behaviour can vary; no production-grade power-loss guarantee is claimed.

## 12. Day 5 preview

Inventory temporary-file naming and define safe recovery/cleanup. Cleanup must not remove an active writer's temp file, must avoid following unsafe paths, and should be idempotent. Document crash states and recovery boundaries before adding fault-injection hooks on Day 6.
