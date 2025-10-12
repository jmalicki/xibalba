# ZFS VM Testing Limitation

**Status**: ZFS pool creation fails in minimal initramfs environment

## Issue

ZFS attempts to create a GPT partition table on `/dev/vda` and access `/dev/vda1`, but:
- Minimal initramfs doesn't have `udev`
- Partition device nodes (vda1, vda9) aren't automatically created
- Error: `failed to detect device partitions on '/dev/vda1': 19` (ENODEV)

## Error Output
```
[    2.968845]  vda: vda1 vda9
cannot label 'vda': failed to detect device partitions on '/dev/vda1': 19
Error preparing/labeling disk.
```

## Root Cause

ZFS behavior with whole disks:
1. Creates GPT partition table on `/dev/vda`
2. Expects partition devices (`/dev/vda1`, `/dev/vda9`) to exist
3. Requires `udev` or `mdev` to create device nodes
4. Minimal initramfs lacks dynamic device management

## Potential Solutions

### Option A: Add udev to initramfs (HEAVY)
- Install udev/eudev
- Run `udevadm trigger` after zpool create
- Increases initramfs size significantly

### Option B: Use partition instead of whole disk
- Pre-create partition with `fdisk`/`parted`
- Give ZFS `/dev/vda1` instead of `/dev/vda`
- Requires partition tools in initramfs

### Option C: Use file-backed vdev (SLOW)
- Create file with `dd`
- Use `zpool create -f pool /test/zfsfile`
- Much slower than block device

### Option D: Accept limitation
- Test ZFS on host system instead
- Focus VM testing on ext4/XFS/btrfs
- Document ZFS incompatibility with minimal VM

## Current Decision

**Option D**: Skip ZFS in VM tests due to complexity.

ZFS requires more infrastructure than other filesystems:
- Dynamic device management (udev)
- Partition handling
- Complex setup time

For filesystem consistency comparison, ext4/XFS/btrfs provide sufficient coverage:
- ext4: Traditional journaling FS
- XFS: High-performance, extent-based
- btrfs: COW filesystem

## Workaround for ZFS Testing

Test ZFS on host system with full kernel and udev:
```bash
# Create ZFS pool on host
sudo zpool create -f test-pool /dev/loop0

# Run Xibalba tests
bazel run //chaos:simple_chaos_test -- \
    --posix --duration 30 --readers 4 --writers 2 \
    /test-pool
```

## Summary

**VM Test Coverage**:
- ext4: ✅ Full support
- XFS: ✅ Full support  
- btrfs: ✅ Full support
- ZFS: ❌ Requires host testing

This limitation doesn't affect the validity of the comparison - the three tested filesystems represent different design philosophies and provide meaningful empirical data.
