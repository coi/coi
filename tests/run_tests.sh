#!/bin/bash

# Get the directory where the script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

COMPILER="$PROJECT_ROOT/coi"
SRC="$PROJECT_ROOT/src/main.cc"

# Ensure compiler exists or rebuild
if [ ! -f "$COMPILER" ]; then
    echo "Compiler not found. Building..."
    (cd "$PROJECT_ROOT" && ./build.sh)
    if [ $? -ne 0 ]; then
        echo -e "${RED}Failed to build compiler.${NC}"
        exit 1
    fi
fi

echo "Running tests..."
FAILURES=0

# Find all .coi files recursively
while IFS= read -r -d '' test_file; do
    # Get relative path from tests directory for display
    rel_path="${test_file#$SCRIPT_DIR/}"
    filename=$(basename "$test_file")
    
    if [[ "$filename" == *"_pass.coi" ]]; then
        echo -n "Running $rel_path (Expect Success)... "
        OUTPUT=$($COMPILER "$test_file" --cc-only 2>&1)
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}PASS${NC}"
            # Clean up generated .cc file
            rm -f "${test_file%.coi}.cc"
        else
            echo -e "${RED}FAIL${NC}"
            echo "Output: $OUTPUT"
            FAILURES=$((FAILURES+1))
        fi
    elif [[ "$filename" == *"_fail.coi" ]]; then
        echo -n "Running $rel_path (Expect Failure)... "
        OUTPUT=$($COMPILER "$test_file" --cc-only 2>&1)
        if [ $? -ne 0 ]; then
            echo -e "${GREEN}PASS${NC}"
        else
            echo -e "${RED}FAIL (Unexpected success)${NC}"
            FAILURES=$((FAILURES+1))
        fi
    fi
done < <(find "$SCRIPT_DIR" -name "*.coi" -type f -print0 | sort -z)

if [ $FAILURES -eq 0 ]; then
    echo -e "\n${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}$FAILURES tests failed.${NC}"
    exit 1
fi
