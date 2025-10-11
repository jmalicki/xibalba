# Pre-built VM Image for Faster Testing

## Overview

We've optimized VM testing in two ways:

1. **Pre-install dependencies during cloud-init** (already working)
   - All runtime deps (`libbpf1`, `libelf1`, filesystem tools) are installed during VM first boot
   - Package deployment uses fast `dpkg -i` instead of slow `apt install`
   
2. **Pre-built VM image** (requires setup)
   - Build a VM image with xibalba **already installed**
   - No installation at runtime - just boot and test!
   - Saves ~30-60 seconds per test run

## Setup for Pre-built Image

### Install libguestfs-tools

```bash
sudo apt install libguestfs-tools
```

This provides `virt-customize` for building custom VM images.

### Build the Pre-built Image

```bash
bazel build //vm:xibalba_vm_image
```

This creates `bazel-bin/vm/xibalba-ready.qcow2` with:
- All runtime dependencies
- Xibalba package already installed
- Optimizations (snapd disabled, etc.)

### Use Pre-built Image

Once built, the test scripts automatically detect and use it:

```bash
./vm/run-parallel-vm-tests.sh
```

The scripts check for `xibalba-gauntlet` command in the VM - if found, skips all deployment!

## Performance Impact

| Method | Time to Ready | Notes |
|--------|--------------|-------|
| Vanilla (apt install) | ~3-4 min | Downloads deps every time |
| Cloud-init deps (dpkg -i) | ~1-2 min | Uses pre-installed deps |
| **Pre-built image** | **~30-60 sec** | Everything ready, just boot! |

## Without Pre-built Image

Tests still work! They fall back to:
1. Download stock Ubuntu cloud image
2. Install deps via cloud-init (first boot only)
3. Deploy xibalba via `dpkg -i` after SSH ready

This is still much faster than the original `apt install` approach.

