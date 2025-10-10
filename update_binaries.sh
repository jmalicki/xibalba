#!/bin/bash
# Update RUDRA binaries after code changes
#
# Run this after modifying .c or .bpf.c files
# Requires sudo (but uses updated setup script that fixes ownership)

set -euo pipefail

echo "=== Updating RUDRA Binaries ==="
echo ""
echo "This will recompile all binaries with latest code changes"
echo ""

# Just re-run the setup script
exec sudo ./setup_ebpf_permissions.sh

