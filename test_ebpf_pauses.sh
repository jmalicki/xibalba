#!/bin/bash
# Quick test script for eBPF pause injection
#
# This script tests if eBPF pause injection works
# Run this to validate Phase 3 of tech de-risking

set -e

echo "=== RUDRA eBPF Pause Injection Test ==="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "ERROR: This script must run as root (for eBPF loading)"
    echo "Please run: sudo ./test_ebpf_pauses.sh"
    exit 1
fi

# Compile if needed
echo "Step 1: Compiling..."
cd "$(dirname "$0")"

if [ ! -f /tmp/simple_chaos_test ]; then
    gcc -o /tmp/simple_chaos_test chaos/simple_chaos_test.c common/dir_reader.c -I. -pthread
    echo "  ✓ simple_chaos_test compiled"
fi

if [ ! -f /tmp/pause_injector.bpf.o ]; then
    clang -g -O2 -target bpf -D__TARGET_ARCH_x86_64 \
        -I/usr/include/x86_64-linux-gnu \
        -c chaos/pause_injector.bpf.c \
        -o /tmp/pause_injector.bpf.o
    echo "  ✓ pause_injector.bpf.o compiled"
fi

if [ ! -f /tmp/pause_controller ]; then
    gcc -o /tmp/pause_controller chaos/pause_controller.c -lbpf -lelf
    echo "  ✓ pause_controller compiled"
fi

# Create test directory
echo ""
echo "Step 2: Creating test directory..."
mkdir -p /tmp/rudra_test
cd /tmp/rudra_test
for i in {1..100}; do touch file$i 2>/dev/null || true; done
echo "  ✓ Created 100 test files"

# Run baseline test (no eBPF)
echo ""
echo "Step 3: Baseline test (WITHOUT eBPF pauses)..."
/tmp/simple_chaos_test /tmp/rudra_test | tee /tmp/baseline_result.txt
BASELINE_OPS=$(grep "Operations/second:" /tmp/baseline_result.txt | awk '{print $2}')
echo "  Baseline: $BASELINE_OPS ops/sec"

# Start pause controller in background
echo ""
echo "Step 4: Starting eBPF pause controller..."
echo "  Pause probability: 20%"
echo "  Pause duration: 5ms"
cd /tmp
./pause_controller 20 5000 > /tmp/pause_controller.log 2>&1 &
CONTROLLER_PID=$!
echo "  ✓ Controller started (PID: $CONTROLLER_PID)"
sleep 2  # Give it time to attach

# Check if eBPF loaded
echo ""
echo "Step 5: Verifying eBPF loaded..."
if bpftool prog list | grep -q getdents; then
    echo "  ✓ eBPF program loaded and attached"
else
    echo "  ✗ eBPF program not found"
    echo "  Check /tmp/pause_controller.log for errors"
    kill $CONTROLLER_PID 2>/dev/null
    exit 1
fi

# Run test WITH eBPF pauses
echo ""
echo "Step 6: Testing WITH eBPF pauses..."
/tmp/simple_chaos_test /tmp/rudra_test | tee /tmp/ebpf_result.txt
EBPF_OPS=$(grep "Operations/second:" /tmp/ebpf_result.txt | awk '{print $2}')
echo "  With eBPF: $EBPF_OPS ops/sec"

# Stop controller
kill $CONTROLLER_PID 2>/dev/null || true
sleep 1

# Analyze results
echo ""
echo "=== Analysis ==="
echo "Baseline (no eBPF):  $BASELINE_OPS ops/sec"
echo "With eBPF (20%):     $EBPF_OPS ops/sec"

# Check pause controller log
echo ""
echo "Pause controller log:"
head -20 /tmp/pause_controller.log

PAUSES=$(grep -c "Paused pid=" /tmp/pause_controller.log 2>/dev/null || echo "0")
echo ""
echo "Total pauses injected: $PAUSES"

if [ "$PAUSES" -gt 10 ]; then
    echo ""
    echo "✅ SUCCESS: eBPF pause injection is working!"
    echo "   Pauses were injected $PAUSES times"
    echo ""
    echo "Next: Build race detector to prove pauses find bugs"
else
    echo ""
    echo "⚠️  WARNING: Few or no pauses detected"
    echo "   Check /tmp/pause_controller.log for errors"
    echo "   May need to debug eBPF loading"
fi

echo ""
echo "=== Test Complete ==="

