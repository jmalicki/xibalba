# RUDRA VM Setup Guide

**Why VMs?** Full control over kernel config, no permission issues, test custom kernels safely.

---

## Quick Start

### Option 1: Single Test VM (Fast Start)

```bash
# Create VM with custom kernel
bazel run //vm:create_vm -- --name rudra-test-01 --kernel /path/to/your/kernel

# Deploy RUDRA
bazel run //vm:deploy_rudra -- rudra-test-01

# Run tests
bazel run //vm:run_tests -- rudra-test-01
```

### Option 2: Multi-Filesystem Matrix (Full Testing)

```bash
# Create VMs for each filesystem
for fs in ext4 xfs btrfs tmpfs; do
  bazel run //vm:create_vm -- --name rudra-$fs --filesystem $fs
done

# Run tests on all VMs in parallel
for vm in rudra-{ext4,xfs,btrfs,tmpfs}; do
  bazel run //vm:run_tests -- $vm > results-$vm.txt 2>&1 &
done
wait

# View results
cat results-*.txt
```

---

## VM Requirements

**Minimum per VM**:
- 2 GB RAM
- 10 GB disk
- 2 vCPUs

**For full matrix** (4 filesystems × 1 VM each):
- 8 GB RAM total
- 40 GB disk total
- QEMU/KVM with libvirt

**Host machine confirmed**: You have the resources! ✅

---

## VM Kernel Configuration

**Required kernel options for RUDRA**:

```bash
CONFIG_BPF_SYSCALL=y                 # eBPF support
CONFIG_BPF_JIT=y                     # eBPF JIT compiler
CONFIG_HAVE_EBPF_JIT=y              # eBPF JIT backend
CONFIG_BPF_EVENTS=y                  # BPF perf events
CONFIG_KPROBES=y                     # Kprobes for eBPF
CONFIG_KPROBE_EVENTS=y              # Kprobe event tracing
CONFIG_BPF_KPROBE_OVERRIDE=y        # ⭐ ERROR INJECTION (key feature!)
CONFIG_DEBUG_INFO_BTF=y             # BTF debug info (for CO-RE)
CONFIG_DEBUG_INFO_BTF_MODULES=y     # BTF for modules
```

**Your custom VFS kernel**: Just enable these options and install in VM!

---

## VM Image Creation

### Base Image: Ubuntu 24.04 Cloud Image

```bash
# Download
wget https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img

# Customize with your kernel
virt-customize -a noble-server-cloudimg-amd64.img \
  --upload /path/to/your/vmlinuz:/boot/vmlinuz-custom \
  --upload /path/to/your/initrd:/boot/initrd-custom \
  --run-command 'update-grub' \
  --install build-essential,clang,libbpf-dev,linux-headers-generic

# Clone for each filesystem
cp noble-server-cloudimg-amd64.img rudra-ext4.qcow2
cp noble-server-cloudimg-amd64.img rudra-xfs.qcow2
cp noble-server-cloudimg-amd64.img rudra-btrfs.qcow2
```

---

## Filesystem Setup Inside Each VM

**ext4 VM**:
```bash
mkfs.ext4 /dev/vdb
mount /dev/vdb /test
```

**XFS VM**:
```bash
mkfs.xfs /dev/vdb
mount /dev/vdb /test
```

**btrfs VM**:
```bash
mkfs.btrfs /dev/vdb
mount /dev/vdb /test
```

**tmpfs** (no extra disk needed):
```bash
mount -t tmpfs -o size=1G tmpfs /test
```

---

## Deploy RUDRA to VMs

```bash
# Build locally with Bazel
bazel build //...

# Deploy to VM (automated)
bazel run //vm:deploy_rudra -- VM_NAME

# Or manually copy
scp -r bazel-bin/chaos/{pause_controller,simple_chaos_test,pause_injector.bpf.o} root@vm-ip:/root/rudra/
```

---

## Run Tests in VM

**From host machine**:

```bash
# Run tests in VM (automated)
bazel run //vm:run_tests -- VM_NAME
```

**Or manually inside VM** (SSH in as root, no permission issues!):

```bash
cd /root/rudra

# Start error injector (works perfectly in VM!)
./pause_controller 50 11 &

# Run chaos test
./simple_chaos_test /test/rudra_test

# Should see:
#   - Errors injected: ~20K/sec
#   - Operations slowed by 50%+
#   - BUGS FOUND! (hopefully!)
```

---

## Available Bazel Targets

All VM operations are exposed as Bazel targets (see `vm/BUILD.bazel`):

1. **`//vm:create_vm`** - Create single VM with custom kernel
2. **`//vm:deploy_rudra`** - Deploy RUDRA binaries to VM
3. **`//vm:run_tests`** - SSH in and run tests
4. **`//vm:destroy_vm`** - Destroy VM and clean up
5. **`//vm:check_prerequisites`** - Verify host has QEMU/KVM/libvirt

**Multi-VM Testing** (use shell loops for matrix testing):
```bash
# Create multiple VMs
for fs in ext4 xfs btrfs tmpfs; do
  bazel run //vm:create_vm -- --name test-$fs --filesystem $fs
done

# Run tests in parallel
for vm in test-{ext4,xfs,btrfs,tmpfs}; do
  bazel run //vm:run_tests -- $vm > results-$vm.txt &
done
wait
```

---

## Benefits of VM Approach

✅ **Full kernel control**: Enable ALL eBPF features  
✅ **No permission fights**: Root in VM = no capability issues  
✅ **Test your VFS changes**: Install custom kernel easily  
✅ **Reproducible**: Snapshot before each test  
✅ **Parallel testing**: Run 4 filesystems simultaneously  
✅ **Safe**: Crash VM, not host  
✅ **CI-ready**: Automate everything  

---

## Next Steps

1. ✅ Confirm QEMU/KVM installed on host
2. ✅ Create first test VM with your custom kernel
3. ✅ Deploy RUDRA binaries
4. ✅ Run test with full eBPF support
5. ✅ Find bugs! 🐛

**Ready to create the VM scripts?**



