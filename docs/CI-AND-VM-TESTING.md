# CI and VM Testing Strategy

**Critical Context**: Why CI can't run VMs, and what to do instead.

---

## The Core Constraint

### What Xibalba Tests

**Xibalba tests CUSTOM KERNELS** - that's the entire point!

- ✅ Testing kernel VFS patches (async getdents, io_uring changes)
- ✅ Testing different kernel configurations
- ✅ Testing kernel race conditions
- ✅ Finding kernel bugs before they reach production

**This REQUIRES real VMs with custom kernels.**

---

## Why CI Can't Run VMs

### Technical Limitation: No Nested Virtualization

**GitHub Actions hosted runners are already VMs:**

```
Physical GitHub Server
  └─ GitHub Actions Runner (VM)  ← You are here
      └─ Your Test VM?  ❌ Not supported
```

**What's missing**:
- ❌ Nested virtualization not enabled
- ❌ No KVM pass-through to containers
- ❌ No `/dev/kvm` device available
- ❌ Even with `--privileged` Docker

**This applies to**:
- Free GitHub accounts
- Paid GitHub accounts  
- All GitHub-hosted runners
- It's a GitHub platform limitation, not a permission issue

### Why Containers Don't Work

**Containers share the host kernel:**

```
GitHub Actions Runner (Kernel: 6.8.0-generic)
  └─ Docker Container
      └─ Your code runs on: 6.8.0-generic  ← Host kernel!
```

**You CANNOT**:
- ❌ Test a custom kernel (you're stuck with host kernel)
- ❌ Test kernel patches (no kernel to patch)
- ❌ Test different kernel configs (host kernel is fixed)
- ❌ Test kernel race conditions (wrong kernel)

**This defeats the entire purpose of Xibalba!**

---

## What CI DOES Test

Our CI pipeline validates the **build and packaging**:

### Stage 1: Build and Smoke Test

✅ **Bazel builds all targets**
```bash
bazel build //...
bazel build //packaging:rudra-deb
```

✅ **Creates Debian package**
```
rudra_0.1.0_amd64.deb (52KB)
Contains: pause_controller, simple_chaos_test, pause_injector.bpf.o
```

✅ **Smoke test on host**
```bash
# Quick sanity check (no eBPF, just test binary works)
bazel-bin/chaos/simple_chaos_test /tmp/test
```

✅ **Verifies eBPF program compiles**
```bash
llvm-objdump -h pause_injector.bpf.o
```

### Stage 2: VM Script Validation

✅ **Shellcheck linting** - All shell scripts are valid  
✅ **Syntax checking** - Scripts parse correctly (`bash -n`)  
✅ **Package verification** - .deb contains correct files  
✅ **Documentation check** - VM testing instructions present  

---

## What CI Does NOT Test

❌ **Actually creating VMs** (needs KVM)  
❌ **Running tests in VMs** (needs KVM)  
❌ **Testing custom kernels** (needs real VMs)  
❌ **Testing different filesystems** (needs real VMs)  
❌ **eBPF fault injection in VMs** (needs real VMs)  

**These require KVM and must be done locally or on self-hosted runners.**

---

## Where to Actually Test VMs

### Option 1: Local Testing (Recommended)

**Your development machine has KVM** ✅

```bash
# Check KVM is available
lsmod | grep kvm

# Create VM
bazel run //vm:create_vm -- --name test-01 --filesystem ext4

# Deploy and test
bazel run //vm:deploy_rudra -- test-01
bazel run //vm:run_tests -- test-01

# Test with custom kernel
bazel run //vm:create_vm -- \
  --name test-custom \
  --kernel ~/linux/arch/x86/boot/bzImage \
  --filesystem ext4
```

**This is the PRIMARY testing method.**

### Option 2: Self-Hosted GitHub Actions Runner

**Set up a dedicated machine with KVM**:

1. Install GitHub Actions runner on a machine with KVM
2. Configure it as self-hosted runner
3. Update workflow to use it:

```yaml
jobs:
  vm-tests:
    runs-on: self-hosted  # Your machine with KVM
    strategy:
      matrix:
        filesystem: [ext4, xfs, btrfs, tmpfs]
```

**When to use**: 
- Automated testing on every PR
- Team collaboration (shared test infrastructure)
- Nightly regression tests

**Cost**: Requires dedicated hardware

### Option 3: Cloud Instances with Nested Virtualization

**Providers that support nested KVM**:
- GCP: n2 instances with nested virtualization enabled
- AWS: .metal instances (bare metal)
- Azure: Dv3/Ev3 series with nested virtualization

**Example (GCP)**:
```bash
# Create instance with nested virt
gcloud compute instances create xibalba-ci \
  --enable-nested-virtualization \
  --machine-type=n2-standard-4 \
  --image-family=ubuntu-2404-lts
  
# Install GitHub Actions runner on it
# Configure as self-hosted runner
```

**Cost**: $$ (compute charges)

---

## Testing Strategy Summary

### For Individual Developers

**Primary**: Test locally
```bash
bazel run //vm:create_vm -- --name test-01
bazel run //vm:run_tests -- test-01
```

**Why**: You have KVM, it's free, it's fast.

### For CI/CD

**GitHub-hosted CI**: Build validation only
- ✅ Verify code compiles
- ✅ Create packages
- ✅ Run smoke tests
- ✅ Lint scripts

**Self-hosted runner**: Full VM testing
- ✅ Create VMs with custom kernels
- ✅ Test all filesystems
- ✅ Run full chaos tests
- ✅ Find kernel bugs

### For Teams

**Recommended setup**:

1. **Developers**: Test locally before pushing
2. **CI (GitHub-hosted)**: Validate builds don't break
3. **Nightly (Self-hosted)**: Full VM test suite on all kernels
4. **Release**: Comprehensive test on self-hosted before tagging

---

## Quick Reference

| Test Type | Where | Why |
|-----------|-------|-----|
| **Build validation** | GitHub CI | Free, fast, automated |
| **Smoke tests** | GitHub CI | Quick sanity check |
| **VM testing** | Local machine | Has KVM, immediate feedback |
| **Custom kernel testing** | Local machine | Full control |
| **Nightly regression** | Self-hosted runner | Automated, comprehensive |
| **Pre-release validation** | Self-hosted runner | Full test matrix |

---

## FAQ

### Q: Can I use GitHub Codespaces?
**A**: No, same limitation. Codespaces are VMs and don't support nested virtualization.

### Q: What about using QEMU without KVM?
**A**: Technically works but 10-100x slower. A 5-second test becomes 5 minutes. Not practical.

### Q: Can I pay GitHub for KVM support?
**A**: No, GitHub doesn't offer this on any tier. It's an architectural limitation.

### Q: Why not use containers instead of VMs?
**A**: Containers share the host kernel. We're testing **custom kernels**. Must use VMs.

### Q: Will self-hosted runners cost me?
**A**: Hardware cost only. GitHub Actions is free for self-hosted runners (even on free accounts).

### Q: Can I use GitHub's "larger runners"?
**A**: No, even 64-core runners don't have nested virtualization. It's not about size.

---

## Recommended Development Workflow

### Daily Development

```bash
# 1. Make changes
vim chaos/pause_injector.bpf.c

# 2. Build locally
bazel build //...

# 3. Test locally in VM
bazel run //vm:create_vm -- --name dev-test
bazel run //vm:deploy_rudra -- dev-test
bazel run //vm:run_tests -- dev-test

# 4. Push when tests pass
git push origin feature-branch
```

**CI will validate**:
- ✅ Build doesn't break
- ✅ Package creates successfully
- ✅ Scripts are valid

### Before Release

```bash
# Full test matrix on self-hosted runner OR locally
for fs in ext4 xfs btrfs tmpfs; do
  bazel run //vm:create_vm -- --name test-$fs --filesystem $fs
  bazel run //vm:deploy_rudra -- test-$fs
  bazel run //vm:run_tests -- test-$fs
done
```

---

## Summary

**CI = Build Validation** ✅  
**VM Testing = Local/Self-Hosted** ✅

**Why**: Testing custom kernels requires real VMs with KVM, which GitHub-hosted runners don't provide.

**This is not a limitation of Xibalba** - it's a fundamental constraint of GitHub Actions architecture.

**Solution**: Use CI for builds, use local KVM for actual testing.

