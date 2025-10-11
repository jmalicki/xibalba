#!/bin/bash
# Bazel wrapper for parallel VM tests
# This wrapper receives the package path from Bazel and passes it to the actual script

set -euo pipefail

# Get the package path from Bazel (first argument)
PACKAGE_PATH="$1"

# Find the actual test script (it's in runfiles)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The actual script is in the same directory
TEST_SCRIPT="$SCRIPT_DIR/run-parallel-vm-tests.sh"

if [ ! -f "$TEST_SCRIPT" ]; then
    echo "❌ Error: Test script not found: $TEST_SCRIPT"
    exit 1
fi

# Execute the actual script with the package path
exec "$TEST_SCRIPT" "$PACKAGE_PATH"

