# Xibalba VM Testing Setup Guide

Complete guide for setting up and running Xibalba's VM-based chaos tests.

## Overview

Xibalba runs filesystem chaos tests inside isolated VMs using:
- **QEMU/KVM** for hardware-accelerated virtualization
- **libvirt** for VM management
- **cloud-init** for automated VM configuration
- **Bazel** for orchestration and dependency validation

---

## Prerequisites

### System Requirements

- Linux host with KVM support (check: `egrep -c '(vmx|svm)' /proc/cpuinfo` should be > 0)
- 8GB+ RAM (16GB+ recommended for parallel testing)
- 50GB+ disk space (for VM images)
- Ubuntu 22.04 or later (other distros may work)

### Enable Virtualization in BIOS

Before proceeding, ensure virtualization is enabled in your BIOS:
- **AMD**: Look for "AMD-V" or "SVM Mode" → Enable
- **Intel**: Look for "VT-x" or "Intel Virtualization" → Enable

Reboot after changing BIOS settings.

---

## Complete One-Time Setup

Run this entire script for full setup:

```bash
#!/bin/bash
# Complete Xibalba VM setup

set -e

echo "=== Xibalba VM Setup ==="
echo

# 1. Install system packages
echo "Installing system packages..."
sudo apt install -y \
    libvirt-daemon-system \
    qemu-kvm \
    virtinst \
    cloud-image-utils

# 2. Grant user permissions
echo "Adding $USER to libvirt and kvm groups..."
sudo usermod -aG libvirt,kvm $USER

# 3. Enable KVM module
echo "Enabling KVM module..."
# Detect CPU vendor
if grep -q "AMD" /proc/cpuinfo; then
    KVM_MODULE="kvm-amd"
elif grep -q "Intel" /proc/cpuinfo; then
    KVM_MODULE="kvm-intel"
else
    echo "WARNING: Could not detect CPU vendor, defaulting to kvm-amd"
    KVM_MODULE="kvm-amd"
fi

echo "$KVM_MODULE" | sudo tee /etc/modules-load.d/kvm.conf
sudo modprobe "$KVM_MODULE" 2>/dev/null || echo "KVM module already loaded"

# 4. Fix Bazel cache permissions
echo "Setting Bazel cache permissions..."
sudo chmod o+x /home/$USER/.cache

# 5. Create libvirt user session network
echo "Creating libvirt network..."
virsh --connect qemu:///session net-define /dev/stdin <<'EOF'
<network>
  <name>default</name>
  <forward mode='nat'/>
  <bridge name='virbr1' stp='on' delay='0'/>
  <ip address='192.168.124.1' netmask='255.255.255.0'>
    <dhcp>
      <range start='192.168.124.2' end='192.168.124.254'/>
    </dhcp>
  </ip>
</network>
EOF

# 6. Start network (requires sudo for bridge creation)
echo "Starting network (requires sudo for bridge)..."
sudo virsh --connect qemu:///session net-start default

# 7. Set autostart
virsh --connect qemu:///session net-autostart default

echo
echo "✅ Setup complete!"
echo
echo "⚠️  IMPORTANT: Log out and back in for group changes to take effect!"
echo
echo "After logging back in, verify with:"
echo "  cd /path/to/xibalba"
echo "  bazel test //vm:verify_host_deps"
```

Save this as `setup_vm.sh`, make it executable, and run it:

```bash
chmod +x setup_vm.sh
./setup_vm.sh
```

**After running**: Log out and back in for group changes to take effect!

---

## Verification

After logging back in:

```bash
cd /path/to/xibalba

# Verify all prerequisites
bazel test //vm:verify_host_deps
```

Expected output:
```
✓ virsh
✓ virt-install
✓ cloud-localds
✓ qemu-system-x86_64
✓ libvirt default network (active)

✅ All host dependencies satisfied
```

If it fails, the test will show exactly what's missing and how to fix it.

---

## Running Tests

### Quick Smoke Test (~2 minutes)

Fast validation that everything works:

```bash
bazel test //vm:vm_smoke_test
```

This creates a single VM, installs the package, and runs a 30-second test on tmpfs.

### Full Progressive Gauntlet (~15 minutes)

Comprehensive testing on ext4 and ZFS with 3 consistency models:

```bash
bazel test //vm:parallel_vm_tests_validated
```

This runs:
- 2 VMs in parallel (ext4 and ZFS)
- 3 consistency models each (EVENTUAL → WEAK → STRICT)
- 300 seconds per model
- Results saved to `test-results/parallel-vm-tests-{timestamp}/`

---

## Network Configuration Details

### Why User Session Networking?

Xibalba uses `qemu:///session` (user-level VMs) instead of `qemu:///system` (system-level) for:
- ✅ No sudo needed for VM operations
- ✅ Better isolation (user-specific)
- ✅ Easier CI integration
- ✅ Consistent with Bazel's user-space philosophy

### Network Setup Explanation

The user session requires explicit network configuration:

1. **Define network**: Create NAT network with DHCP
   ```bash
   virsh --connect qemu:///session net-define ...
   ```

2. **Start network**: Requires sudo (one-time) for bridge creation
   ```bash
   sudo virsh --connect qemu:///session net-start default
   ```

3. **Autostart**: Network starts automatically on boot
   ```bash
   virsh --connect qemu:///session net-autostart default
   ```

**Why sudo for net-start?** Creating network bridges requires elevated permissions, but this is only needed once. After that, the network persists and VMs can use it without sudo.

### Network Details

- **Name**: `default`
- **Type**: NAT (VMs can reach internet, isolated from host network)
- **Bridge**: `virbr1` (doesn't conflict with system libvirt's `virbr0`)
- **Subnet**: `192.168.124.0/24`
- **DHCP Range**: `192.168.124.2` - `192.168.124.254`
- **Gateway**: `192.168.124.1`

---

## Troubleshooting

### Network Issues

**Error**: `Network not found: default`

**Check**:
```bash
virsh --connect qemu:///session net-list --all
```

**Fix**: Create the network (see setup script above)

---

**Error**: `Operation not permitted` when starting network

**Cause**: Bridge creation requires elevated permissions

**Fix**:
```bash
sudo virsh --connect qemu:///session net-start default
```

---

**Error**: Network exists but not started

**Fix**:
```bash
sudo virsh --connect qemu:///session net-start default
```

### KVM Issues

**Error**: `KVM acceleration not available`

**Check**:
```bash
# Should exist and be accessible
ls -l /dev/kvm

# Should show kvm_amd or kvm_intel
lsmod | grep kvm

# Check CPU supports virtualization
egrep -c '(vmx|svm)' /proc/cpuinfo  # Should be > 0
```

**Fix**:
1. Enable in BIOS (see Prerequisites)
2. Load KVM module:
   ```bash
   # AMD
   sudo modprobe kvm-amd
   
   # Intel
   sudo modprobe kvm-intel
   ```

### Permission Issues

**Error**: `Permission denied` on virsh commands

**Check groups**:
```bash
groups | grep -E 'libvirt|kvm'
```

**If empty**: You didn't log out and back in after adding groups!

**Fix**: Log out and back in, or force group refresh:
```bash
newgrp libvirt
```

---

**Error**: VM images inaccessible

**Check Bazel cache permissions**:
```bash
ls -ld /home/$USER/.cache
# Should end with 'x': drwx-----x
```

**Fix**:
```bash
sudo chmod o+x /home/$USER/.cache
```

### Test Failures

**VM fails to boot**:
- Check network is active: `virsh --connect qemu:///session net-list`
- Check KVM is available: `ls -l /dev/kvm`
- Check logs: See `test-results/` directory

**Slow performance**:
- Ensure KVM is enabled (not emulation)
- Check: `grep -E '(vmx|svm)' /proc/cpuinfo`
- Verify `/dev/kvm` exists

---

## Advanced Configuration

### Custom Test Parameters

Smoke test defaults:
```bash
# Override via environment variables
TEST_DURATION=60 \
TEST_READERS=5 \
TEST_WRITERS=2 \
FILESYSTEM=ext4 \
bazel test //vm:vm_smoke_test
```

### Cleanup Old VMs

```bash
# List all VMs
virsh --connect qemu:///session list --all

# Destroy and remove
virsh --connect qemu:///session destroy xibalba-smoke
virsh --connect qemu:///session undefine xibalba-smoke

# Clean up disk images
rm -f vm/images/xibalba-*.qcow2
```

### Manual VM Testing

```bash
# Create VM
vm/create_test_vm.sh --name test-vm --ram 2048 --disk 10

# Deploy package
vm/deploy_xibalba.sh test-vm bazel-bin/packaging/xibalba_0.1.0_amd64.deb

# SSH into VM
ssh root@test-vm

# Destroy VM
vm/destroy_vm.sh test-vm
```

---

## Performance Expectations

### With KVM Acceleration

- Smoke test: **~2 minutes**
- Full gauntlet: **~15 minutes** (2 VMs parallel, 3 models each)
- Per-model test: **~5 minutes** (300 seconds + setup/teardown)

### Without KVM (Emulation)

- Smoke test: **~10 minutes**
- Full gauntlet: **~3-6 hours**
- Not recommended for regular testing

**Always use KVM for sane test times!**

---

## What's Installed in VMs

When the Xibalba package is deployed to VMs, it includes:

**Core Tools**:
- `pause_controller` - eBPF userspace controller
- `simple_chaos_test` - Multi-threaded chaos test
- `pause_injector.bpf.o` - eBPF bytecode

**Test Runners**:
- `xibalba-test-runner` - Single filesystem test
- `xibalba-gauntlet` - Progressive multi-model testing
- `xibalba-smoke-test` - Quick validation

**Filesystem Tools** (auto-installed via package dependencies):
- `e2fsprogs` (ext4), `xfsprogs` (XFS), `btrfs-progs` (btrfs)
- `zfsutils-linux` (ZFS), `f2fs-tools` (F2FS)
- `nilfs-tools` (NILFS2), `nfs-common` (NFS)
- `exfatprogs` (exFAT), `ntfs-3g` (NTFS)

---

## CI Integration

For GitHub Actions or other CI:

```yaml
- name: Install VM dependencies
  run: |
    sudo apt-get update
    sudo apt-get install -y libvirt-daemon-system qemu-kvm virtinst cloud-image-utils
    
- name: Setup VM environment
  run: |
    sudo usermod -aG libvirt,kvm $USER
    # CI runs don't need group login, commands run as root
    
- name: Run smoke test
  run: bazel test //vm:vm_smoke_test
```

Note: Full gauntlet may be too slow for CI. Use smoke test for PR validation.

---

## Summary

**One-time setup**:
1. Run setup script
2. Log out and back in
3. Verify with `bazel test //vm:verify_host_deps`

**Regular testing**:
- Quick: `bazel test //vm:vm_smoke_test` (~2 min)
- Full: `bazel test //vm:parallel_vm_tests_validated` (~15 min)

**Results**: Check `test-results/` directory for detailed JSON output and logs.

For more details, see:
- [`VM-PERMISSIONS.md`](VM-PERMISSIONS.md) - Group and permission details
- [`GAUNTLET-TESTING.md`](GAUNTLET-TESTING.md) - Progressive testing strategy
- [`VM-FILESYSTEM-SETUP.md`](VM-FILESYSTEM-SETUP.md) - Filesystem configuration
