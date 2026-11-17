# Day 1 — Foundation and first storage primitive

**Date:** 17 November 2026  
**Status:** Complete (initial CI passed; follow-up CI pending at note creation)  
**Milestone:** Repository foundation and local chunk-store prototype

## Goal
Establish a clean C++20 repository, automated build/test pipeline, and the first storage primitive. This is a single-node prototype, not yet a distributed storage service.

## What exists in the repository
- C++20 configured through CMake.
- `dfs_core` library with `Checksum` and `ChunkStore`.
- SHA-256 digest implementation using OpenSSL EVP.
- Chunk IDs restricted to letters, digits, hyphen and underscore.
- Maximum chunk size of 4 MiB.
- Chunk record layout: format marker, 64-character SHA-256 hex digest, then payload bytes.
- Temporary-file write followed by rename to final path.
- Read path validates header, enforces size limit, recomputes SHA-256 and rejects mismatches.
- GoogleTest cases for known SHA-256 vectors, empty input, chunk round-trip, invalid IDs, missing chunks, corruption and oversized chunks.
- GitHub Actions workflow builds and tests the project.

## Code map
- `CMakeLists.txt`: C++20 build, OpenSSL dependency, warnings, optional sanitizers and GoogleTest.
- `include/dfs/common/result.hpp`: explicit value-or-error return type.
- `include/dfs/common/checksum.hpp`, `src/common/checksum.cpp`: SHA-256 API and implementation.
- `include/dfs/storage/chunk_store.hpp`, `src/core/chunk_store.cpp`: local chunk persistence.
- `tests/unit/`: checksum and chunk-store tests.
- `docs/ARCHITECTURE.md`: current and target architecture.
- `docs/ROADMAP.md`: 30-day plan.

## Concepts to learn

### Chunking
A large file is divided into independently addressable blocks. Fixed-size chunks bound the amount of memory needed for one storage operation and allow retries, checksums and later placement across nodes. Day 1 stores a supplied chunk; it does not yet split a whole file.

### SHA-256
A cryptographic hash maps arbitrary bytes to a 256-bit digest. Here it is used to detect accidental corruption when reading stored bytes. A hash is not encryption and does not prove who supplied the data.

### RAII
Resource Acquisition Is Initialization ties resource lifetime to object lifetime. The OpenSSL digest context is owned by a `std::unique_ptr` with a custom deleter, so it is released even if an exception occurs.

### Result<T>
The project uses a small `Result<T>` abstraction to distinguish success from failure. This matters because an empty file/chunk can be valid data and must not be confused with an error.

### Temporary file + rename
Writing to a temporary path and renaming after the write reduces the chance that readers see a partially written final chunk. Exact replacement and atomicity behaviour depends on the operating system and filesystem. It does not by itself establish power-loss durability.

### Flush vs fsync
A C++ stream flush pushes buffered data through the stream layer. On Linux, `fsync` synchronizes modified file data and metadata to storage, and syncing the containing directory may also be needed for the directory entry. The current prototype does not yet implement that durability sequence. See: https://man7.org/linux/man-pages/man2/fsync.2.html

## What is not implemented yet
- Whole-file streaming upload/download.
- File manifests and ordered chunk references.
- Explicit crash/power-loss durability contract.
- Defined same-chunk concurrent-write semantics.
- Storage-node processes, coordinator, gRPC, PostgreSQL, replication or repair.
- Production-grade security or distributed consistency guarantees.

## Verification evidence
- CI run for commit `ba1f8152eb375099ae60b436130763bbbf0fa595` completed successfully: configure, build and test all passed.
- Follow-up commit `f2e063c0a07b71dc5b54e7cf88a7956539ffb675` was queued/in progress at the time this note was written. Verify the latest Actions status before claiming the latest branch is green.
- Do not infer local test results from code being committed.

## Interview questions
1. Why split a large file into chunks instead of loading it all into memory?
2. What does SHA-256 guarantee, and what does it not guarantee?
3. Explain RAII and identify where this project uses it.
4. Why write a temporary file before renaming it?
5. Why is stream flush different from fsync?
6. Why validate chunk IDs before using them in paths?
7. Why does the project use Result<T> rather than an empty vector for errors?
8. What additional work is needed before calling this storage engine durable?

## Hands-on exercises
- Trace `ChunkStore::put` from input bytes to final on-disk record.
- Add a zero-byte chunk round-trip test.
- Inspect a stored chunk in a hex viewer and identify its header, digest and payload.
- Corrupt one byte and verify that `get` rejects the chunk.
- Explain why a checksum does not provide authentication or confidentiality.

## Day 2 preview
Implement streaming file ingestion: read a file in bounded 4 MiB buffers, persist each chunk, and produce an ordered manifest. Test empty files, exact-boundary files, partial final chunks and multi-chunk files.
