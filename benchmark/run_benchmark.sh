#!/usr/bin/env bash
# benchmark/run_benchmark.sh
# Builds the project, starts a fresh server, runs the benchmark, then
# stops the server.  Full output is tee'd to benchmark/results_<metric>.txt.
#
# Usage:
#   ./benchmark/run_benchmark.sh                     # Euclidean (default)
#   ./benchmark/run_benchmark.sh --metric euclidean  # Euclidean explicitly
#   ./benchmark/run_benchmark.sh --metric cosine     # Cosine similarity

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

#  Parse arguments
METRIC="euclidean"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --metric)
            if [[ -z "${2-}" ]]; then
                echo "Error: --metric requires an argument (euclidean or cosine)" >&2
                exit 1
            fi
            METRIC="$2"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            echo "Usage: $0 [--metric euclidean|cosine]" >&2
            exit 1
            ;;
    esac
done

if [[ "$METRIC" != "euclidean" && "$METRIC" != "cosine" ]]; then
    echo "Error: --metric must be 'euclidean' or 'cosine'" >&2
    exit 1
fi

RESULTS_FILE="benchmark/results_${METRIC}.txt"
echo "Metric     : $METRIC"
echo "Output file: $RESULTS_FILE"

#  1. Build
echo ""
echo "=== Building project ==="
make all

#  2. Fresh data directory
echo ""
echo "=== Removing old data directory ==="
rm -rf ./vdata
mkdir -p ./vdata

#  3. Start server
echo ""
echo "=== Starting server (dim=64, port=5556, metric=$METRIC) ==="
./vdb --data ./vdata --dim 64 --port 5556 --metric "$METRIC" &
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
# Pass the metric-specific output file as the 3rd argument to the benchmark binary.
# The binary prints progress to stdout and saves the results table to $RESULTS_FILE.
./benchmark/benchmark localhost 5556 "$RESULTS_FILE"

#  5. Done
echo ""
echo "=== Benchmark complete. Results saved to $RESULTS_FILE ==="
