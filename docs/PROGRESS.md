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

### Next milestone — Day 3
Define a versioned manifest representation and persist/reload it safely. Add validation for malformed, truncated and unsupported manifest records before introducing a database.
