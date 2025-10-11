#!/bin/bash
# Bazel wrapper for parallel VM tests
# This wrapper receives the package path from Bazel and passes it to the actual script

set -euo pipefail

# First, validate host dependencies (fail fast if not met)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Find verify_host_deps script
VERIFY_SCRIPT=""
for loc in "$SCRIPT_DIR/verify_host_deps.sh" "$SCRIPT_DIR/../vm/verify_host_deps.sh" "$(pwd)/vm/verify_host_deps.sh"; do
    if [ -f "$loc" ]; then
        VERIFY_SCRIPT="$loc"
        break
    fi
done

if [ -n "$VERIFY_SCRIPT" ]; then
    echo "Validating host dependencies..."
    if ! "$VERIFY_SCRIPT"; then
        echo
        echo "❌ Host dependencies not met. Cannot proceed with VM testing."
        echo "Install missing packages as shown above, then try again."
        exit 1
    fi
    echo
else
    echo "⚠️  Warning: Could not find verify_host_deps.sh, skipping validation"
    echo
fi

# Get the package path from Bazel (first argument)
PACKAGE_PATH="$1"

# Find the actual test script in Bazel runfiles
# When running via 'bazel run', we're in the execroot
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Try multiple locations for the script
LOCATIONS=(
    "$SCRIPT_DIR/run-parallel-vm-tests.sh"
    "$SCRIPT_DIR/vm/run-parallel-vm-tests.sh"
    "$(pwd)/vm/run-parallel-vm-tests.sh"
)

TEST_SCRIPT=""
for loc in "${LOCATIONS[@]}"; do
    if [ -f "$loc" ]; then
        TEST_SCRIPT="$loc"
        break
    fi
done

if [ -z "$TEST_SCRIPT" ]; then
    echo "❌ Error: Test script not found. Tried:"
    for loc in "${LOCATIONS[@]}"; do
        echo "  - $loc"
    done
    exit 1
fi

echo "Using test script: $TEST_SCRIPT"
echo "Using package: $PACKAGE_PATH"
echo

# Execute the actual script with the package path
exec "$TEST_SCRIPT" "$PACKAGE_PATH"

