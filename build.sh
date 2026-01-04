#!/bin/bash

set -e

echo "[COI] Compiling compiler..."
# Compile the compiler (now split into multiple files)
g++ -std=c++20 -O3 -o coi src/main.cc src/lexer.cc src/parser.cc src/ast.cc

echo "[COI] Done."

# Check if coi is already linked correctly
if command -v coi >/dev/null 2>&1 && [ "$(command -v coi)" -ef "$PWD/coi" ]; then
    echo "coi is already configured in PATH."
    exit 0
fi

# Only offer install if /usr/local/bin exists (common on Linux/macOS)
# And we are in an interactive terminal and not in CIs
if [ -d "/usr/local/bin" ] && [ -t 0 ] && [ -z "$CI" ]; then
    echo ""
    echo "To use 'coi' from anywhere, it needs to be in your PATH."
    read -p "Would you like to create a symlink in /usr/local/bin? [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        TARGET="/usr/local/bin/coi"
        if [ -w /usr/local/bin ]; then
            ln -sf "$PWD/coi" "$TARGET"
        else
            echo "Need sudo access to write to /usr/local/bin"
            sudo ln -sf "$PWD/coi" "$TARGET"
        fi
        echo "Symlink created: $TARGET -> $PWD/coi"
        echo "You can now use 'coi' command from any directory."
    fi
fi
