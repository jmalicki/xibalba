#!/bin/bash
# Grant eBPF capabilities to pause_controller
# Run: sudo ./grant_caps.sh

set -euo pipefail

if [ "$EUID" -ne 0 ]; then
    echo "Please run: sudo ./grant_caps.sh"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$SCRIPT_DIR/bin"

if [ ! -f "$BIN_DIR/pause_controller" ]; then
    echo "ERROR: $BIN_DIR/pause_controller not found"
    echo "Run ./compile.sh first to build binaries"
    exit 1
fi

setcap cap_bpf,cap_perfmon,cap_net_admin=ep "$BIN_DIR/pause_controller"
echo "✅ Granted eBPF capabilities to $BIN_DIR/pause_controller"
getcap "$BIN_DIR/pause_controller"

