#!/bin/bash

set -e

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
    
    # Ensure webcc is in PATH
    export PATH="$PWD/deps/webcc:$PATH"
fi

# Force rebuild schema if requested
if [ "$REBUILD_SCHEMA" = true ]; then
    echo "[Coi] Rebuilding Coi schema..."
    # Remove generated schema files to force regeneration
    rm -f src/coi_schema.h src/coi_schema.cc
    rm -f build/gen_schema build/obj/gen_schema.o
    # Also regenerate .d.coi definition files
    rm -f def/*.d.coi
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
