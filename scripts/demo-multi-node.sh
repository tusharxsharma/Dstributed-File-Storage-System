#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-demo-run}"
BUILD="${2:-build}"
BIN="$BUILD"
if [[ ! -x "$BIN/dfs_rpc_node" || ! -x "$BIN/dfs_cluster_cli" || ! -x "$BIN/dfs_repair_cli" ]]; then
  echo "Build executables first (see README)." >&2; exit 2
fi
mkdir -p "$ROOT"
ROOT="$(cd "$ROOT" && pwd)"
TMP="$ROOT/.demo-tmp"
mkdir -p "$TMP"
PIDS=(); FIFOS=(); FDS=()
cleanup() {
  for fd in "${FDS[@]:-}"; do eval "exec $fd>&-" || true; done
  for pid in "${PIDS[@]:-}"; do kill "$pid" 2>/dev/null || true; done
  for pid in "${PIDS[@]:-}"; do wait "$pid" 2>/dev/null || true; done
  rm -f "${FIFOS[@]:-}"
}
trap cleanup EXIT INT TERM

PORTS=(7411 7412 7413)
IDS=(node-a node-b node-c)
for i in 0 1 2; do
  fifo="$TMP/${IDS[$i]}.fifo"; rm -f "$fifo"; mkfifo "$fifo"; FIFOS+=("$fifo")
  # Open read/write to keep the node's stdin pipe alive until cleanup.
  exec {fd}<>"$fifo"; FDS+=("$fd")
  "$BIN/dfs_rpc_node" "${IDS[$i]}" "$ROOT/${IDS[$i]}" 127.0.0.1 "${PORTS[$i]}" <"$fifo" >"$TMP/${IDS[$i]}.log" 2>&1 &
  PIDS+=("$!")
done

NODES=(node-a 127.0.0.1 7411 node-b 127.0.0.1 7412 node-c 127.0.0.1 7413)
payload="$TMP/source.bin"; restored="$TMP/restored.bin"
printf 'Project Day 10 distributed storage demo\n' >"$payload"
# Give the processes a moment to bind their listeners; fail fast if a node exits.
sleep 1
for pid in "${PIDS[@]}"; do kill -0 "$pid" || { echo "A node failed to start; logs: $TMP" >&2; exit 1; }; done

"$BIN/dfs_cluster_cli" put demo-chunk "$payload" 3 "${NODES[@]}"
"$BIN/dfs_cluster_cli" get demo-chunk "$restored" 3 "${NODES[@]}"
cmp "$payload" "$restored"
echo "PASS: replicated PUT, GET and byte-for-byte comparison"
"$BIN/dfs_repair_cli" audit demo-chunk 3 "${NODES[@]}"
"$BIN/dfs_repair_cli" repair demo-chunk 3 "${NODES[@]}"
"$BIN/dfs_repair_cli" audit demo-chunk 3 "${NODES[@]}"
echo "PASS: replica audit and repair workflow completed"
echo "Node logs: $TMP"
