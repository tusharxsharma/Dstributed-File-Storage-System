# Project Day 10 — Operations and integration demo

## Scope
- Added a repeatable Bash demo that launches three RPC nodes locally.
- Demonstrates replicated chunk PUT, GET, byte-for-byte reconstruction validation, and audit/repair/audit.
- Uses existing executables and protocol; no new dependencies or deployment stack.

## Run
`bash scripts/demo-multi-node.sh demo-run build`

## Verification status
The script and documentation were committed for CI/review. Execution on the developer machine and CI verification must be recorded separately; this note does not claim a run that has not been observed.

## Limitations
This is a local smoke demo, not a benchmark, cluster-management system, or network security hardening. RPC remains plaintext/unauthenticated. Node membership is supplied manually.