#!/bin/bash
# Wrapper for running xibalba QEMU tests via Bazel test rules
# Captures output and determines pass/fail

set -euo pipefail

# Parse all arguments (passed from Bazel, already in --flag format)
# We just pass them through to the runner

# Find the qemu runner in runfiles
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Try multiple locations
if [ -n "${RUNFILES_DIR:-}" ]; then
    RUNNER="$RUNFILES_DIR/_main/vm/qemu_test_runner"
elif [ -f "$SCRIPT_DIR/run-qemu-test.sh" ]; then
    RUNNER="$SCRIPT_DIR/run-qemu-test.sh"
elif [ -f "$SCRIPT_DIR/../qemu_test_runner" ]; then
    RUNNER="$SCRIPT_DIR/../qemu_test_runner"
else
    echo "ERROR: Could not find qemu_test_runner"
    echo "SCRIPT_DIR=$SCRIPT_DIR"
    echo "RUNFILES_DIR=${RUNFILES_DIR:-not set}"
    ls -la "$SCRIPT_DIR/" || true
    exit 1
fi

echo "Using runner: $RUNNER"
echo "Test args: $*"

# Run test and capture output (pass all args through)
OUTPUT=$(mktemp)
if "$RUNNER" "$@" > "$OUTPUT" 2>&1; then
    VM_EXIT=$?
else
    VM_EXIT=$?
fi

# Check if test completed
if grep -q "XIBALBA_TEST_COMPLETE" "$OUTPUT"; then
    TEST_EXIT=$(grep "^EXIT_CODE=" "$OUTPUT" | cut -d= -f2)
    FILESYSTEM=$(grep "^FILESYSTEM=" "$OUTPUT" | cut -d= -f2)
    
    echo "=== Xibalba Fast VM Test: ${FILESYSTEM:-unknown} ==="
    echo ""
    
    # Show test results
    grep -A20 "=== Results ===" "$OUTPUT" || true
    
    echo ""
    echo "[DEBUG] TEST_EXIT='$TEST_EXIT' (length: ${#TEST_EXIT})"
    
    # Strip any whitespace
    TEST_EXIT=$(echo "$TEST_EXIT" | tr -d '[:space:]')
    echo "[DEBUG] After strip: TEST_EXIT='$TEST_EXIT'"
    
    if [ "$TEST_EXIT" = "0" ]; then
        echo ""
        echo "✅ TEST PASSED: No bugs detected"
        cat "$OUTPUT" > "${TEST_LOG:-/dev/null}"  # Save full log if TEST_LOG is set
        rm -f "$OUTPUT"
        exit 0
    else
        echo ""
        echo "❌ TEST FAILED: Bugs detected or test error (exit code: $TEST_EXIT)"
        echo ""
        echo "Full output:"
        cat "$OUTPUT"
        rm -f "$OUTPUT"
        exit 1
    fi
else
    echo "❌ ERROR: VM failed to complete test"
    echo ""
    echo "Full output:"
    cat "$OUTPUT"
    rm -f "$OUTPUT"
    exit 1
fi

