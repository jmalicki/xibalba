#!/bin/bash
# Grant eBPF capabilities to pause_controller
# Run: sudo ./grant_caps.sh

set -euo pipefail

if [ "$EUID" -ne 0 ]; then
    echo "Please run: sudo ./grant_caps.sh"
    exit 1
fi

setcap cap_bpf,cap_perfmon,cap_net_admin=ep /tmp/pause_controller
echo "✅ Granted eBPF capabilities to /tmp/pause_controller"
getcap /tmp/pause_controller

