# VM Permissions - Running Without Sudo

Xibalba VM operations (creating, starting, managing VMs) require access to libvirt and QEMU/KVM. Instead of using `sudo` for every command, you can configure your user to have the necessary permissions.

## Quick Setup (Recommended)

```bash
# Add your user to the libvirt group
sudo usermod -aG libvirt $USER

# Add your user to the kvm group (for KVM access)
sudo usermod -aG kvm $USER

# IMPORTANT: Log out and log back in for group changes to take effect
# Or run: newgrp libvirt
```

**Verify it worked:**
```bash
# Check group membership
groups | grep libvirt

# Test libvirt access (should work without sudo)
virsh list --all
```

## What These Groups Provide

### `libvirt` Group

Grants access to:
- `/var/run/libvirt/libvirt-sock` - libvirt control socket
- VM management operations (create, start, stop, destroy)
- Domain (VM) configuration
- Network management (default NAT network)

**Allows**:
- `virsh` commands without sudo
- VM creation and management
- Virtual network operations

### `kvm` Group

Grants access to:
- `/dev/kvm` - KVM kernel module device
- Hardware-accelerated virtualization

**Allows**:
- QEMU to use KVM acceleration
- VMs run at near-native speed (not required but highly recommended)

## Alternative: Session-Based Access

If you don't want to modify groups:

```bash
# Use qemu:///session URI (user-level VMs)
virsh -c qemu:///session list --all

# Set default connection
export LIBVIRT_DEFAULT_URI="qemu:///session"
```

**Limitations**:
- User-level VMs (not system-level)
- Limited networking options
- Can't access system bridges

## Xibalba-Specific Requirements

### For VM Operations

**Required**:
- `libvirt` group - VM management
- `kvm` group - Hardware acceleration

**Commands that need this**:
```bash
# All work without sudo after group membership
virsh list --all
virsh start xibalba-ext4
virsh destroy xibalba-ext4
virt-install ...
```

### For eBPF Operations (Inside VMs)

**Required Capabilities**:
- `CAP_BPF` - Load eBPF programs
- `CAP_PERFMON` - Attach to kprobes
- `CAP_NET_ADMIN` - Network eBPF programs
- `CAP_SYS_ADMIN` - Kprobe attachment
- `CAP_IPC_LOCK` - Memory locking for eBPF

**Set on binary** (inside VM):
```bash
sudo setcap cap_bpf,cap_perfmon,cap_net_admin,cap_sys_admin,cap_ipc_lock=+ep /usr/bin/pause_controller
```

This is already handled by the VM setup - no action needed!

## Complete Setup Guide

### One-Time Host Setup

```bash
# 1. Install libvirt and KVM
sudo apt install -y \
    libvirt-daemon-system \
    qemu-kvm \
    virt-manager \
    bridge-utils

# 2. Add your user to groups
sudo usermod -aG libvirt $USER
sudo usermod -aG kvm $USER

# 3. Enable and start libvirt
sudo systemctl enable --now libvirtd

# 4. Verify default network
virsh net-list --all
# Should show 'default' network (auto-created)

# 5. Log out and back in (or run: newgrp libvirt)
```

### Verify Setup

```bash
# Should work without sudo:
virsh list --all
virsh net-list

# Check KVM access
ls -l /dev/kvm
# Should show: crw-rw----+ 1 root kvm ...
#                                 ^^^ You should be in this group

# Verify your groups
groups
# Should include: ... libvirt kvm ...
```

### First VM Test

```bash
# Should now work without sudo!
bazel run //vm:parallel_vm_tests
```

## Troubleshooting

### "Failed to connect to libvirt"

```bash
# Check if libvirtd is running
systemctl status libvirtd

# Start it if needed
sudo systemctl start libvirtd

# Enable on boot
sudo systemctl enable libvirtd
```

### "Permission denied" for /dev/kvm

```bash
# Check KVM group membership
groups | grep kvm

# If not in kvm group, add and re-login
sudo usermod -aG kvm $USER
# Then log out and back in
```

### "Groups not updating"

```bash
# Force group refresh (without logout)
newgrp libvirt

# Or the nuclear option: log out and back in
```

### Still need sudo?

Check socket permissions:
```bash
ls -l /var/run/libvirt/libvirt-sock
# Should be: srw-rw---- 1 root libvirt ...
#                             ^^^^^^^ Group should be libvirt
```

If not, reinstall libvirt:
```bash
sudo apt install --reinstall libvirt-daemon-system
```

## Security Implications

### What Access Does This Grant?

**libvirt group** gives you:
- Full VM management on this host
- Network configuration
- Storage pool management
- Equivalent to "VM admin" role

**kvm group** gives you:
- Direct access to /dev/kvm
- Ability to run KVM-accelerated VMs

### Is This Safe?

**For personal development machines**: Yes, this is standard practice.

**For shared/production systems**: Consider the security implications:
- Users in `libvirt` can create VMs (resource exhaustion)
- Users in `libvirt` can access VM storage (data access)
- Users in `libvirt` can create virtual networks (network access)

**Recommended for**:
- Personal development workstations
- Dedicated test machines
- CI runners (self-hosted)

**Not recommended for**:
- Multi-user production servers
- Shared development environments
- Systems with strict access controls

## Alternative: PolicyKit Rules

For finer-grained control without group membership:

```bash
# Create PolicyKit rule for specific user
sudo tee /etc/polkit-1/rules.d/50-libvirt-$USER.rules <<EOF
polkit.addRule(function(action, subject) {
    if (action.id == "org.libvirt.unix.manage" &&
        subject.user == "$USER") {
        return polkit.Result.YES;
    }
});
EOF

# Restart PolicyKit
sudo systemctl restart polkit
```

This grants libvirt access to only your specific user without broad group membership.

## Summary

**Simplest approach** (recommended for development):
```bash
sudo usermod -aG libvirt,kvm $USER
# Then log out and back in
```

**After this**:
- ✅ All `virsh` commands work without sudo
- ✅ VM creation works without sudo
- ✅ `bazel run //vm:parallel_vm_tests` works without sudo
- ✅ Full KVM acceleration

**Security**: Standard for development workstations, equivalent to Docker group access.

