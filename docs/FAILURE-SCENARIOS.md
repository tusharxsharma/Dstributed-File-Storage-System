# Failure-scenario walkthroughs

These scenarios describe current code paths, not service-level guarantees. The runnable healthy-path demo is in [OPERATIONS-DEMO.md](OPERATIONS-DEMO.md).

## One selected node is offline during GET

The client recomputes the selected list and tries endpoints in order. It can return a later readable, checksum-verified replica. If all selected endpoints fail, GET returns an error. It does not compare all replicas or perform a quorum read, and assumes the same ordered node list and replication factor used on write.

## A node is offline during PUT

The service attempts each selected replica. It reports incomplete unless all requested replicas accept the chunk. Earlier writes are not rolled back, so a partial set may remain. Once endpoints are available, audit/repair can reconcile if a readable source exists.

## A chunk is missing

Audit reports a missing replica when the read error indicates not found. Repair can copy from another readable selected replica and verifies the target by reading it back. With no readable source, the replicas stay unresolved.

## A chunk record is corrupt

GET checks the stored record and fails on invalid header, truncation or checksum mismatch. Audit classifies recognized errors as corrupt. Repair can use a different readable copy and issue repair-PUT, which can replace the corrupt record under the node quota path; then it reads the replacement back. Normal PUT does not have this override.

## Readable replicas diverge

Audit chooses the first readable selected replica as its baseline and reports later byte differences as divergent. Repair copies that baseline to unresolved nodes. There is no trusted expected distributed digest or consensus, so it cannot prove which readable copy is correct. A wrong first readable copy can be propagated.

## A target fails during repair

Repair proceeds target by target and is not transactional across nodes. Failed writes or read-back leave a target unresolved and the report incomplete, while other nodes may already have been repaired. There is no durable repair queue or automatic retry worker.

## Capacity is insufficient

The node rejects a write that exceeds its configured payload-byte quota. Accounting excludes record headers, temporary files and filesystem overhead. Same-process operations for a normalized root are serialized; separate processes/hosts do not share the lock and can race. A replicated write can therefore leave partial data.

## Local file download encounters invalid/missing data

Download validates the manifest and each referenced chunk, then verifies the full-file digest. Output is staged in a temporary destination and published only after verification, so a failed reconstruction is not published as the requested completed output.

## Manifest save fails

On POSIX, manifest publication writes and fsyncs a temporary sibling, closes it, renames it, and fsyncs the parent directory. A pre-rename failure preserves the previous destination; a directory-fsync failure after rename may leave the new manifest visible while durability is uncertain. Filesystem and hardware behavior still matter. ChunkStore records and node identity do not share this same durability sequence.

## What the demo proves

CI run 45978986022 exercised three local RPC nodes, replicated one chunk at factor three, GET, byte comparison, and audit/repair/re-audit on GitHub-hosted Ubuntu. It did not inject node failure, benchmark throughput, test untrusted-network security or establish production availability.
