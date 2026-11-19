# Day 3 — Versioned manifest persistence

**Date:** 19 November 2026  
**Milestone:** Persist a file's chunk map in a bounded, versioned format and safely reload it  
**Implementation status:** Complete — local smoke verification and full GitHub Actions test suite passed  
**Implementation commit:** 931443c23ee28cb24d93d239b9d1947df349c30e  
**Final verified commit:** ecd66a447bbf1f654608e0658cca4ccf9c64be2e  
**CI/test evidence:** https://github.com/tusharxsharma/Dstributed-File-Storage-System/actions/runs/44473663953

## 1. Goal

Day 2 could split and reconstruct a file, but the FileManifest existed only in RAM. If the process exited, chunk files could remain on disk while the information needed to find them disappeared.

Day 3 adds a persistence boundary: ManifestStore writes a manifest to a file and can load it later. This is not a database and it does not make chunk writes power-loss durable. It gives the project a restart-readable metadata representation with strict input bounds.

## 2. What changed

- Added include/dfs/storage/manifest_store.hpp and src/core/manifest_store.cpp.
- Added ManifestStore::save and ManifestStore::load.
- Added a deterministic version-1 binary format with a magic value, explicit little-endian integers, length-prefixed identifiers and a SHA-256 trailer covering all preceding manifest bytes.
- Added a 64 MiB encoded-file limit and a 300,000-record limit.
- Added parser checks for unsupported versions/flags, malformed lengths, impossible counts, truncation, trailing bytes, checksum mismatch and inconsistent manifest fields.
- Added eight GoogleTest cases and registered the new source/test in CMake.
- Updated architecture and roadmap. The manifest must still be explicitly saved by the caller; upload does not persist it automatically.

## 3. Concepts to understand

### Why persistence is more than writing a struct

A C++ struct is an in-memory layout. Its padding, alignment, endianness and representation are not a stable file format. Writing sizeof(FileManifest) bytes would also write pointer/vector internals, not the vector's contents. A persistent format must define exactly which fields are written and how each field is encoded.

### Versioning and forward compatibility

The file starts with the eight-byte magic DFSMAN01, followed by a 16-bit format version. Version 1 is accepted. Unknown versions are rejected rather than guessed. If a future format changes field meanings or encoding, it should get an explicit version and migration/compatibility policy.

### Little-endian integers

Multi-byte integers are written least-significant byte first. The reader reconstructs each integer from its bytes rather than relying on the host CPU's byte order. This makes the file representation deterministic across little-endian and big-endian machines.

### Length-prefixed strings

The file stores a string's length before its bytes. The reader validates the length against a field-specific maximum and the number of bytes remaining before allocating/copying the string. This avoids trusting unbounded lengths from a corrupted file.

### Checksumming metadata

The final 64 ASCII characters contain the lowercase hexadecimal SHA-256 digest of every preceding encoded byte. A changed byte in the header, ID, chunk metadata or digest field is detected before the manifest is returned. This is corruption detection, not authentication: an attacker who can rewrite the file can also calculate a new checksum. A keyed MAC or digital signature would be needed for authenticity.

### Bounded parsing

The loader asks the filesystem for the encoded file size before allocating its byte buffer. Files above 64 MiB are rejected. It also caps chunk count at 300,000 and checks that the remaining bytes could contain at least that many minimum-size records before reserving the vector. This prevents a tiny malformed file from requesting a huge vector allocation.

### Atomic visibility is not durability

save writes a temporary sibling file, flushes and closes it, then renames it to the destination. Readers should see either the old manifest or the newly published one on filesystems with suitable rename semantics. The implementation does not call fsync on the file or containing directory, so sudden power loss can still lose recent data or the directory entry. Days 4–6 will define and test this contract.

## 4. Code map and data flow

| File | Responsibility |
|---|---|
| include/dfs/storage/manifest_store.hpp | Public ManifestStore API and parser bounds |
| src/core/manifest_store.cpp | Format encoding/decoding, validation, checksum, bounded file I/O and temporary-file publication |
| include/dfs/storage/file_transfer.hpp | FileManifest and ChunkReference data model |
| src/core/file_transfer.cpp | Existing streaming upload/download; download revalidates loaded metadata before reading chunks |
| include/dfs/common/checksum.hpp, src/common/checksum.cpp | OpenSSL-backed SHA-256 used for manifest integrity |
| tests/unit/manifest_store_test.cpp | Round-trip, empty, integration and malformed-input coverage |
| CMakeLists.txt | Adds manifest implementation and test target source |
| docs/ARCHITECTURE.md | Current persistence boundary and guarantees |

Version-1 record layout (all integer fields little-endian):

~~~text
8 bytes   magic: DFSMAN01
2 bytes   version: 1
2 bytes   flags: 0 (reserved)
8 bytes   total file size
4 bytes   chunk count
2 bytes   file ID length
N bytes   file ID
64 bytes  whole-file SHA-256 as lowercase hex
repeated chunk records:
  8 bytes   sequence number
  2 bytes   chunk ID length
  N bytes   chunk ID
  8 bytes   chunk size
  64 bytes  chunk SHA-256 as lowercase hex
64 bytes  SHA-256 of all preceding bytes, as lowercase hex
~~~

End-to-end flow:

~~~text
source -> FileTransfer::upload -> FileManifest in memory
       -> ManifestStore::save -> versioned manifest file

later:
manifest file -> ManifestStore::load -> validated FileManifest
              -> FileTransfer::download -> verified chunks
              -> temporary output -> whole-file digest -> rename
~~~

## 5. Design decisions and trade-offs

1. **Small custom binary format:** avoids introducing JSON/serialization dependencies before the data model settles. The trade-off is that format changes and tooling must be maintained by this project.
2. **Explicit version and reserved flags:** unknown versions/flags fail closed. This prevents silently misinterpreting future fields.
3. **Little-endian fixed-width integers:** deterministic representation independent of host architecture.
4. **Length-prefixed identifiers:** allows strict bounds before allocation. File IDs are at most 100 bytes; chunk IDs are at most 128 bytes.
5. **64 MiB file and 300,000 chunk limits:** protect memory use and parser work. A very large file with more than 300,000 chunks cannot be represented by this version.
6. **Whole-manifest SHA-256 trailer:** catches accidental metadata corruption. It is not a security boundary against a malicious writer.
7. **Read into one bounded metadata buffer:** simpler parser and checksum calculation; memory is capped at 64 MiB plus decoded metadata. Streaming parsing could lower peak memory later.
8. **Explicit save call:** keeps FileTransfer focused on transfer and makes the persistence boundary visible. A higher-level coordinator can later coordinate chunk and manifest commits.
9. **Temporary sibling + rename:** avoids publishing a partially written manifest, but file/directory fsync and cross-platform replacement policy are deferred.

## 6. Failure modes and current behaviour

| Failure/input | Behaviour |
|---|---|
| Empty path or path without filename | Save/load return an error |
| Invalid file ID, chunk ID or digest | Save rejects the manifest; load rejects decoded metadata |
| Non-contiguous sequence or mismatched chunk ID | Manifest rejected |
| Zero-sized or oversized chunk | Manifest rejected |
| Sum of chunk sizes differs from file size | Manifest rejected |
| Integer sum would overflow | Manifest rejected |
| File larger than 64 MiB | Rejected before allocating the read buffer |
| More than 300,000 records | Rejected before vector reservation |
| Truncated header/record/trailer | Rejected; no partial FileManifest is returned |
| Unsupported version or non-zero flags | Rejected explicitly |
| Changed metadata byte | SHA-256 trailer mismatch is reported |
| Unexpected bytes before trailer | Rejected as trailing data |
| Temporary-file open/write/close failure | Save returns an error; best-effort temporary cleanup is attempted |
| Rename failure | Save returns an error; final path is not reported as updated |
| Process/power loss during save | No crash-durability guarantee; stale temp files may remain |
| Concurrent processes saving same path | No cross-process lock or unique-name coordination yet |

## 7. Tests and verification evidence

### Local build

Command:

~~~bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF -DDFS_ENABLE_SANITIZERS=ON
cmake --build build --parallel 2
~~~

Result: configure and library build passed with GCC 14.2, C++20, OpenSSL 3.5.7, strict warnings, AddressSanitizer and UndefinedBehaviorSanitizer. BUILD_TESTING=OFF was necessary because this environment could not resolve GitHub to fetch the pinned GoogleTest dependency.

### Local integration smoke

A standalone ASan/UBSan executable passed this path:

1. Create a source file slightly larger than 4 MiB.
2. Upload it into ChunkStore, producing two chunks.
3. Save the manifest through ManifestStore.
4. Open a new ManifestStore instance and reload the manifest.
5. Download using the reloaded metadata.
6. Compare source and restored file byte-for-byte.
7. Confirm unsupported-version and oversized-file guards reject their inputs.

A second local ASan/UBSan check passed empty-manifest round-trip, invalid-ID rejection, truncation rejection, unsupported-version rejection, checksum-corruption rejection and oversized-file rejection.

### GoogleTest cases added

- SavesAndReloadsManifestDeterministically
- RoundTripsEmptyFileManifest
- PersistsUploadedManifestForLaterReconstruction
- RejectsInvalidManifestBeforeCreatingFile
- RejectsTruncatedManifest
- RejectsUnsupportedFormatVersion
- DetectsManifestChecksumCorruption
- RejectsOversizedFileBeforeReadingIt

The existing 18 tests remain in the target, giving 26 total tests. GitHub Actions run 44473663953 tested commit ecd66a447bbf1f654608e0658cca4ccf9c64be2e: configure passed, build passed, and CTest passed 26/26 (100%) in 3.52 seconds. CI used Debug mode with AddressSanitizer and UndefinedBehaviorSanitizer enabled. The preceding run failed at configure because the GoogleTest archive URL was missing the organization segment; that URL was corrected before the successful run.

## 8. Interview questions and answer outlines

1. **Why not write a C++ struct directly to disk?**  
   Struct layout can include padding and host-specific representation. A vector contains process-local pointers/capacity. A file format must explicitly encode stable fields and byte order.

2. **What problem does a format version solve?**  
   It lets readers identify the schema they are parsing. Unknown versions can be rejected instead of interpreting fields using the wrong layout.

3. **Why use length-prefixed strings?**  
   The reader knows how many bytes belong to a field and can validate the length before allocation or copying.

4. **Why is the manifest parser bounded?**  
   Metadata files are input and can be corrupt or hostile. File-size, string-length and record-count limits prevent unbounded allocation and excessive work.

5. **What does the trailing SHA-256 protect?**  
   It detects accidental changes to the encoded metadata. It does not authenticate the writer because an attacker can recompute an unkeyed hash.

6. **Why is a checksum not a MAC?**  
   A checksum/hash has no secret key. Anyone who can modify the content can calculate a replacement digest. A MAC uses a shared secret; a signature uses a private key.

7. **What is the difference between atomic rename and durable commit?**  
   Rename can provide atomic visibility of a name change. Durability after power loss needs file data/metadata and often the parent directory entry to be synchronized.

8. **Why check the chunk count against remaining bytes before reserve?**  
   A tiny corrupt file could claim hundreds of thousands of chunks. The preflight check avoids allocating a large vector when the encoded bytes cannot contain those records.

9. **Why does upload not automatically save the manifest?**  
   FileTransfer handles chunk movement. An orchestration layer should decide when metadata is committed and how to reconcile partial chunk writes; that transaction boundary is not implemented yet.

10. **What remains before restart recovery is reliable?**  
    Persist chunks and metadata with a defined commit order, add fsync semantics, handle stale temp files/orphans, and test process crashes at each write boundary.

## 9. Hands-on exercises

1. Open a generated manifest file in a hex viewer and identify the magic, version, total size, chunk count and checksum trailer.
2. Change the version bytes to 2 and observe the explicit unsupported-version error.
3. Flip one byte in a chunk ID without changing the trailer and observe the manifest checksum failure.
4. Truncate the file at several offsets: inside the header, inside a chunk record and inside the checksum trailer. Record the error for each case.
5. Add a test for a chunk count larger than the remaining encoded records and confirm it fails before vector allocation.
6. Explain why a valid manifest checksum still does not prove that the referenced chunk files exist.
7. Propose a commit protocol for chunk data plus manifest metadata. Identify what happens if the process crashes between each step.

## 10. Revision flashcards

- **Q:** What is the version-1 magic? **A:** DFSMAN01.
- **Q:** What byte order is used for integers? **A:** Little-endian.
- **Q:** What is the maximum encoded manifest size? **A:** 64 MiB.
- **Q:** What is the maximum chunk-record count? **A:** 300,000.
- **Q:** What protects the encoded manifest from accidental corruption? **A:** A trailing SHA-256 over all preceding bytes.
- **Q:** Does that checksum authenticate the writer? **A:** No; it is unkeyed.
- **Q:** Does rename guarantee power-loss durability? **A:** No; fsync is not implemented.
- **Q:** Does upload persist metadata automatically? **A:** No; the caller invokes ManifestStore::save.

## 11. Known limitations

- Manifest metadata is read into memory, bounded to 64 MiB; parsing is not streaming.
- The 300,000-chunk cap is a deliberate format limit and may need a new version or pagination for larger workloads.
- Save flushes/closes and renames but does not fsync the file or parent directory.
- Temporary names use a process-local sequence; cross-process collision/locking and stale-temp recovery are not implemented.
- Replacement semantics vary across filesystems and operating systems.
- Manifest SHA-256 detects accidental corruption but provides no authenticity, confidentiality or authorization.
- The caller must explicitly persist the manifest after upload.
- Failed uploads can leave orphan chunks; no transaction log or garbage collector exists.
- The manifest is not indexed in a database and has no ownership or version-history model.
- Source-file snapshotting, same-chunk concurrent-write policy, replication and distributed recovery are still future work.

## 12. Next day

Days 4–6 harden the local storage contract. First define the difference between atomic visibility and durable commit, then add platform-appropriate file and directory synchronization where supported. Introduce fault injection around write, flush, close, rename and restart/recovery boundaries. Keep the documented guarantee limited to what the tests actually demonstrate.