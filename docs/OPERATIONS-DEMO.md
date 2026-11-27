# Project Day 10: repeatable multi-node demo

This script exercises the existing three-node RPC cluster without adding infrastructure.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Run

On POSIX systems with Bash:

```bash
bash scripts/demo-multi-node.sh demo-run build
```

The script starts three local RPC nodes, stores a sample chunk at replication factor 3, retrieves it, compares the output byte-for-byte, and runs audit/repair/audit. Node logs and temporary demo data are kept under the selected directory; processes are stopped when the script exits.

This is a smoke/demo workflow, not a benchmark or a fault-injection test. The repair tests in the unit suite cover missing, corrupt, divergent, unavailable, and unrecoverable replicas. The RPC protocol remains plaintext and unauthenticated and is intended only for trusted local/demo networks.
