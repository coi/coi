#!/bin/bash

# Get the directory where the script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
COI_COLOR='\033[38;2;148;119;255m'  # #9477FF
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
FAILED_TESTS=()
TOTAL=0
PASSED=0

# Count total tests
TOTAL=$(find "$SCRIPT_DIR" -name "*_pass.coi" -o -name "*_fail.coi" | wc -l)

# Function to draw progress bar
draw_progress_bar() {
    local current=$1
    local total=$2
    local width=50
    
    # Calculate filled width
    local filled=$((current * width / total))
    
    # Build the bar
    printf "\r["
    
    # Draw filled portion in coi color
    for ((i=0; i<filled; i++)); do
        printf "${COI_COLOR}█${NC}"
    done
    
    # Draw empty portion
    for ((i=filled; i<width; i++)); do
        printf "░"
    done
    
    printf "] %d/%d" "$current" "$total"
}

# Find all .coi files recursively
while IFS= read -r -d '' test_file; do
    # Get relative path from tests directory for display
    rel_path="${test_file#$SCRIPT_DIR/}"
    filename=$(basename "$test_file")
    
    # Get directory of test file for cleanup
    test_dir=$(dirname "$test_file")
    
    if [[ "$filename" == *"_pass.coi" ]]; then
        OUTPUT=$($COMPILER "$test_file" --cc-only 2>&1)
        if [ $? -eq 0 ]; then
            PASSED=$((PASSED+1))
        else
            FAILED_TESTS+=("$rel_path (expected success, got failure)")
            FAILURES=$((FAILURES+1))
        fi
    elif [[ "$filename" == *"_fail.coi" ]]; then
        OUTPUT=$($COMPILER "$test_file" --cc-only 2>&1)
        if [ $? -ne 0 ]; then
            PASSED=$((PASSED+1))
        else
            FAILED_TESTS+=("$rel_path (expected failure, got success)")
            FAILURES=$((FAILURES+1))
        fi
    fi
    
    # Show progress bar
    CURRENT=$((PASSED + FAILURES))
    draw_progress_bar "$CURRENT" "$TOTAL"
    
    # Clean up any generated .cc files
    rm -f "${test_file%.coi}.cc" "$test_dir/app.cc"
done < <(find "$SCRIPT_DIR" -name "*.coi" -type f -print0 | sort -z)

echo ""  # New line after progress

if [ $FAILURES -eq 0 ]; then
    echo -e "${GREEN}All $TOTAL tests passed!${NC}"
    exit 0
else
    echo -e "${RED}$FAILURES test(s) failed:${NC}"
    for failed_test in "${FAILED_TESTS[@]}"; do
        echo -e "  ${RED}✗${NC} $failed_test"
    done
    echo -e "\n${GREEN}$PASSED passed${NC}, ${RED}$FAILURES failed${NC} out of $TOTAL tests"
    exit 1
fi
