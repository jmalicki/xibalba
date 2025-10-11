#!/bin/bash
# Wrapper that runs host validation test, then VM tests
# This is a Bazel test that ensures prerequisites are met

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "═══════════════════════════════════════════════════════════════"
echo "  Xibalba VM Testing with Validation"
echo "═══════════════════════════════════════════════════════════════"
echo

# Step 1: Validate host dependencies
echo "Step 1: Validating host dependencies..."
echo

# Find and run verification
VERIFY_SCRIPT=""
for loc in "$SCRIPT_DIR/verify_host_deps.sh" "$(pwd)/vm/verify_host_deps.sh"; do
    if [ -f "$loc" ]; then
        VERIFY_SCRIPT="$loc"
        break
    fi
done

if [ -z "$VERIFY_SCRIPT" ]; then
    echo "❌ Error: verify_host_deps.sh not found"
    exit 1
fi

if ! "$VERIFY_SCRIPT"; then
    echo
    echo "❌ Host dependencies not met. Cannot proceed."
    echo "Install missing packages as shown above."
    exit 1
fi

echo
echo "✅ Host validation passed"
echo

# Step 2: Run VM tests
echo "Step 2: Running parallel VM tests..."
echo

# Find the actual parallel test wrapper
WRAPPER=""
for loc in "$SCRIPT_DIR/run-parallel-vm-tests-wrapper.sh" "$(pwd)/vm/run-parallel-vm-tests-wrapper.sh"; do
    if [ -f "$loc" ]; then
        WRAPPER="$loc"
        break
    fi
done

if [ -z "$WRAPPER" ]; then
    echo "❌ Error: run-parallel-vm-tests-wrapper.sh not found"
    exit 1
fi

# Execute the actual tests
exec "$WRAPPER" "$@"

