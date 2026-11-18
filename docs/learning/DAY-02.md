# Day 2 — Bounded file ingestion and verified reconstruction

**Date:** 18 November 2026  
**Milestone:** Stream a whole file through the existing chunk store and rebuild it safely  
**Implementation status:** Complete — CI passed  
**Implementation commit:** 62682f4149ed7bbba2f64b316ce283cab3f6791d  
**CI run:** https://github.com/tusharxsharma/Dstributed-File-Storage-System/actions/runs/44254974369

## 1. Goal

Day 1 could store and retrieve one chunk, but a user works with files, not isolated chunks. Today connects that primitive to a whole-file workflow without loading the entire file into RAM.

The upload path reads at most one 4 MiB block at a time. It stores that block, then records where it belongs in a manifest. The download path follows that manifest, validates every chunk, and reconstructs the original bytes.

This is still a single-node file-transfer layer. The manifest is in memory and there are no network nodes or replication yet.

## 2. What changed

- Added FileTransfer::upload and FileTransfer::download.
- Added FileManifest and ChunkReference.
- Upload calculates a SHA-256 digest for each chunk (through ChunkStore) and a streaming SHA-256 digest for the complete file.
- Download validates IDs, digest format, contiguous sequence numbers, per-chunk size bounds and total manifest size before it publishes output.
- Download uses a temporary destination and renames it only after all chunk checks and the complete-file digest succeed.
- Added unit coverage for normal multi-chunk flow and important boundary/failure cases.

## 3. Concepts to understand

### Bounded-memory streaming

A naive implementation might read the complete file into a vector. That makes payload memory grow with file size: a 2 GB file could require roughly 2 GB just for the bytes, often more due to copies.

Streaming processes a file as a sequence of bounded blocks. The upload loop reuses a vector whose size is exactly kMaxChunkSizeBytes (4 MiB). Each iteration reads into that buffer, stores the bytes read, and then reuses the buffer. Payload memory is O(chunk size), rather than O(file size).

The current ChunkStore::put API accepts a vector, so upload makes a bounded copy for the current chunk. Peak payload memory is consequently roughly the reusable 4 MiB input buffer plus one current chunk vector. The manifest itself is O(number of chunks), because it retains one small metadata record per chunk. A future span-based storage API could remove the payload copy.

### Manifest = recipe for reconstruction

A manifest is the file-level index that says which chunks form a file and in what order. Each ChunkReference records:
- sequence: zero-based order, starting at 0;
- chunk_id: deterministic local storage key (file-id-sequence);
- size_bytes: expected payload size;
- sha256: expected chunk digest.

The manifest also stores the total file size and SHA-256 of the concatenated file bytes. Download validates each explicit sequence number so malformed or reordered metadata is rejected.

### Why two checksum levels?

A chunk checksum catches damage to one stored block. The whole-file checksum verifies the concatenated result. It also catches an incorrect manifest that points to valid chunks in the wrong order or with missing/extra content.

SHA-256 provides integrity checking when the expected digest is trusted. It is not encryption, authorization, or proof of who uploaded the data.

### Incremental hashing

Calling the existing one-shot checksum function on the complete file would require retaining all bytes. The private Sha256Stream wrapper uses OpenSSL EVP's init/update/final sequence. Each block is fed to the digest context as it passes through the transfer loop. The digest context is managed by RAII, so its OpenSSL resource is released automatically.

### Atomic visibility versus durability

The download output is written to a temporary sibling path. If a chunk is missing or a digest fails, the temporary output is removed and the final destination is not replaced. Once verification succeeds, the temporary file is closed and renamed.

This protects readers from seeing a partially reconstructed destination on filesystems with suitable rename semantics. It does not guarantee survival after sudden power loss: file and parent-directory fsync are not implemented. Replacement behaviour also varies across operating systems.

## 4. Code map and data flow

| File | Responsibility |
|---|---|
| include/dfs/storage/file_transfer.hpp | Public manifest structures and upload/download API |
| src/core/file_transfer.cpp | Streaming loop, incremental whole-file hash, manifest validation, safe reconstruction |
| include/dfs/storage/chunk_store.hpp | Existing bounded chunk API and 4 MiB limit |
| src/core/chunk_store.cpp | Chunk record persistence and per-chunk verification |
| include/dfs/common/checksum.hpp, src/common/checksum.cpp | Existing one-shot SHA-256; streaming helper uses OpenSSL EVP directly |
| tests/unit/file_transfer_test.cpp | File-level happy path, boundaries and failure tests |
| CMakeLists.txt | Adds new implementation and test source |
| docs/ARCHITECTURE.md, docs/ROADMAP.md | Current architecture and milestone sequencing |

Upload flow:

~~~text
source path
  -> ifstream (binary)
  -> reusable 4 MiB buffer
  -> bounded chunk vector
  -> ChunkStore::put(file-id-sequence)
  -> ChunkReference appended to FileManifest
  -> streaming whole-file SHA-256 finalized
~~~

Download flow:

~~~text
FileManifest
  -> validate IDs, hashes, sequence, sizes
  -> ChunkStore::get(each chunk)
  -> verify chunk size + manifest chunk digest
  -> append bytes to temporary output + update whole-file digest
  -> verify whole-file digest
  -> close + rename temporary output to destination
~~~

## 5. Design decisions and trade-offs

1. Fixed 4 MiB chunk size: simple and bounded; later benchmarks may justify a configurable chunk size. Changing it affects manifest semantics and should be versioned.
2. In-memory manifest: keeps Day 2 focused on transfer correctness. It is not restart-safe; Day 3 adds a versioned persistent representation.
3. Deterministic chunk IDs: make the mapping easy to inspect, but re-uploading the same file ID can overwrite prior chunks. A versioned upload/session ID will be needed for safe updates.
4. Per-chunk plus whole-file checksums: costs extra hashing but provides clear fault localization and end-to-end verification.
5. Temporary destination: avoids publishing incomplete data. Rename is not a substitute for fsync.
6. Sequential reconstruction: simplest correctness baseline. Parallel reads/writes can be considered after metadata and concurrency semantics are defined.
7. Explicit validation: malformed manifests are rejected before storage access, reducing wasted work and unsafe path construction.

## 6. Failure modes and current behaviour

| Failure | Behaviour |
|---|---|
| Invalid file ID | Upload rejects it before opening/storing chunks |
| Missing/unreadable source | Upload returns an error |
| Read error mid-stream | Upload returns an error; already stored chunks may remain orphaned |
| Chunk write failure | Upload reports the failing sequence; earlier chunks may remain |
| Empty source | Valid manifest with zero chunks and SHA-256 of empty input |
| Exact multiple of 4 MiB | No extra zero-byte chunk is created |
| Missing/corrupt stored chunk | Download fails and removes its temporary output |
| Wrong chunk size/hash in manifest | Download rejects the chunk |
| Invalid sequence or total size | Manifest rejected before reading chunks |
| Whole-file checksum mismatch | Temporary output removed; final destination is not replaced |
| Rename/permission failure | Error returned; temporary file cleanup attempted |
| Power loss | Not guaranteed durable; synchronization is not implemented |

## 7. Verification evidence

**GitHub Actions:** https://github.com/tusharxsharma/Dstributed-File-Storage-System/actions/runs/44254974369  
**Commit tested:** 62682f4149ed7bbba2f64b316ce283cab3f6791d  
**Result:** Configure passed; build passed; CTest passed 18/18 tests (100%). The workflow configured Debug mode with AddressSanitizer and UndefinedBehaviorSanitizer enabled.

The 11 new file-transfer tests:
- StreamsMultipleChunksAndReconstructsOriginalFile
- SupportsEmptyFiles
- HandlesExactChunkBoundaryWithoutExtraEmptyChunk
- HandlesPartialFinalChunk
- RejectsUnsafeFileIds
- ReportsMissingSource
- RejectsMissingStoredChunkAndDoesNotPublishOutput
- RejectsManifestChecksumMismatchAndPreservesExistingDestination
- RejectsNonContiguousManifestSequence
- RejectsChunkChecksumMismatchInManifest
- RejectsManifestSizeMismatch

The remaining seven tests cover checksum and ChunkStore behaviour.

## 8. Interview questions and answer outlines

1. **How does streaming keep payload memory independent of file size?**  
   It repeatedly reads into a fixed-size buffer and processes each block before reusing the buffer. Payload memory is O(chunk size), not O(file size). The in-memory manifest still grows O(number of chunks).

2. **Why is a manifest necessary if chunks are already stored?**  
   Chunks alone do not describe which file they belong to or their order. The manifest is the reconstruction index.

3. **Why keep both chunk and whole-file checksums?**  
   Chunk hashes detect and localize individual block corruption. The whole-file hash verifies the exact concatenated byte sequence and catches ordering or manifest mistakes.

4. **What is the difference between EOF and an I/O error in an input loop?**  
   A short final read normally sets EOF and still provides valid bytes via gcount(). badbit, or failbit without EOF, indicates an I/O failure.

5. **Why write downloads to a temporary path?**  
   A failed transfer should not expose a partial final file or destroy a previously valid destination. The rename is the publication step.

6. **Does rename make the data durable after power loss?**  
   No. Durability generally requires synchronizing file contents and relevant directory metadata. This implementation does not claim that guarantee.

7. **What happens if upload fails after several chunks have been stored?**  
   The current version can leave orphan chunks because there is no upload transaction or garbage collector yet. A later session/metadata state machine can identify and clean them.

8. **Why is the manifest validated before downloading?**  
   It rejects malformed IDs, invalid digests, sequence gaps and impossible sizes before file I/O, limiting wasted work and unsafe path construction.

## 9. Hands-on exercises

1. Change the source size to exactly 8 MiB and confirm that the manifest has exactly two chunks.
2. Add a test for a file of 4 MiB + 1 byte.
3. Modify one byte in a stored chunk file and confirm download rejects it.
4. Explain the maximum payload memory used by upload and why it is bounded.
5. Add a span-based ChunkStore::put overload to remove the per-chunk vector copy, then compare tests and API ergonomics.
6. Simulate a source read failure (for example, by abstracting the input stream behind a testable reader) and discuss orphan cleanup.

## 10. Revision flashcards

- **Q:** What is the maximum chunk size? **A:** 4 MiB.
- **Q:** What does the manifest preserve? **A:** File ID, total size, whole-file digest, and ordered chunk references.
- **Q:** Why hash incrementally? **A:** To verify the whole file without storing it all in memory.
- **Q:** When is the reconstructed file published? **A:** After all chunk checks and the whole-file checksum succeed.
- **Q:** Is the manifest persistent? **A:** No, it is currently in memory only.
- **Q:** Does rename guarantee power-loss durability? **A:** No; file and directory synchronization are not implemented.

## 11. Known limitations

- Manifest is not serialized or persisted; process restart loses it.
- Failed uploads can leave orphan chunks.
- The source can change while it is being read; no snapshot or modification-time check exists.
- Upload holds one 4 MiB reusable input buffer and one bounded chunk copy because the current ChunkStore API takes a vector.
- Manifest metadata grows with the number of chunks.
- Download reads one complete chunk into memory at a time.
- Temporary file cleanup is best-effort on exceptions and process termination; crash recovery of temp files is not implemented.
- File and directory fsync are not implemented.
- Same-chunk concurrent writes and portable replacement semantics remain undefined.
- No distributed nodes, network transport, replication, authentication or authorization exist yet.

## 12. Day 3 preview

Design a versioned manifest file format and implement safe serialization/deserialization. The parser must reject unsupported versions, truncated records, oversized counts/strings, invalid checksums and inconsistent lengths. Keep the format deterministic and test malformed input before introducing PostgreSQL.
