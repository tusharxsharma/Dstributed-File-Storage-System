# Development Progress

## 2026-11-17 — Repository bootstrap

### Completed
- Created the public C++20 project foundation on `main`.
- Added CMake configuration, strict compiler warnings, optional AddressSanitizer/UndefinedBehaviorSanitizer flags, and GoogleTest-based CI.
- Added a small `Result<T>` error-return type.
- Added SHA-256 hashing using OpenSSL EVP.
- Added a local `ChunkStore` prototype with safe chunk-ID validation, a 4 MiB per-chunk limit, temporary-file writes, rename-based commit, and read-time checksum verification.
- Added unit tests for known SHA-256 vectors, empty input, chunk round-trip, invalid IDs, missing chunks, payload corruption, and oversized chunks.
- Documented the architecture and 30-day roadmap.
- Added GitHub Actions workflow for build and test.

### Verification
- GitHub Actions run for commit `ba1f8152eb375099ae60b436130763bbbf0fa595` completed successfully. Configure, build, and test steps all passed.
- Follow-up commit `f2e063c0a07b71dc5b54e7cf88a7956539ffb675` contains a CMake compatibility cleanup and explicit integer header include. Its CI run was still in progress at the time this entry was written.

### Known limitations / next engineering tasks
- This is a local chunk-store prototype, not yet a distributed system.
- Add a true file-streaming API that chunks large input without loading the whole file.
- Define and implement the durability contract with file and directory synchronization.
- Define semantics for concurrent writes to the same chunk ID and portable atomic replacement.
- Add manifest metadata and chunk sequencing.
- Only then introduce the node process and gRPC communication.

## Next milestone
Implement streaming file ingestion and reconstruction on top of `ChunkStore`, with tests that cover multi-chunk files, empty files, and interrupted operations.
