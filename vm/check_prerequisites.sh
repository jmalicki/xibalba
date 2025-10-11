#!/bin/bash
set -euo pipefail

# Check if host system has required tools for VM testing

echo "=== Checking RUDRA VM Prerequisites ==="
echo

MISSING=()
WARNINGS=()

# Check for QEMU/KVM
echo -n "Checking QEMU/KVM... "
if command -v qemu-system-x86_64 &> /dev/null; then
    echo "✓ $(qemu-system-x86_64 --version | head -1)"
else
    echo "✗ NOT FOUND"
    MISSING+=("qemu-system-x86")
fi

# Check KVM module
echo -n "Checking KVM support... "
if lsmod | grep -q kvm; then
    echo "✓ KVM module loaded"
else
    echo "⚠ KVM module not loaded"
    WARNINGS+=("KVM not loaded (VMs will be slow). Load with: sudo modprobe kvm-intel (or kvm-amd)")
fi

# Check for libvirt
echo -n "Checking libvirt... "
if command -v virsh &> /dev/null; then
    echo "✓ $(virsh --version)"
else
    echo "✗ NOT FOUND"
    MISSING+=("libvirt-daemon-system libvirt-clients")
fi

# Check for virt-install
echo -n "Checking virt-install... "
if command -v virt-install &> /dev/null; then
    echo "✓ Found"
else
    echo "✗ NOT FOUND"
    MISSING+=("virtinst")
fi

# Check for virt-customize (optional but useful)
echo -n "Checking virt-customize... "
if command -v virt-customize &> /dev/null; then
    echo "✓ Found"
else
    echo "⚠ NOT FOUND (optional)"
    WARNINGS+=("virt-customize not found. Install with: sudo apt install libguestfs-tools")
fi

# Check for cloud-localds (for cloud-init)
echo -n "Checking cloud-localds... "
if command -v cloud-localds &> /dev/null; then
    echo "✓ Found"
else
    echo "✗ NOT FOUND"
    MISSING+=("cloud-image-utils")
fi

# Check disk space
echo -n "Checking disk space... "
AVAIL_GB=$(df -BG . | tail -1 | awk '{print $4}' | sed 's/G//')
if [ "$AVAIL_GB" -gt 50 ]; then
    echo "✓ ${AVAIL_GB}GB available"
else
    echo "⚠ Only ${AVAIL_GB}GB available"
    WARNINGS+=("Low disk space. Need ~50GB for VM matrix. Have: ${AVAIL_GB}GB")
fi

# Check RAM
echo -n "Checking available RAM... "
AVAIL_RAM_MB=$(free -m | awk 'NR==2{print $7}')
AVAIL_RAM_GB=$((AVAIL_RAM_MB / 1024))
if [ "$AVAIL_RAM_MB" -gt 8192 ]; then
    echo "✓ ${AVAIL_RAM_GB}GB available"
else
    echo "⚠ Only ${AVAIL_RAM_GB}GB available"
    WARNINGS+=("Low RAM. Need ~8GB for VM matrix. Have: ${AVAIL_RAM_GB}GB")
fi

# Check if user is in libvirt/kvm groups
echo -n "Checking user groups... "
if groups | grep -qE 'libvirt|kvm'; then
    echo "✓ User in libvirt/kvm groups"
else
    echo "⚠ User not in libvirt/kvm groups"
    WARNINGS+=("Add user to groups: sudo usermod -aG libvirt,kvm \$USER (then logout/login)")
fi

echo
echo "=== Summary ==="
echo

if [ ${#MISSING[@]} -eq 0 ]; then
    echo "✅ All required tools are installed!"
else
    echo "❌ Missing required tools:"
    for tool in "${MISSING[@]}"; do
        echo "   - $tool"
    done
    echo
    echo "Install with:"
    echo "   sudo apt update"
    echo "   sudo apt install ${MISSING[*]}"
    echo
fi

if [ ${#WARNINGS[@]} -gt 0 ]; then
    echo "⚠️  Warnings:"
    for warning in "${WARNINGS[@]}"; do
        echo "   - $warning"
    done
    echo
fi

if [ ${#MISSING[@]} -eq 0 ]; then
    echo "✅ Ready to create VMs!"
    echo
    echo "Next steps:"
    echo "   ./create_test_vm.sh --name rudra-test-01"
    exit 0
else
    echo "❌ Please install missing tools first"
    exit 1
fi



