#!/bin/bash
# Simplest possible eBPF test
# Shows exactly what commands to run

echo "=== RUDRA eBPF Test - Manual Steps ==="
echo ""
echo "Step 1: Grant capabilities to pause_controller"
echo "Run this command:"
echo ""
echo "  sudo setcap cap_bpf,cap_perfmon,cap_net_admin=ep /tmp/pause_controller"
echo ""
read -p "Press Enter after you've run the command above..."

# Verify
if getcap /tmp/pause_controller | grep -q cap_bpf; then
    echo "  ✅ Capabilities verified!"
else
    echo "  ❌ Capabilities not found"
    echo "  Please run the setcap command above"
    exit 1
fi

echo ""
echo "Step 2: Run pause controller in one terminal"
echo "Open a new terminal and run:"
echo ""
echo "  cd /tmp && ./pause_controller 20 5000"
echo ""
read -p "Press Enter after controller is running (shows 'active!')..."

# Check if eBPF loaded
if bpftool prog list 2>/dev/null | grep -q "getdents"; then
    echo "  ✅ eBPF program loaded!"
else
    echo "  ⚠️  eBPF not detected (this is OK if controller just started)"
fi

echo ""
echo "Step 3: Run chaos test"
echo ""
/tmp/simple_chaos_test /tmp/rudra_test

echo ""
echo "Check the pause controller terminal - did you see pause messages?"
echo ""
read -p "Did you see pause events? (y/n): " response

if [[ "$response" == "y" || "$response" == "Y" ]]; then
    echo ""
    echo "🎉 SUCCESS! eBPF pause injection works!"
    echo ""
    echo "You've validated:"
    echo "  ✅ eBPF loads and attaches"
    echo "  ✅ Pauses are injected"
    echo "  ✅ Core technology works!"
    echo ""
    echo "Next: Build race detector to find actual bugs"
else
    echo ""
    echo "Hmm, let's debug..."
    echo ""
    echo "Check these:"
    echo "  1. Did pause_controller show 'eBPF program loaded'?"
    echo "  2. Run: bpftool prog list | grep getdents"
    echo "  3. Check: sudo cat /sys/kernel/debug/tracing/trace_pipe"
fi


