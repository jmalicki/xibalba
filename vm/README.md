# RUDRA VM Infrastructure

Automated scripts for testing RUDRA in VMs with full kernel control.

---

## Quick Start

**Note**: All VM operations use Bazel for consistency with the rest of RUDRA.

### 1. Prerequisites Check

```bash
bazel run //vm:check_prerequisites
```

Verifies: QEMU/KVM, libvirt, virt-install, virt-customize

### 2. Create a Test VM

```bash
# Simple: Use existing kernel
bazel run //vm:create_vm -- --name rudra-test-01

# With custom kernel
bazel run //vm:create_vm -- --name rudra-test-01 --kernel /path/to/vmlinuz --initrd /path/to/initrd

# With specific filesystem
bazel run //vm:create_vm -- --name rudra-xfs-test --filesystem xfs
```

### 3. Deploy RUDRA

```bash
bazel run //vm:deploy_rudra -- rudra-test-01
```

**Note**: This automatically uses the latest Bazel-built binaries from `bazel-bin/chaos/`.

### 4. Run Tests

```bash
# Interactive (see output in real-time)
bazel run //vm:run_tests -- rudra-test-01

# Automated (collect results)
bazel run //vm:run_tests -- --automated rudra-test-01 > results.txt
```

---

## Bazel Targets

All VM operations are exposed as Bazel targets for consistency and dependency tracking.

### `//vm:check_prerequisites`
Checks if host has required tools.

```bash
bazel run //vm:check_prerequisites
```

### `//vm:create_vm`
Creates a VM configured for RUDRA testing.

**Options**:
- `--name NAME` - VM name (required)
- `--kernel PATH` - Custom kernel vmlinuz
- `--initrd PATH` - Custom initrd
- `--filesystem TYPE` - ext4, xfs, btrfs, tmpfs (default: ext4)
- `--ram MB` - RAM in MB (default: 2048)
- `--disk GB` - Disk size in GB (default: 10)

**What it does**:
- Downloads Ubuntu 24.04 cloud image
- Installs eBPF tools (clang, libbpf-dev)
- Installs your custom kernel (if provided)
- Creates test data disk
- Formats with specified filesystem
- Configures for RUDRA testing

**Example**:
```bash
bazel run //vm:create_vm -- --name test-01 --filesystem xfs
```

### `//vm:deploy_rudra`
Deploys RUDRA binaries to a VM.

**Usage**:
```bash
bazel run //vm:deploy_rudra -- VM_NAME
```

**What it does**:
- Uses binaries from `bazel-bin/chaos/` (already built by Bazel)
- Copies binaries to VM via SSH
- Sets up test directories
- Verifies eBPF can load

**Example**:
```bash
bazel run //vm:deploy_rudra -- test-01
```

### `//vm:run_tests`
Runs chaos tests in VM.

**Usage**:
```bash
bazel run //vm:run_tests -- [--automated] VM_NAME
```

**What it does**:
- Starts error injector
- Runs chaos test
- Reports results
- Collects statistics

**Example**:
```bash
bazel run //vm:run_tests -- test-01
```

### `//vm:destroy_vm`
Destroys a test VM.

**Usage**:
```bash
bazel run //vm:destroy_vm -- VM_NAME
```

**Example**:
```bash
bazel run //vm:destroy_vm -- test-01
```

---

## Multi-VM Testing

### Creating Multiple VMs

To test across multiple filesystems, create VMs individually:

```bash
# Create VMs for each filesystem
bazel run //vm:create_vm -- --name rudra-ext4 --filesystem ext4
bazel run //vm:create_vm -- --name rudra-xfs --filesystem xfs
bazel run //vm:create_vm -- --name rudra-btrfs --filesystem btrfs
bazel run //vm:create_vm -- --name rudra-tmpfs --filesystem tmpfs
```

### Deploying to Multiple VMs

```bash
# Deploy to all VMs
for vm in rudra-ext4 rudra-xfs rudra-btrfs rudra-tmpfs; do
  bazel run //vm:deploy_rudra -- $vm
done
```

### Running Tests in Parallel

```bash
# Run tests on all VMs (in parallel with background jobs)
for vm in rudra-ext4 rudra-xfs rudra-btrfs rudra-tmpfs; do
  bazel run //vm:run_tests -- $vm > results-$vm.txt &
done

# Wait for all to complete
wait

# View results
cat results-*.txt
```

---

## VM Details

### Network
- VMs use NAT networking
- SSH on port 22 (forwarded to random host port)
- Or use `virsh console VM_NAME`

### Login
- Username: `root` or `ubuntu`
- SSH key-based auth
- Your host SSH key is copied automatically

### Storage
- Boot disk: 10 GB qcow2
- Data disk: 5 GB raw (for test filesystem)
- Data disk mounted at `/test`

### eBPF Configuration
VMs are configured with:
- `CONFIG_BPF_KPROBE_OVERRIDE=y` ✅
- `CONFIG_DEBUG_INFO_BTF=y` ✅
- All eBPF features enabled ✅

---

## Typical Workflows

### Single VM Testing

```bash
# 1. Create VM with your custom kernel
bazel run //vm:create_vm -- --name my-test \
  --kernel ~/linux/arch/x86/boot/bzImage \
  --initrd ~/linux/initrd.img

# 2. Deploy RUDRA
bazel run //vm:deploy_rudra -- my-test

# 3. Run tests
bazel run //vm:run_tests -- my-test

# 4. Iterate (make changes, redeploy)
bazel build //chaos:pause_controller  # Rebuild after changes
bazel run //vm:deploy_rudra -- my-test  # Deploy updated binaries
bazel run //vm:run_tests -- my-test     # Re-run tests

# 5. Clean up when done
bazel run //vm:destroy_vm -- my-test
```

### Multi-Filesystem Matrix Testing

```bash
# 1. Create VMs for each filesystem
for fs in ext4 xfs btrfs tmpfs; do
  bazel run //vm:create_vm -- --name rudra-$fs --filesystem $fs
done

# 2. Deploy to all
for vm in rudra-{ext4,xfs,btrfs,tmpfs}; do
  bazel run //vm:deploy_rudra -- $vm
done

# 3. Run tests on all (parallel)
for vm in rudra-{ext4,xfs,btrfs,tmpfs}; do
  bazel run //vm:run_tests -- $vm > results-$vm.txt 2>&1 &
done
wait

# 4. View results
cat results-*.txt

# 5. Clean up all VMs
for vm in rudra-{ext4,xfs,btrfs,tmpfs}; do
  bazel run //vm:destroy_vm -- $vm
done
```

---

## Troubleshooting

### VM won't start
```bash
virsh list --all
virsh start VM_NAME
virsh console VM_NAME
```

### Can't SSH to VM
```bash
virsh domifaddr VM_NAME  # Get IP
ssh root@<IP>
```

### eBPF won't load in VM
```bash
# Inside VM
zgrep CONFIG_BPF /proc/config.gz
dmesg | grep -i bpf
```

### Performance issues
- Ensure KVM is enabled: `lsmod | grep kvm`
- Check VM has enough RAM: `virsh dominfo VM_NAME`
- Use virtio drivers (scripts do this automatically)

---

## Advanced Usage

### Custom Kernel Config
Edit `configs/kernel-config-rudra` before creating VM.

### Snapshot Before Testing
```bash
virsh snapshot-create-as VM_NAME pre-test
# Run tests...
virsh snapshot-revert VM_NAME pre-test
```

### Serial Console Access
```bash
virsh console VM_NAME
# (Ctrl+] to exit)
```

---

## Files Created

```
vm/
├── images/              # VM disk images
│   ├── rudra-test-01.qcow2
│   └── rudra-test-01-data.qcow2
├── configs/             # Config files
│   ├── cloud-init/
│   └── kernel-config-rudra
├── results/             # Test results
│   └── test-run-YYYYMMDD-HHMMSS/
└── scripts/             # Generated scripts
```

---

## CI Integration

For automated CI testing:

```yaml
# .github/workflows/vm-tests.yml
- name: Install Bazel
  uses: bazelbuild/setup-bazelisk@v2

- name: Build everything
  run: bazel build //...

- name: Check VM prerequisites
  run: bazel run //vm:check_prerequisites

- name: Create test VM
  run: bazel run //vm:create_vm -- --name ci-test-${{ github.run_id }}

- name: Deploy RUDRA to VM
  run: bazel run //vm:deploy_rudra -- ci-test-${{ github.run_id }}

- name: Run tests in VM
  run: bazel run //vm:run_tests -- ci-test-${{ github.run_id }}

- name: Clean up VM
  if: always()
  run: bazel run //vm:destroy_vm -- ci-test-${{ github.run_id }}
```

---

**Ready to create your first VM!** 🚀



