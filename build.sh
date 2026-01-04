#!/bin/bash

set -e

# Check for webcc dependency
if ! command -v webcc >/dev/null 2>&1; then
    echo "[COI] webcc not found in PATH. Checking submodule..."
    if [ ! -f "deps/webcc/build.sh" ]; then
        echo "[COI] Initializing webcc submodule..."
        git submodule update --init --recursive
    fi
    
    if [ ! -f "deps/webcc/webcc" ]; then
        echo "[COI] Building webcc toolchain..."
        pushd deps/webcc > /dev/null
        # Automatically answer 'y' to the install prompt in webcc's build script
        echo "y" | ./build.sh
        popd > /dev/null
    fi
    
    # Add webcc to PATH for the rest of the script if it was built locally
    export PATH="$PWD/deps/webcc:$PATH"
fi

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
    
    # Offer to install coi
    if ! command -v coi >/dev/null 2>&1 || [ ! "$(command -v coi)" -ef "$PWD/coi" ]; then
        read -p "Would you like to create a symlink for 'coi' in /usr/local/bin? [y/N] " -n 1 -r
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
fi
