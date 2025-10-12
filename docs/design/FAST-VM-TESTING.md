# Fast VM Testing for Xibalba

## Status: DRAFT
**Created:** 2025-10-11  
**Author:** AI Assistant + User  
**Status:** Needs Review

## QEMU vs Libvirt vs KVM - Clearing Up Confusion

### The Stack

```
┌─────────────────────────────────────────────┐
│ Libvirt (management layer)                 │  ← We use this today
│  - virsh, virt-install, XML configs        │
│  - Network management, storage pools       │
└────────────────┬────────────────────────────┘
                 │
                 ↓
┌─────────────────────────────────────────────┐
│ QEMU (virtualizer + device emulation)      │  ← Proposed direct use
│  - Manages VM lifecycle                    │
│  - Emulates devices (disk, network, etc.)  │
│  - Can use KVM or pure emulation           │
└────────────────┬────────────────────────────┘
                 │
                 ↓
┌─────────────────────────────────────────────┐
│ KVM (kernel module)                        │  ← Hardware acceleration
│  - Hardware virtualization (VT-x/AMD-V)    │
│  - CPU runs at NATIVE speed                │
│  - Used by BOTH libvirt and direct QEMU    │
└─────────────────────────────────────────────┘
```

### Key Points

**Libvirt** = Management wrapper around QEMU
- Adds: Network management, XML configs, storage pools
- Overhead: SSH setup, network stack, systemd
- **Still uses KVM underneath** (hardware virtualization)

**Direct QEMU** = Same virtualization, less overhead
- **Still uses KVM** (hardware virtualization) via `-enable-kvm`
- Skip: Network setup, SSH, cloud-init
- Same speed for CPU, just faster boot/setup

**9p/Virtio-FS** = Just a file sharing protocol
- Like NFS or Docker volumes
- Doesn't affect CPU virtualization at all
- Can be used with libvirt OR direct QEMU

### Answer: True Virtualization Either Way! ✅

Both approaches use **KVM hardware virtualization**:
- CPU runs at native speed (Intel VT-x/AMD-V)
- Real kernel in VM (not shared with host)
- True isolation (separate memory space)

**Current status**: Already using KVM today!
```bash
$ lsmod | grep kvm
kvm_amd    # ← Hardware virtualization active
kvm        # ← KVM kernel module loaded
```

The difference is **boot/setup time**, not virtualization:
- Libvirt + SSH: 2-3 minutes (network, cloud-init, SSH)
- Direct QEMU: 10 seconds (skip all that overhead)

**Bottom line**: Both use same KVM hardware acceleration. QEMU just skips the overhead.

## Problem Statement

Current VM testing is slow (3-4 minutes to ready state) due to:
- Full Ubuntu Server cloud images (network, systemd, snapd, etc.)
- SSH-based deployment and testing
- Cloud-init package installation
- Network setup and DHCP
- Services we don't need for testing

**Core insight**: We only need a real Linux kernel to test filesystem operations. Everything else is overhead.

## Requirements

### Must Have
1. **Real Linux kernel** - Need actual VFS layer, not Docker's shared kernel
2. **Block device for testing** - Real block device (not root FS) to format and test
3. **Xibalba binaries available** - pause_controller, simple_chaos_test, etc.
4. **Filesystem tools** - mkfs.ext4, mkfs.zfs, etc.
5. **Test output capture** - Get results back to host
6. **Multiple parallel VMs** - Test ext4 and zfs simultaneously

### Don't Need
- SSH access (direct execution is fine)
- Network stack (no network testing)
- Systemd/init complexity (simple boot-to-test is fine)
- Cloud-init (custom init script is faster)
- Full Ubuntu (minimal rootfs is fine)

### Performance Goal
Boot to test execution: **< 10 seconds** (currently ~2-3 minutes)

## Options Analysis

### Option 1: Current Approach (SSH + Cloud-init)
**Description**: Full Ubuntu cloud image, SSH after boot

**Pros**:
- Works today
- Standard Ubuntu tooling
- Easy to debug (SSH in)

**Cons**:
- Slow boot (60-90 seconds)
- SSH setup delays (30-60 seconds)
- Cloud-init overhead
- Network complexity
- Package installation at runtime

**Verdict**: ❌ Too slow for rapid testing

---

### Option 2: QEMU with Virtio-9p/Virtio-FS + Custom Init
**Description**: Share host binaries via 9p/virtio-fs, custom init script, serial console

```bash
qemu-system-x86_64 \
  -enable-kvm \                    # ← HARDWARE VIRTUALIZATION (native speed!)
  -cpu host \                       # ← Use host CPU features
  -kernel vmlinuz \
  -initrd initrd.img \
  -append "console=ttyS0 init=/init.sh" \
  -fsdev local,id=xibalba,path=/host/xibalba,security_model=none \
  -device virtio-9p-pci,fsdev=xibalba,mount_tag=xibalba \
  -drive file=test-disk.qcow2,if=virtio \
  -nographic
```

**Virtualization Mode**: Uses KVM (hardware virtualization) - same as libvirt!
- **NOT emulation**: CPU runs at native speed via Intel VT-x/AMD-V
- **Real kernel**: Actual Linux kernel in VM, not shared with host
- **True isolation**: Separate memory, separate kernel, real /dev/vda

**Architecture**:
```
Host                           VM
┌────────────────┐            ┌────────────────────┐
│ xibalba/       │──9p/virtio→│ /opt/xibalba (ro)  │
│ ├─ binaries    │            │ └─ pause_controller│
│ └─ scripts     │            │                    │
└────────────────┘            │ /dev/vda (empty)   │
                              │ └─ format & test   │
                              │                    │
                              │ Serial → stdout    │
                              └────────────────────┘
```

**Pros**:
- **Very fast**: No SSH, no network, minimal boot
- **No installation**: Binaries shared from host
- **Simple**: Direct execution, no SSH complexity
- **Hermetic**: Easy to reproduce
- **Standard qemu**: Works everywhere

**Cons**:
- Need custom kernel/initrd setup
- Less "real" than full VM (but kernel is real!)
- 9p can be slower than native filesystem (but we don't test 9p, just /dev/vda)
- Custom init script maintenance

**Performance**: ~5-10 seconds boot-to-test

**Verdict**: ✅ Best balance of speed and simplicity

---

### Option 3: Firecracker
**Description**: AWS's microVM technology

**Pros**:
- **Very fast boot**: 125ms to userspace
- Production-grade (powers AWS Lambda)
- Minimal overhead
- Still real kernel

**Cons**:
- Linux host only (no macOS)
- More complex setup than qemu
- Less flexible than qemu
- Requires newer kernel features (KVM)
- Additional dependency

**Performance**: ~1-2 seconds boot-to-test

**Verdict**: ⚠️ Great performance but more complex, consider for future optimization

---

### Option 4: Kata Containers
**Description**: Docker-like interface with real VMs

**Pros**:
- Docker-like UX
- Real kernel isolation
- Standard tooling

**Cons**:
- Complex setup
- Still has container overhead
- Block device access less clear
- Overkill for our needs

**Verdict**: ❌ Too complex for our simple needs

---

### Option 5: Custom Minimal Rootfs (Busybox/Alpine)
**Description**: Build tiny rootfs with just what we need

**Pros**:
- Extremely fast boot
- Complete control
- Small size (~50MB)

**Cons**:
- Need to build/maintain rootfs
- May lack some tools (ZFS, btrfs-progs, etc.)
- Filesystem tool versions may differ from production

**Verdict**: ⚠️ Consider combining with Option 2

---

### Option 6: Keep Current + Pre-built Image
**Description**: Keep SSH approach but pre-install everything

**Pros**:
- Minimal code changes
- Familiar tooling
- Works with current setup

**Cons**:
- Still slow (60+ seconds)
- SSH overhead
- Network complexity
- Not fast enough for rapid iteration

**Verdict**: ❌ Better than before but not good enough

## Recommendation: Option 2 (QEMU + Virtio-9p + Custom Init)

### Why This Approach

1. **Speed**: 5-10 second boot vs 2-3 minutes
2. **Simplicity**: No SSH, no network, no cloud-init
3. **Standard tooling**: Uses qemu directly (already have libvirt/qemu)
4. **Hermetic**: Binaries from host, reproducible
5. **Real kernel**: Full VFS testing capability
6. **Bazel-friendly**: Easy to integrate with Bazel

### Architecture

```
┌─────────────────────────────────────────────────────────┐
│ Bazel Build                                             │
│ ├─ //packaging:xibalba-deb → extract binaries          │
│ ├─ //vm:minimal-rootfs → busybox + kernel + initrd     │
│ └─ //vm:test-runner → qemu invocation script           │
└─────────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────────┐
│ QEMU VM                                                 │
│                                                         │
│ Kernel Boot → Custom Init:                             │
│   1. Mount 9p share from host (xibalba binaries)       │
│   2. Load kernel modules (ext4, zfs, etc.)             │
│   3. Format /dev/vda as test filesystem                │
│   4. Run xibalba-gauntlet /dev/vda                      │
│   5. Output results to serial console                  │
│   6. Poweroff                                          │
│                                                         │
│ Serial Console → Host captures output                  │
└─────────────────────────────────────────────────────────┘
```

### Boot Flow

```
0.0s  : qemu start
0.5s  : kernel boot complete
1.0s  : init script starts
1.5s  : mount 9p share with xibalba binaries
2.0s  : format /dev/vda as ext4
3.0s  : start xibalba-gauntlet
3.0s+ : tests running...
```

### Implementation Phases

#### Phase 1: Proof of Concept (1-2 days)
- [ ] Extract kernel + initrd from Ubuntu cloud image
- [ ] Create minimal init script
- [ ] Test 9p mounting
- [ ] Run single test with serial console output
- [ ] Measure actual boot time

#### Phase 2: Bazel Integration (1-2 days)
- [ ] Create Bazel target for VM rootfs
- [ ] Integrate with //packaging:xibalba-deb
- [ ] Create test rule that launches VM
- [ ] Capture and parse test output
- [ ] Support multiple filesystems in parallel

#### Phase 3: Feature Parity (2-3 days)
- [ ] Support all filesystems (ext4, zfs, btrfs, xfs, etc.)
- [ ] Result aggregation and reporting
- [ ] Error handling and debugging
- [ ] CI integration
- [ ] Documentation

#### Phase 4: Polish (1-2 days)
- [ ] Performance tuning
- [ ] Better error messages
- [ ] Cleanup old SSH-based code
- [ ] Migration guide

**Total estimate**: 1-2 weeks

## Alternative Quick Win: Keep SSH but Optimize

If VM rebuild is too risky right now, we can still get big wins:

### Current Status
- ✅ No reboot after cloud-init (saves ~60s)
- ✅ Pre-install deps via cloud-init (saves ~30s)
- ✅ Use `dpkg -i` instead of `apt install` (saves ~20s)
- ✅ Disable snapd (saves ~10s)
- ✅ Skip apt update/upgrade (saves ~20s)

**Result**: ~60-90 seconds to ready (down from 3-4 minutes)

### Still Can Do
- [ ] Pre-built VM image with xibalba installed (saves ~30s)
  - Requires `sudo apt install libguestfs-tools`
  - Build once: `bazel build //vm:xibalba_vm_image`
  - No runtime installation needed

**Result**: ~30-60 seconds to ready

## Decision Matrix

| Criteria | Current (SSH) | SSH + Optimized | QEMU + 9p (Recommended) | Firecracker |
|----------|---------------|-----------------|-------------------------|-------------|
| Boot Time | 180s | 60s | **10s** | 5s |
| Complexity | Low | Low | Medium | High |
| Real Kernel | ✅ | ✅ | ✅ | ✅ |
| Bazel Integration | OK | OK | **Easy** | Medium |
| Debugging | Easy (SSH) | Easy (SSH) | Medium (serial) | Medium |
| CI Friendly | ❌ Slow | ⚠️ Slow | ✅ Fast | ✅ Fast |
| Dependencies | cloud-init | cloud-init, virt-customize | qemu only | firecracker binary |
| Maintenance | Low | Low | **Medium** | High |

## Recommendation Summary

1. **Immediate** (today): Current optimizations are good enough to proceed with testing
   - Boot time: ~60-90s (acceptable)
   - Already implemented

2. **Short-term** (this week): Install libguestfs-tools and build pre-built image
   - Boot time: ~30-60s
   - One command: `sudo apt install libguestfs-tools`
   - Big improvement for small effort

3. **Medium-term** (next sprint): Implement QEMU + 9p approach
   - Boot time: ~10s
   - Best long-term solution
   - More work but worth it for CI and rapid iteration

4. **Future**: Consider Firecracker if we need even faster (<5s)
   - Only if QEMU + 9p isn't fast enough
   - More complex, save for when we need it

## Open Questions

1. Do we need to test multiple kernel versions? (affects rootfs choice)
2. What's our CI environment - do we have KVM access?
3. Are we testing ZFS with DKMS or kernel modules?
4. Do we need to preserve test artifacts (core dumps, logs) after VM shutdown?
5. Should tests run in parallel within one VM or one VM per test?

## References

- [Virtio-9p Documentation](https://wiki.qemu.org/Documentation/9psetup)
- [Firecracker](https://firecracker-microvm.github.io/)
- [QEMU Direct Kernel Boot](https://qemu-project.gitlab.io/qemu/system/linuxboot.html)
- [Minimal Linux Live](http://minimal.linux-bg.org/) - Example of minimal rootfs
- Current Xibalba docs: `docs/design/JEPSEN-INSPIRED-FILESYSTEM-TESTING.md`

