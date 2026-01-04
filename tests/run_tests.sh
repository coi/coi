#!/bin/bash

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

COMPILER="../coi"
SRC="../src/main.cc"

# Ensure compiler exists or rebuild
if [ ! -f "$COMPILER" ]; then
    echo "Compiler not found. Building..."
    (cd .. && ./build.sh)
    if [ $? -ne 0 ]; then
        echo -e "${RED}Failed to build compiler.${NC}"
        exit 1
    fi
fi

echo "Running tests..."
FAILURES=0

# Test 1: Should Pass
echo -n "Running test_pass.coi (Expect Success)... "
$COMPILER test_pass.coi --cc-only > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${GREEN}PASS${NC}"
    rm -f test_pass.cc
else
    echo -e "${RED}FAIL${NC}"
    FAILURES=$((FAILURES+1))
fi

# Test 2: Should Fail (Logic component in view)
echo -n "Running test_fail.coi (Expect Failure)... "
OUTPUT=$($COMPILER test_fail.coi --cc-only 2>&1)
if [ $? -ne 0 ]; then
    # Check if output contains expected error message
    if echo "$OUTPUT" | grep -q "logic-only component"; then
        echo -e "${GREEN}PASS${NC}"
    else
        echo -e "${RED}FAIL (Wrong error message)${NC}"
        echo "Output: $OUTPUT"
        FAILURES=$((FAILURES+1))
    fi
else
    echo -e "${RED}FAIL (Unexpected success)${NC}"
    FAILURES=$((FAILURES+1))
fi

# Test 3: Style Test (Expect Success)
echo -n "Running test_style.coi (Expect Success)... "
$COMPILER test_style.coi --cc-only > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${GREEN}PASS${NC}"
    rm -f test_style.cc
else
    echo -e "${RED}FAIL${NC}"
    FAILURES=$((FAILURES+1))
fi

if [ $FAILURES -eq 0 ]; then
    echo -e "\n${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}$FAILURES tests failed.${NC}"
    exit 1
fi
