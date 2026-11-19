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