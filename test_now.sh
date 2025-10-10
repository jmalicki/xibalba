#!/bin/bash
# Quick eBPF test using /tmp binaries
#
# Run after: sudo ./grant_caps.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$SCRIPT_DIR/bin"

echo "=== RUDRA eBPF Test (Quick Version) ==="
echo ""

# Check if binaries exist
if [ ! -f "$BIN_DIR/pause_controller" ]; then
    echo "❌ ERROR: Binaries not found in $BIN_DIR"
    echo ""
    echo "Please run first:"
    echo "  ./compile.sh"
    exit 1
fi

# Check if capabilities granted
if ! getcap "$BIN_DIR/pause_controller" 2>/dev/null | grep -q cap_bpf; then
    echo "❌ ERROR: pause_controller doesn't have capabilities"
    echo ""
    echo "Please run first:"
    echo "  sudo ./grant_caps.sh"
    exit 1
fi

echo "✓ Capabilities verified"
echo ""

# Create test data
mkdir -p /tmp/rudra_test
cd /tmp/rudra_test
for i in {1..100}; do touch file$i 2>/dev/null || true; done
echo "✓ Test directory ready: /tmp/rudra_test (100 files)"
echo ""

# Baseline test
echo "=== Test 1: Baseline (no eBPF) ==="
"$BIN_DIR/simple_chaos_test" /tmp/rudra_test | tee /tmp/baseline.txt
BASELINE=$(grep "Operations/second:" /tmp/baseline.txt | awk '{print $2}')
echo ""

# Test with eBPF
echo "=== Test 2: With eBPF (20% pauses, 5ms each) ==="
echo "Starting pause controller..."

cd "$BIN_DIR" || exit 1
./pause_controller 20 5000 > /tmp/pause_log.txt 2>&1 &
PID=$!
echo "  Pause controller PID: $PID"
sleep 2

# Check if eBPF loaded
if bpftool prog list 2>/dev/null | grep -q "getdents"; then
    echo "  ✓ eBPF program loaded!"
else
    echo "  ✗ eBPF not loaded - check /tmp/pause_log.txt"
    kill $PID 2>/dev/null || true
    cat /tmp/pause_log.txt
    exit 1
fi

# Run test
"$BIN_DIR/simple_chaos_test" /tmp/rudra_test | tee /tmp/ebpf_test.txt
EBPF=$(grep "Operations/second:" /tmp/ebpf_test.txt | awk '{print $2}')

# Stop controller
kill $PID 2>/dev/null || true
sleep 1

# Results
echo ""
echo "=== Results ==="
echo "Baseline (no eBPF):  $BASELINE ops/sec"
echo "With eBPF (20%):     $EBPF ops/sec"
echo ""

PAUSES=$(grep -c "Paused pid=" /tmp/pause_log.txt 2>/dev/null || echo "0")
echo "Pauses injected: $PAUSES"
echo ""

if [ "$PAUSES" -gt 10 ]; then
    echo "✅ SUCCESS! eBPF pause injection works!"
    echo ""
    echo "Evidence:"
    echo "  - eBPF program loaded and attached"
    echo "  - $PAUSES pauses were injected"
    
    # Calculate slowdown
    if [ -n "$BASELINE" ] && [ -n "$EBPF" ]; then
        SLOWDOWN=$(echo "scale=1; 100 * (1 - $EBPF / $BASELINE)" | bc)
        echo "  - Performance impact: ${SLOWDOWN}% slower (expected with pauses)"
    fi
    
    echo ""
    echo "🎉 Tech de-risking Phase 0-3 COMPLETE!"
    echo ""
    echo "Next: Build race detector to prove pauses find bugs"
    exit 0
else
    echo "⚠️  WARNING: Only $PAUSES pauses detected"
    echo ""
    echo "Controller log (first 20 lines):"
    head -20 /tmp/pause_log.txt
    exit 1
fi

