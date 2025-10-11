#!/bin/bash
# Grant eBPF capabilities to pause_controller
# 
# Usage:
#   1. Build with Bazel: bazel build //chaos:pause_controller
#   2. Grant capabilities: sudo ./grant_caps.sh
#   3. Run: bazel run //chaos:pause_controller -- 50 11

set -euo pipefail

if [ "$EUID" -ne 0 ]; then
    echo "ERROR: Must run as root"
    echo "Usage: sudo ./grant_caps.sh"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY="$SCRIPT_DIR/bazel-bin/chaos/pause_controller"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: pause_controller not found at $BINARY"
    echo ""
    echo "Build it first:"
    echo "  bazel build //chaos:pause_controller"
    exit 1
fi

echo "=== Granting eBPF Capabilities ==="
echo ""
echo "Target: $BINARY"
echo ""
echo "Capabilities:"
echo "  • CAP_BPF       : Load eBPF programs"
echo "  • CAP_PERFMON   : Attach to tracepoints/kprobes"  
echo "  • CAP_NET_ADMIN : Network-related BPF operations"
echo "  • CAP_SYS_ADMIN : Kprobe attachment"
echo ""

setcap cap_bpf,cap_perfmon,cap_net_admin,cap_sys_admin=ep "$BINARY"

echo "✅ Capabilities granted successfully"
echo ""
getcap "$BINARY"
echo ""
echo "Now you can run without sudo:"
echo "  bazel run //chaos:pause_controller -- 50 11"
echo ""

