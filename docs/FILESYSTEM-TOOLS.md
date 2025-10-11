# Filesystem Tools for Xibalba Testing

**Required tools to test all documented filesystems**

---

## Quick Install

```bash
# Install all filesystem tools (except ZFS)
./vm/install_filesystem_tools.sh

# Or manually:
sudo apt-get install -y \
  e2fsprogs \
  xfsprogs \
  btrfs-progs \
  f2fs-tools \
  nilfs-tools \
  nfs-common \
  exfatprogs \
  ntfs-3g \
  fuse3
```

---

## Filesystems and Their Tools

### Included in Debian Package (Recommended)

These are automatically installed when you install the `xibalba` package:

| Filesystem | Package | Purpose |
|------------|---------|---------|
| **ext4** | `e2fsprogs` | mkfs.ext4, fsck.ext4, tune2fs |
| **XFS** | `xfsprogs` | mkfs.xfs, xfs_repair, xfs_info |
| **btrfs** | `btrfs-progs` | mkfs.btrfs, btrfs check, btrfs balance |
| **F2FS** | `f2fs-tools` | mkfs.f2fs, fsck.f2fs |
| **NILFS2** | `nilfs-tools` | mkfs.nilfs2, mount.nilfs2 |
| **NFS (client)** | `nfs-common` | mount.nfs, showmount |

### Optional (Suggested)

Install these for additional filesystem testing:

| Filesystem | Package | Notes |
|------------|---------|-------|
| **ZFS** | `zfsutils-linux` | May require enabling contrib repository |
| **NFS (server)** | `nfs-kernel-server` | For setting up NFS test servers |
| **exFAT** | `exfatprogs` | mkfs.exfat, fsck.exfat |
| **NTFS** | `ntfs-3g` | NTFS support via FUSE |
| **CIFS/SMB** | `cifs-utils` | mount.cifs for Windows shares |
| **FUSE** | `fuse3` | User-space filesystems (sshfs, etc.) |

### Already Available (No Install Needed)

| Filesystem | Notes |
|------------|-------|
| **tmpfs** | Kernel built-in, always available |
| **procfs** | Kernel built-in, always available |
| **sysfs** | Kernel built-in, always available |

---

## Installation Methods

### Method 1: Debian Package (Recommended)

```bash
# Install Xibalba with recommended filesystem tools
sudo dpkg -i xibalba_0.1.0_amd64.deb
sudo apt-get install -f  # Install dependencies

# Recommended tools are installed automatically
# Suggested tools need manual installation:
sudo apt-get install zfsutils-linux nfs-kernel-server exfatprogs
```

### Method 2: Installation Script

```bash
# Run our helper script
sudo ./vm/install_filesystem_tools.sh
```

### Method 3: Manual Installation

```bash
# Core filesystems (ext4, XFS, btrfs)
sudo apt-get install -y e2fsprogs xfsprogs btrfs-progs

# Flash-optimized (F2FS, NILFS2)
sudo apt-get install -y f2fs-tools nilfs-tools

# Network filesystems
sudo apt-get install -y nfs-common nfs-kernel-server

# Additional filesystems
sudo apt-get install -y exfatprogs ntfs-3g cifs-utils fuse3

# ZFS (requires contrib repo)
sudo apt-get install -y zfsutils-linux
```

---

## ZFS Special Instructions

ZFS requires the `contrib` repository on Debian/Ubuntu:

```bash
# Ubuntu
sudo apt-add-repository universe
sudo apt-get update
sudo apt-get install zfsutils-linux

# Debian
# Add 'contrib' to /etc/apt/sources.list
# deb http://deb.debian.org/debian bookworm main contrib
sudo apt-get update
sudo apt-get install zfsutils-linux
```

---

## Verifying Installation

```bash
# Check which filesystem tools are available
which mkfs.ext4 mkfs.xfs mkfs.btrfs mkfs.f2fs mkfs.nilfs2

# Check versions
mkfs.ext4 -V
mkfs.xfs -V
mkfs.btrfs --version
```

---

## Creating Test Filesystems

### ext4

```bash
# Create loopback device
dd if=/dev/zero of=ext4.img bs=1M count=100
mkfs.ext4 ext4.img
sudo mount -o loop ext4.img /mnt/test
```

### XFS

```bash
dd if=/dev/zero of=xfs.img bs=1M count=100
mkfs.xfs xfs.img
sudo mount -o loop xfs.img /mnt/test
```

### btrfs

```bash
dd if=/dev/zero of=btrfs.img bs=1M count=100
mkfs.btrfs btrfs.img
sudo mount -o loop btrfs.img /mnt/test
```

### ZFS

```bash
# Create a pool from a file
dd if=/dev/zero of=zfs.img bs=1M count=1000
sudo zpool create testpool $(pwd)/zfs.img
sudo zfs create testpool/testfs
# Mount at /testpool/testfs
```

### F2FS

```bash
dd if=/dev/zero of=f2fs.img bs=1M count=100
mkfs.f2fs f2fs.img
sudo mount -o loop f2fs.img /mnt/test
```

### tmpfs (No Image Needed)

```bash
sudo mount -t tmpfs -o size=100M tmpfs /mnt/test
```

---

## VM Images

### Automatic Installation in VMs

When creating test VMs, filesystem tools are automatically installed:

```bash
# Create VM with filesystem tools pre-installed
bazel run //vm:create_vm -- --name test-01

# Filesystem tools are installed during VM creation
# See vm/create_test_vm.sh for details
```

### Manual Installation in Existing VM

```bash
# Copy and run installation script
scp vm/install_filesystem_tools.sh root@test-vm:
ssh root@test-vm 'sudo bash install_filesystem_tools.sh'
```

---

## CI/CD Considerations

**GitHub Actions runners** come with:
- ✅ ext4 tools (e2fsprogs)
- ✅ Basic utilities

**Not included by default**:
- ❌ XFS, btrfs, F2FS, NILFS2
- ❌ ZFS (licensing/kernel module issues)

For CI testing with multiple filesystems, use VM-based testing or self-hosted runners with custom images.

---

## Disk Space Requirements

| Test Scenario | Disk Space Needed |
|---------------|-------------------|
| Single filesystem (100MB image) | ~150MB (including overhead) |
| All filesystems (test images) | ~2GB |
| VM with all tools | ~10GB root + 5GB data disk |

---

## Troubleshooting

### "mkfs.xfs not found"

```bash
sudo apt-get install xfsprogs
```

### "zpool: command not found"

ZFS needs contrib repository (see ZFS Special Instructions above)

### "mount.nfs: command not found"

```bash
sudo apt-get install nfs-common
```

### "Permission denied" when creating filesystems

Most `mkfs.*` commands need root:

```bash
sudo mkfs.ext4 test.img
```

---

## See Also

- [FILESYSTEM-CONSISTENCY-MODELS.md](design/FILESYSTEM-CONSISTENCY-MODELS.md) - Detailed filesystem documentation
- [TESTING-GUIDE.md](../TESTING-GUIDE.md) - How to run tests on different filesystems
- [vm/README.md](../vm/README.md) - VM setup and configuration

---

**Summary**: Install filesystem tools with `./vm/install_filesystem_tools.sh` or via the Debian package's recommended dependencies.

