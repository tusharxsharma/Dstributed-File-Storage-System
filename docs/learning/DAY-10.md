# Project Day 10 — Operations and integration demo

## Scope
- Added a repeatable Bash demo that launches three RPC nodes locally.
- Demonstrates replicated chunk PUT, GET, byte-for-byte reconstruction validation, and audit/repair/audit.
- Uses existing executables and protocol; no new dependencies or deployment stack.

## Run
`bash scripts/demo-multi-node.sh demo-run build`

## Verification status
GitHub Actions run [45978986022](https://github.com/tusharxsharma/Dstributed-File-Storage-System/actions/runs/45978986022) passed configure, build, all 65 CTest tests, and the three-node smoke demo on PR head commit `81d5547cd882d67503cc72ab5555e318af750c60`. The demo ran on a GitHub-hosted Ubuntu runner. Local execution was unavailable in that environment.\n\n## Limitations
This is a local smoke demo, not a benchmark, cluster-management system, or network security hardening. RPC remains plaintext/unauthenticated. Node membership is supplied manually.