#!/usr/bin/env bash
# benchmark/run_benchmark.sh
# Builds the project, starts a fresh server, runs the benchmark, then
# stops the server.  Full output is tee'd to benchmark/results.txt.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

#  1. Build 
echo "=== Building project ==="
make all

#  2. Fresh data directory 
echo ""
echo "=== Removing old data directory ==="
rm -rf ./vdata
mkdir -p ./vdata

#  3. Start server 
echo ""
echo "=== Starting server (dim=64, port=5556) ==="
./vdb --data ./vdata --dim 64 --port 5556 &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"

# Ensure server is killed when the script exits for any reason
cleanup() {
    echo ""
    echo "=== Stopping server (PID $SERVER_PID) ==="
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    echo "Server stopped."
}
trap cleanup EXIT

# Give the server a moment to bind and start listening
sleep 1

#  4. Run benchmark 
echo ""
echo "=== Running benchmark ==="
# Tee captures all stdout to results.txt while still printing to the terminal.
./benchmark/benchmark localhost 5556 | tee benchmark/results.txt

#  5. Done 
echo ""
echo "=== Benchmark complete. Results saved to benchmark/results.txt ==="
