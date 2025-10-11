# VM Testing Setup Guide

This guide covers setting up your system for Xibalba VM-based chaos testing.

## Prerequisites

### Required (Must Install)

#### 1. KVM Virtualization
```bash
# Check if KVM is available
ls -la /dev/kvm

# If missing, enable virtualization in BIOS/UEFI
# Then install KVM
sudo apt install qemu-kvm
```

**Verify**: `/dev/kvm` should exist with read/write permissions for your user (via groups).

#### 2. libvirt and Tools
```bash
# Install libvirt and related tools
sudo apt install -y \
    libvirt-daemon-system \
    libvirt-clients \
    virtinst \
    qemu-system-x86 \
    cloud-image-utils
```

**What each does**:
- `libvirt-daemon-system` - VM management daemon
- `libvirt-clients` - `virsh` command-line tool
- `virtinst` - `virt-install` for creating VMs
- `qemu-system-x86` - QEMU emulator/hypervisor
- `cloud-image-utils` - `cloud-localds` for cloud-init ISOs

#### 3. User Groups
```bash
# Add yourself to libvirt and kvm groups
sudo usermod -aG libvirt,kvm $USER

# IMPORTANT: Log out and back in for group changes to take effect!
# Or use: newgrp libvirt

# Verify after re-login
groups | grep libvirt
groups | grep kvm
```

**Why**: Without group membership, you'll get permission errors when accessing VMs.

#### 4. libvirt Default Network
```bash
# Check network status
virsh --connect qemu:///system net-list

# If 'default' network not active, start it
sudo virsh --connect qemu:///system net-start default

# Make it auto-start on boot
sudo virsh --connect qemu:///system net-autostart default
```

**Verify**: `virsh net-list` should show `default` network as `active`.

## Optional but Highly Recommended

### libnss-libvirt (VM Hostname Resolution)

**Impact**: Without this, VM tests take **5-10 minutes** to connect via SSH due to slow IP detection.  
**With this**: Tests connect in **~2 seconds** using hostnames instead of IPs.

```bash
# Install
sudo apt install libnss-libvirt

# Verify installation
ldconfig -p | grep libnss_libvirt

# Should see:
# libnss_libvirt.so.2 (libc6,x86-64) => /usr/lib/x86_64-linux-gnu/libnss_libvirt.so.2
```

**How it works**: Adds `libvirt` to NSS resolution chain in `/etc/nsswitch.conf`, allowing commands like:
```bash
ssh root@ext4_gauntlet  # Instead of ssh root@192.168.122.123
ping zfs_gauntlet       # Hostname resolution just works
```

**Before**: Script waits up to 10 minutes polling `virsh domifaddr` for IP  
**After**: Hostname resolves immediately via libvirt NSS

## Verification

Run Bazel's automated verification:
```bash
bazel run //vm:verify_host_deps
```

This checks:
- ✓ Required tools installed
- ✓ User in correct groups
- ✓ libvirt network active
- ⚠️ libnss-libvirt (warns if missing but doesn't fail)

**Expected output**:
```
✓ virsh
✓ virt-install
✓ cloud-localds
✓ qemu-system-x86_64
✓ User in libvirt group
✓ libvirt default network (active)

✅ All host dependencies satisfied
```

## Troubleshooting

### Permission Denied Errors
```
ERROR: ... Permission denied
```
**Fix**: Ensure you're in `libvirt` and `kvm` groups and have **logged out/in** after adding them.

### Network Not Active
```
ERROR: network 'default' is not active
```
**Fix**: 
```bash
sudo virsh net-start default
sudo virsh net-autostart default
```

### KVM Not Available
```
ERROR: /dev/kvm not found
```
**Fix**: 
1. Enable virtualization in BIOS/UEFI (look for VT-x/AMD-V)
2. Install: `sudo apt install qemu-kvm`
3. Check: `ls -la /dev/kvm`

### Slow VM Tests (5-10 minutes to connect)
**Symptom**: Tests wait a long time at "Waiting for VM network and SSH"  
**Cause**: IP detection via `virsh domifaddr` is slow  
**Fix**: Install `libnss-libvirt` (see above)

## Complete Setup Script

```bash
#!/bin/bash
# Complete Xibalba VM testing setup

echo "Installing required packages..."
sudo apt update
sudo apt install -y \
    libvirt-daemon-system \
    libvirt-clients \
    virtinst \
    qemu-kvm \
    qemu-system-x86 \
    cloud-image-utils \
    libnss-libvirt

echo "Adding user to groups..."
sudo usermod -aG libvirt,kvm $USER

echo "Configuring libvirt network..."
sudo virsh net-start default 2>/dev/null || true
sudo virsh net-autostart default

echo "✓ Setup complete!"
echo
echo "IMPORTANT: Log out and back in for group changes to take effect"
echo
echo "After re-login, verify with:"
echo "  bazel run //vm:verify_host_deps"
```

## Architecture Notes

### Why Bazel Can't Install These

Bazel provides **hermetic builds** - isolated from system state. System dependencies like libvirt require:
- Root permissions
- System service management (systemd)
- Kernel module interaction (KVM)
- Global config file modifications (NSS)

These are fundamentally **non-hermetic** and must be set up at the system level.

**Bazel's role**: Verify they exist, provide clear setup instructions if missing.

### See Also
- `VM-TESTING-STATUS.md` - Current testing status and architecture
- `README.md` - Quick start guide
- `vm/verify_host_deps.sh` - Automated dependency checker
