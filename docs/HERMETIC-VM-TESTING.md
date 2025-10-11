# Hermetic VM Testing Architecture

This document explains how Xibalba balances Bazel's hermetic build philosophy with VM testing's system-level requirements.

## The Split: What Bazel Provides vs. System Prerequisites

### ✅ Bazel Provides (Hermetic)

These are **build artifacts** that Bazel can hermetically provide:

- `cloud-localds` - Tool for creating cloud-init ISOs
- `qemu-img` - Disk image creation tool  
- `genisoimage` - ISO creation tool
- Any other userspace tools needed

**How**: Bazel can download, build, or vendor these tools as part of the build.

**Status**: TODO - Currently relies on system packages, should be made hermetic.

### ❌ System Prerequisites (Non-Hermetic)

These are **system-level requirements** that Bazel CANNOT provide:

1. **libvirtd daemon**
   - System service (needs root, systemd)
   - Manages VMs, networking, storage
   - Bazel can't start system services

2. **KVM kernel module**
   - Kernel feature (needs hardware support)
   - Loaded with `modprobe kvm-intel` or `kvm-amd`
   - Bazel can't modify kernel

3. **User permissions**
   - Group membership: `libvirt`, `kvm`
   - Set by: `sudo usermod -aG libvirt,kvm $USER`
   - Bazel can't change system permissions

4. **Disk space & RAM**
   - Physical resources (50GB+ disk, 8GB+ RAM)
   - Bazel can't allocate hardware

**How**: One-time manual setup by user.

## The Bazel Way

### Current State (Mixed)

```
bazel run //vm:parallel_vm_tests
  ├─ Builds: xibalba package ✅ (hermetic)
  ├─ Checks: libvirt installed? ❌ (runtime check)
  └─ Fails: If tools missing ❌ (wrong place to check)
```

**Problem**: Runtime checks violate Bazel's philosophy.

### Target State (Bazel-First)

```
# Step 1: User installs system prerequisites (one-time)
sudo apt install libvirt-daemon-system qemu-kvm virtinst cloud-image-utils
sudo usermod -aG libvirt,kvm $USER
# Log out and back in

# Step 2: Bazel validates (fast fail)
bazel test //vm:verify_host_deps
# FAIL if missing → clear error message
# PASS if ready → proceed

# Step 3: Bazel provides tools hermetically (future)
bazel run //vm:parallel_vm_tests
  ├─ Uses: hermetic cloud-localds (Bazel-provided) ✅
  ├─ Uses: system libvirtd (prerequisite) ✅
  └─ Runs: VM tests

```

## Implementation Plan

### Phase 1: Clear Separation (Now)

- ✅ Remove runtime checks from scripts
- ✅ Create `//vm:verify_host_deps` test
- ✅ Document system prerequisites clearly
- ✅ Trust that Bazel test passed

### Phase 2: Hermetic Tools (Future)

Make Bazel provide tools hermetically:

```python
# vm/BUILD.bazel

# Download cloud-image-utils source and build hermetically
http_archive(
    name = "cloud_utils",
    url = "https://launchpad.net/cloud-utils/...",
    build_file = "//vm:cloud_utils.BUILD",
)

# Provide hermetic cloud-localds
sh_binary(
    name = "hermetic_cloud_localds",
    srcs = ["@cloud_utils//:cloud-localds"],
    data = ["@cloud_utils//:runtime_deps"],
)

# VM scripts use hermetic tools
sh_binary(
    name = "create_vm",
    srcs = ["create_test_vm.sh"],
    data = [
        ":hermetic_cloud_localds",  # Bazel-provided!
        ":hermetic_qemu_img",       # Bazel-provided!
    ],
    env = {
        "CLOUD_LOCALDS": "$(location :hermetic_cloud_localds)",
        "QEMU_IMG": "$(location :hermetic_qemu_img)",
    },
)
```

**Benefits**:
- No system package dependencies for tools
- Reproducible across machines
- Specific versions (not "whatever apt has")
- True hermetic builds

### Phase 3: Containerized VMs (Optional)

For truly hermetic testing:

```python
# Run tests in containers that run VMs (requires KVM passthrough)
container_test(
    name = "vm_tests_containerized",
    image = "//vm:test_environment",  # Has libvirt, KVM
    test = "//vm:parallel_vm_tests",
    requires_kvm = True,  # Host must have KVM
)
```

## Current Workflow (Phase 1)

### One-Time Setup

User runs:
```bash
# Install system prerequisites
sudo apt install -y libvirt-daemon-system qemu-kvm virtinst cloud-image-utils
sudo usermod -aG libvirt,kvm $USER
# Log out and back in
```

See: `docs/VM-PERMISSIONS.md`

### Validate Environment

```bash
# Bazel test validates prerequisites
bazel test //vm:verify_host_deps
```

**Output if missing**:
```
✗ cloud-localds (install: cloud-image-utils)
❌ Missing dependencies. Install with:
  sudo apt install -y cloud-image-utils
```

**Output if ready**:
```
✓ virsh
✓ virt-install
✓ cloud-localds
✓ qemu-system-x86_64
✅ All host dependencies satisfied
```

### Run Tests

```bash
# Bazel assumes prerequisites are met (validated by test)
bazel run //vm:parallel_vm_tests
```

Scripts no longer check - they trust Bazel validated the environment.

## Why This Split?

### Bazel Philosophy

**Bazel should**:
- Provide hermetic tools (binaries, libraries)
- Validate environment (via tests)
- Fail fast with clear errors
- Reproducible builds

**Bazel should NOT**:
- Start system daemons
- Modify kernel modules
- Change user permissions
- Assume root access

### Xibalba Reality

VM testing needs:
- ✅ Hermetic: Xibalba binaries, tools
- ❌ Non-hermetic: libvirtd, KVM, permissions

We handle both properly:
- Hermetic parts: Bazel builds
- System parts: Documented, validated via test

## Future: Full Hermetic

If we implement Phase 2 (hermetic tools), only system prerequisites remain:
- libvirtd daemon
- KVM kernel module  
- User permissions

Everything else (cloud-localds, qemu-img, etc.) becomes hermetic and Bazel-provided.

This is the **right Bazel architecture** for system-level testing! 🚀

