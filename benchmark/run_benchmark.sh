#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================"
echo "       Coi vs React Benchmark"
echo "========================================"

# Check for python3
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 could not be found"
    exit 1
fi

# Run the python runner
python3 runner.py "$@"

echo ""
echo "Dashboard generated at: $SCRIPT_DIR/benchmark_results.svg"
echo "Raw data at: $SCRIPT_DIR/benchmark_results.json"
