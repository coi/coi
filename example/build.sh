#!/bin/bash

set -e

# Get the directory of this script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

# Compiler path (root of the repo)
COMPILER="$DIR/../coi"

if [ ! -f "$COMPILER" ]; then
    echo "Error: Compiler not found at $COMPILER"
    echo "Please build the compiler first."
    exit 1
fi

# Go to the example directory
cd "$DIR"

# Create output directory
mkdir -p dist

# Copy assets
mkdir -p dist/images
cp "$DIR/../docs/images/logo.png" dist/images/

echo "Building example..."
"$COMPILER" src/App.coi --out dist #--keep-cc

echo "Done. Output generated in example/dist/"

# Start dev server only when not in CI
if [ -z "$CI" ]; then
    echo "Starting development server at http://localhost:8000"
    cd dist
    python3 -m http.server 8000
fi