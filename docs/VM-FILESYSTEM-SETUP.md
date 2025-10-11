# VM Filesystem Setup Guide

**How to set up VMs for testing ext4, ZFS, and other filesystems**

---

## Quick Answer

**YES, but only if you install correctly!**

The Xibalba Debian package declares:
- **Recommends**: ext4, XFS, btrfs, F2FS, NILFS2, NFS tools
- **Suggests**: ZFS, exFAT, NTFS, CIFS, FUSE tools

### Installation Methods

**❌ Wrong (doesn't install filesystem tools)**:
```bash
dpkg -i xibalba_0.1.0_amd64.deb
# Only installs libbpf0, libelf1
# Filesystem tools NOT installed!
```

**✅ Correct (installs filesystem tools automatically)**:
```bash
apt install ./xibalba_0.1.0_amd64.deb
# Installs libbpf0, libelf1 (depends)
# ALSO installs e2fsprogs, xfsprogs, btrfs-progs, etc. (recommends)
# Does NOT install zfsutils-linux (suggests only)
```

---

## Filesystem Tool Installation Matrix

| Tool | Package Type | `dpkg -i` | `apt install` | Notes |
|------|--------------|-----------|---------------|-------|
| libbpf0 | depends | ✅ | ✅ | Always installed |
| libelf1 | depends | ✅ | ✅ | Always installed |
| e2fsprogs (ext4) | recommends | ❌ | ✅ | ext4 testing |
| xfsprogs (XFS) | recommends | ❌ | ✅ | XFS testing |
| btrfs-progs | recommends | ❌ | ✅ | btrfs testing |
| f2fs-tools | recommends | ❌ | ✅ | F2FS testing |
| nilfs-tools | recommends | ❌ | ✅ | NILFS2 testing |
| nfs-common | recommends | ❌ | ✅ | NFS client |
| **zfsutils-linux** | **suggests** | ❌ | ❌ | **Must install manually!** |
| exfatprogs | suggests | ❌ | ❌ | Must install manually |

---

## Setting Up VMs for Your Tests

### ext4 Testing

**ext4 tools included!**

```bash
# Create VM
bazel run //vm:create_vm -- --name ext4-test

# Deploy Xibalba (use apt install!)
ssh root@ext4-test
apt install ./xibalba_0.1.0_amd64.deb  # Installs e2fsprogs automatically

# Create ext4 filesystem
mkdir -p /mnt/ext4
dd if=/dev/zero of=/root/ext4.img bs=1M count=1000
mkfs.ext4 /root/ext4.img
mount -o loop /root/ext4.img /mnt/ext4

# Run test (5 minutes)
simple_chaos_test --weak /mnt/ext4
```

### ZFS Testing

**ZFS tools NOT included (suggests only)!**

```bash
# Create VM
bazel run //vm:create_vm -- --name zfs-test

# Deploy Xibalba
ssh root@zfs-test
apt install ./xibalba_0.1.0_amd64.deb

# Install ZFS manually (suggests doesn't auto-install)
apt install zfsutils-linux

# Create ZFS pool
dd if=/dev/zero of=/root/zfs.img bs=1M count=2000
zpool create testpool /root/zfs.img
zfs create testpool/testfs

# Run test (5 minutes)
simple_chaos_test --weak /testpool/testfs
```

---

## Recommended VM Deployment Script

Update `vm/deploy_xibalba.sh` to use `apt install` instead of manual binary copying:

```bash
#!/bin/bash
# Deploy Xibalba to VM

VM_NAME=$1
PACKAGE_PATH="bazel-bin/packaging/xibalba_0.1.0_amd64.deb"

# Copy package to VM
scp $PACKAGE_PATH root@$VM_NAME:/tmp/

# Install with apt (gets recommends!)
ssh root@$VM_NAME "apt install -y /tmp/xibalba_0.1.0_amd64.deb"

# Verify filesystem tools
ssh root@$VM_NAME "which mkfs.ext4 mkfs.xfs mkfs.btrfs"

# For ZFS, install manually
if [[ "$VM_NAME" == *"zfs"* ]]; then
    ssh root@$VM_NAME "apt install -y zfsutils-linux"
fi
```

---

## Testing Both ext4 and ZFS

**Total estimated time**: ~17 minutes

```bash
# Setup (once)
bazel build //packaging:xibalba-deb

# Create VMs
bazel run //vm:create_vm -- --name ext4-test
bazel run //vm:create_vm -- --name zfs-test

# Deploy to both (~1 min each)
bazel run //vm:deploy_xibalba -- ext4-test
bazel run //vm:deploy_xibalba -- zfs-test

# Install ZFS tools on ZFS VM
ssh root@zfs-test "apt install -y zfsutils-linux"

# Create filesystems and run tests
ssh root@ext4-test << 'SCRIPT'
  # Setup ext4 (~30 sec)
  dd if=/dev/zero of=/root/ext4.img bs=1M count=1000
  mkfs.ext4 -q /root/ext4.img
  mkdir -p /mnt/test
  mount -o loop /root/ext4.img /mnt/test
  
  # Run test (5 min)
  simple_chaos_test --weak /mnt/test
SCRIPT

ssh root@zfs-test << 'SCRIPT'
  # Setup ZFS (~1 min)
  dd if=/dev/zero of=/root/zfs.img bs=1M count=2000
  zpool create testpool /root/zfs.img
  zfs create testpool/testfs
  
  # Run test (5 min)
  simple_chaos_test --weak /testpool/testfs
SCRIPT
```

**Breakdown**:
- VM creation (once): 2-3 min
- Deploy to 2 VMs: 2 min
- ext4 setup + test: 6 min
- ZFS setup + test: 7 min
- **Total: ~17 minutes**

---

## Summary

**Answer**: The package has the right dependencies, but:

1. ✅ **ext4, XFS, btrfs, F2FS, NILFS2, NFS** → Installed automatically with `apt install`
2. ❌ **ZFS** → Must install manually (`apt install zfsutils-linux`)
3. ⚠️  **Must use `apt install`, NOT `dpkg -i`** to get recommends

**Next step**: Update `vm/deploy_xibalba.sh` to use `apt install` so filesystem tools are installed automatically!

Should I update the deploy script now?

