#!/bin/bash
# Wrapper script for Bazel sh_test
# Passes arguments to run-qemu-modular-test.sh

set -euo pipefail

# Find the actual test runner script
SCRIPT_DIR="$(dirname "$0")"
RUNNER="$SCRIPT_DIR/run-qemu-modular-test.sh"

if [ ! -f "$RUNNER" ]; then
    # Try alternate locations (Bazel runfiles)
    for path in \
        "vm/qemu/run-qemu-modular-test.sh" \
        "qemu/run-qemu-modular-test.sh"; do
        if [ -f "$path" ]; then
            RUNNER="$path"
            break
        fi
    done
fi

if [ ! -f "$RUNNER" ]; then
    echo "Error: run-qemu-modular-test.sh not found"
    exit 1
fi

# Pass all arguments through
exec "$RUNNER" "$@"

