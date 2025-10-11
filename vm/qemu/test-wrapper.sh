#!/bin/bash
# Wrapper for running xibalba QEMU tests via Bazel test rules
# Captures output and determines pass/fail

set -euo pipefail

FILESYSTEM=$1
DURATION=${2:-10}
READERS=${3:-3}
WRITERS=${4:-2}

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

# Run test and capture output
OUTPUT=$(mktemp)
if "$RUNNER" "$FILESYSTEM" "$DURATION" "$READERS" "$WRITERS" > "$OUTPUT" 2>&1; then
    VM_EXIT=$?
else
    VM_EXIT=$?
fi

# Check if test completed
if grep -q "XIBALBA_TEST_COMPLETE" "$OUTPUT"; then
    TEST_EXIT=$(grep "^EXIT_CODE=" "$OUTPUT" | cut -d= -f2)
    
    echo "=== Xibalba Fast VM Test: $FILESYSTEM ==="
    echo ""
    
    # Show test results
    grep -A20 "=== Results ===" "$OUTPUT" || true
    
    if [ "$TEST_EXIT" -eq 0 ]; then
        echo ""
        echo "✅ TEST PASSED: No bugs detected"
        cat "$OUTPUT" > "${TEST_LOG:-/dev/null}"  # Save full log if TEST_LOG is set
        rm -f "$OUTPUT"
        exit 0
    else
        echo ""
        echo "❌ TEST FAILED: Bugs detected or test error"
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

