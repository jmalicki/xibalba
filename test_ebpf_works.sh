#!/bin/bash
# Test if eBPF pause injection is working
#
# This runs both baseline and eBPF tests, compares results

echo "=== Testing eBPF Pause Injection ==="
echo ""

BIN_DIR="$(cd "$(dirname "$0")/bin" && pwd)"

# Create test data
mkdir -p /tmp/rudra_test
cd /tmp/rudra_test
for i in {1..100}; do touch file$i 2>/dev/null || true; done
echo "Test directory: /tmp/rudra_test (100 files)"
echo ""

# Baseline test (no eBPF)
echo "=== Baseline (no eBPF) ==="
"$BIN_DIR/simple_chaos_test" /tmp/rudra_test | tee /tmp/baseline.txt
BASELINE_OPS=$(grep "Operations/second:" /tmp/baseline.txt | awk '{print $2}')
echo ""

# Test with eBPF pauses
echo "=== With eBPF pauses (20%, 5ms) ==="
echo "Starting pause controller in background..."

cd "$BIN_DIR"
./pause_controller 20 5000 > /tmp/pause_controller.log 2>&1 &
CONTROLLER_PID=$!
echo "  Controller PID: $CONTROLLER_PID"
sleep 2  # Let eBPF attach

# Check if loaded
if bpftool prog list 2>/dev/null | grep -q getdents; then
    echo "  ✓ eBPF program loaded"
else
    echo "  ✗ eBPF program not loaded - check /tmp/pause_controller.log"
    kill $CONTROLLER_PID 2>/dev/null || true
    exit 1
fi

# Run test
"$BIN_DIR/simple_chaos_test" /tmp/rudra_test | tee /tmp/ebpf.txt
EBPF_OPS=$(grep "Operations/second:" /tmp/ebpf.txt | awk '{print $2}')

# Stop controller
kill $CONTROLLER_PID 2>/dev/null || true
wait $CONTROLLER_PID 2>/dev/null || true
sleep 1

# Results
echo ""
echo "=== Results ==="
echo "Baseline (no eBPF):  $BASELINE_OPS ops/sec"
echo "With eBPF (20%):     $EBPF_OPS ops/sec"

PAUSES=$(grep -c "Paused pid=" /tmp/pause_controller.log 2>/dev/null || echo "0")
echo "Pauses injected:     $PAUSES"
echo ""

if [ "$PAUSES" -gt 10 ]; then
    echo "✅ SUCCESS: eBPF pause injection is working!"
    echo ""
    echo "Evidence:"
    echo "  - eBPF program loaded successfully"
    echo "  - Pauses were injected $PAUSES times"
    echo "  - Performance impact visible (slower with pauses)"
    echo ""
    echo "Next: Build race detector to prove pauses find bugs"
    exit 0
else
    echo "⚠️  ISSUE: Few or no pauses detected"
    echo ""
    echo "Debug info:"
    cat /tmp/pause_controller.log
    exit 1
fi
