#!/bin/bash

set -e

# Check for Clang 16+ (required for full C++20 support)
check_clang_version() {
    if ! command -v clang++ &> /dev/null; then
        echo "Error: clang++ not found. Please install Clang 16 or later."
        echo "  Ubuntu/Debian: sudo apt install clang-16"
        echo "  macOS: brew install llvm"
        exit 1
    fi
    
    CLANG_VERSION=$(clang++ --version | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1 | cut -d. -f1)
    if [ -z "$CLANG_VERSION" ]; then
        echo "Warning: Could not detect Clang version"
    elif [ "$CLANG_VERSION" -lt 16 ]; then
        echo "Error: Clang $CLANG_VERSION detected. Coi requires Clang 16+ for full C++20 support."
        echo "  Ubuntu/Debian: sudo apt install clang-16"
        echo "  macOS: brew install llvm"
        exit 1
    fi
}

check_clang_version

# Ensure git submodules are up to date (deps/webcc)
if [ -d ".git" ] && [ -f ".gitmodules" ]; then
    if [ -d "deps/webcc/.git" ] || grep -q "submodule.*webcc" .gitmodules; then
        # Check if submodule is out of sync
        GIT_SUBMODULE_STATUS=$(git submodule status -- deps/webcc 2>/dev/null || echo "")
        if echo "$GIT_SUBMODULE_STATUS" | grep -q '^[-+]'; then
            echo "[Coi] Updating git submodules (deps/webcc)..."
            git submodule update --init --recursive deps/webcc
        fi
    fi
fi

# Parse arguments
REBUILD_WEBCC=false
REBUILD_SCHEMA=false
for arg in "$@"; do
    case $arg in
        --rebuild-webcc|--force-webcc)
            REBUILD_WEBCC=true
            ;;
        --rebuild-schema|--force-schema)
            REBUILD_SCHEMA=true
            ;;
        --help|-h)
            echo "Usage: ./build.sh [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --rebuild-webcc   Force rebuild the webcc toolchain"
            echo "  --rebuild-schema  Force regenerate Coi schema files (coi_schema.h/cc and def/*.d.coi)"
            echo "  --help            Show this help message"
            exit 0
            ;;
    esac
done

# Force rebuild webcc if requested
if [ "$REBUILD_WEBCC" = true ]; then
    echo "[Coi] Force rebuilding webcc toolchain..."
    if [ -d "deps/webcc" ]; then
        pushd deps/webcc > /dev/null
        ./build.sh --force && WEBCC_EXIT=0 || WEBCC_EXIT=$?
        popd > /dev/null
        # Exit 2 = rebuilt, Exit 0 = up to date, anything else = error
        if [ $WEBCC_EXIT -ne 0 ] && [ $WEBCC_EXIT -ne 2 ]; then
            echo "[Coi] Error: webcc build failed"
            exit 1
        fi
        REBUILD_SCHEMA=true  # webcc rebuilt, so regenerate coi schema
    else
        echo "[Coi] Error: deps/webcc not found"
        exit 1
    fi
fi

# Auto-rebuild webcc if schema.def changed (smart incremental)
if [ -d "deps/webcc" ]; then
    pushd deps/webcc > /dev/null
    ./build.sh && WEBCC_EXIT=0 || WEBCC_EXIT=$?
    popd > /dev/null
    
    # Exit 2 = rebuilt, Exit 0 = up to date, anything else = error
    if [ $WEBCC_EXIT -eq 2 ]; then
        echo "[Coi] WebCC schema changed, regenerating Coi schema..."
        REBUILD_SCHEMA=true
    elif [ $WEBCC_EXIT -ne 0 ]; then
        echo "[Coi] Error: webcc build failed"
        exit 1
    fi
fi

# Force rebuild schema if requested
if [ "$REBUILD_SCHEMA" = true ]; then
    echo "[Coi] Rebuilding Coi schema..."
    # Remove generated schema files to force regeneration
    rm -f src/coi_schema.h src/coi_schema.cc
    rm -f build/gen_schema build/obj/gen_schema.o
    # Also regenerate .d.coi definition files and cache
    rm -rf defs/.cache/
    rm -rf defs/web/
fi

if ! command -v ninja >/dev/null 2>&1; then
    echo "[Coi] Ninja not found. Please install ninja-build."
    exit 1
fi

echo "[Coi] Running Ninja..."
ninja

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
