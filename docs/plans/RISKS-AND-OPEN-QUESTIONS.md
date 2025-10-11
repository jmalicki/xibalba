# Xibalba: Risks and Open Questions

*Critical issues to address before implementation*
*Date: October 10, 2025*

## Quick Status

### ✅ Major Issues RESOLVED (Thanks to VM Control):
- ✅ **Kernel configuration** - Can build kernel with any config needed
- ✅ **eBPF support** - Can enable CONFIG_BPF_KPROBE_OVERRIDE in custom kernel
- ✅ **Hardware resources** - 32GB RAM, 500GB disk confirmed available
- ✅ **VM technology** - QEMU/KVM with libvirt (already in plan)
- ✅ **Shared machine** - Can use overnight for long benchmarks

### ⚠️ Remaining Open Questions (4 critical):
1. **Kernel source**: Where is your kernel git tree? Which version?
2. **io_uring opcode**: Does IORING_OP_GETDENTS exist in your kernel?
3. **MVP scope**: Start with 3 filesystems or all 5?
4. **Timeline**: 12-16 weeks acceptable? Full-time or part-time?

**Bottom line**: Most risks eliminated by VM control. Only need to clarify scope and kernel source location.

---

## Overview

This document outlines **risks, open questions, and decisions needed** before starting Xibalba implementation. Address these early to avoid costly mid-project pivots.

**Update**: Using VMs with custom kernels **eliminates most risks**! This document now focuses on actual remaining questions.

---

## Category 1: Kernel & eBPF Compatibility

### ✅ RESOLVED: Complete Kernel Control via VMs

**KEY INSIGHT**: Since we're using VMs, we have **complete control** over kernel configuration!

**What this means**:
- ✅ Can enable `CONFIG_BPF_KPROBE_OVERRIDE=y` - just build with this flag
- ✅ Can enable BTF generation - just install pahole and build with CONFIG_DEBUG_INFO_BTF=y
- ✅ Can use any kernel version we want (6.8+, 6.10+, whatever)
- ✅ Can test multiple kernel versions side-by-side in different VMs
- ✅ Can rebuild kernel with different configs for different tests

**What you'll do**:
```bash
# In VM setup phase:
# 1. Clone kernel source (your branch with async getdents patches)
# 2. Configure with required eBPF options:
make menuconfig
  # Enable:
  # - CONFIG_BPF_KPROBE_OVERRIDE=y
  # - CONFIG_DEBUG_INFO_BTF=y
  # - CONFIG_DEBUG_INFO=y
  # - CONFIG_FTRACE=y

# 3. Build kernel
make -j$(nproc) bindeb-pkg

# 4. Install in VMs
# VMs get .deb packages, install with dpkg

# Done! Every VM has exactly the kernel you need
```

**Remaining considerations** (minor):
- **Kernel build time**: ~30-60 minutes per build (but only once per kernel version)
- **Disk space**: Kernel source + build artifacts = ~20GB (manageable)
- **Multiple versions**: Can test "before patch" vs "after patch" by swapping kernels

**Severity**: ~~HIGH~~ → **NOT A RISK** - Completely under your control

---

### ✅ SIMPLIFIED: Kernel Symbol Availability

**Issue**: eBPF hooks depend on kernel function names

**Resolution**: Since you control the kernel source:
- ✅ You know exactly which functions exist (you're looking at the source!)
- ✅ Can grep for function names: `grep -r "vfs_getdents_async" fs/`
- ✅ If testing patches you're developing, function names are in your patches

**Only matters if**: Testing multiple kernel versions with different APIs

**Mitigation**: 
- Document which kernel version(s) each test targets
- Use conditional eBPF if supporting multiple versions

**Severity**: ~~MEDIUM~~ → **LOW** - You control the source

---

## Category 2: VM Infrastructure

### ✅ RESOLVED: Resource Requirements

**Status**: User confirms resources available!
- ✅ 32GB+ RAM available
- ✅ 500GB+ disk space available
- ✅ Shared dev machine (not a server)

**Implications**:
```
Shared dev machine means:
- ✅ Can run VMs for testing
- ⚠️ Might need to run tests overnight for long runs
- ⚠️ Can run 1-2 VMs concurrently during day
- ✅ Can run all 5 VMs overnight for comprehensive tests
```

**VM resource allocation** (conservative for shared machine):
```
During development (daytime):
- 1-2 VMs running: 8-16GB RAM used
- Quick iteration: test on ext4 VM primarily
- Other VMs stopped

Overnight/comprehensive testing:
- All 5 VMs running: 20-24GB RAM used
- Full test suite across all filesystems
- Chaos tests with long durations (hours)
```

**Severity**: ~~HIGH~~ → **RESOLVED** - Hardware available, just schedule appropriately

---

### ✅ CLARIFIED: VM Technology Stack

**Technology**: QEMU/KVM with libvirt management

**What this means**:
```
QEMU/KVM:
- QEMU: Virtual machine emulator
- KVM: Kernel Virtual Machine (hardware acceleration)
- Together: Fast, native-speed virtualization

libvirt:
- Management layer on top of QEMU/KVM
- Provides: virsh, virt-install, networking
- What the scripts use (virsh commands throughout plan)

Full stack:
  libvirt tools (virsh, virt-install)
        ↓
  libvirtd daemon
        ↓
  QEMU/KVM
        ↓
  Hardware (CPU virtualization extensions)
```

**Already in the plan**:
- All scripts use `virsh` (libvirt CLI)
- VMs created with `virt-install` (libvirt)
- Images are `.qcow2` format (QEMU)
- Networking via libvirt default network

**No changes needed** - plan already assumes QEMU/KVM via libvirt!

---

### Risk: VM Network Configuration

**Issue**: VMs need networking for SSH access and test deployment

**Questions**:
- [ ] Is libvirt default network configured?
- [ ] Can VMs reach the internet (for apt packages)?
- [ ] Can host SSH into VMs?
- [ ] Firewall rules blocking anything?

**Check**:
```bash
virsh net-list --all
# Should show 'default' network as active

virsh net-info default
# Should show bridge and IP range
```

**Severity**: MEDIUM - Common issue, well-documented solutions

---

### ✅ SIMPLIFIED: Kernel Installation in VMs

**Approach**: Build on host, deploy .deb packages to VMs

**Workflow**:
```bash
# On host machine:
cd /path/to/kernel/source
make -j$(nproc) bindeb-pkg
# Produces: linux-image-*.deb, linux-headers-*.deb

# Copy to VMs:
for vm in ext4 xfs zfs btrfs tmpfs; do
    VM_IP=$(virsh domifaddr async-getdents-$vm | awk ...)
    scp linux-*.deb test@$VM_IP:/tmp/
    ssh test@$VM_IP "sudo dpkg -i /tmp/linux-*.deb && sudo reboot"
done
```

**Advantages**:
- ✅ Build once, deploy to all VMs
- ✅ .deb format handles GRUB config automatically
- ✅ Clean install/uninstall with dpkg
- ✅ Can keep multiple kernel versions installed

**UEFI Secure Boot**: Disable in VMs (we control the environment)

**Severity**: ~~MEDIUM~~ → **LOW** - Well-understood process

---

## Category 3: Testing Actual Kernel Changes

### ✅ CLARIFIED: Kernel Patch Workflow

**Your workflow** (now clear with VMs):

```bash
# 1. Develop kernel patches (on host)
cd ~/kernel-dev/linux
git checkout -b async-getdents-v1
# ... make changes to fs/readdir.c, io_uring/getdents.c, etc ...
git commit -m "Add async getdents support"

# 2. Build kernel with patches
make -j$(nproc) bindeb-pkg

# 3. Create VMs with custom kernel
bazel run //vm:create_vm -- --name test-01 --kernel /path/to/vmlinuz

# 4. Deploy and run tests
bazel run //vm:deploy_xibalba -- test-01
bazel run //vm:run_tests -- test-01

# 5. Analyze results, iterate
# If bugs found, fix patches, rebuild, redeploy, retest
```

**Baseline comparison**:
```bash
# Option 1: Multiple VMs
- VM set A: baseline kernel (no patches)
- VM set B: patched kernel
- Run identical tests on both, compare

# Option 2: Kernel swapping (same VMs)
- Install baseline kernel: dpkg -i linux-baseline.deb
- Run tests, save results
- Install patched kernel: dpkg -i linux-patched.deb  
- Run tests, compare results

# Option 3: VM snapshots (recommended)
- Create snapshot "baseline-kernel"
- Install patched kernel
- Run tests
- Revert to "baseline-kernel" snapshot
- Run tests
- Compare
```

**Severity**: ~~HIGH~~ → **NOT A RISK** - Standard kernel dev workflow

---

## Category 4: Fault Injection Efficacy

### Risk: Will eBPF Fault Injection Actually Find Bugs?

**Issue**: No guarantee that injecting faults will trigger bugs

**Questions**:
- [ ] Have you validated eBPF fault injection works on simpler code first?
- [ ] What's the success metric? (X bugs found, Y% coverage?)
- [ ] How long do chaos tests need to run to find races?

**Reality check**:
- Race conditions are probabilistic - may need millions of iterations
- Pauses help but don't guarantee finding all races
- Some bugs only appear under specific timing

**Mitigation**:
- Start with known bugs/regressions to validate framework
- Increase fault injection rates progressively
- Run tests for extended periods (hours/days)
- Use Thread Sanitizer (TSAN) in addition to chaos testing

**Severity**: MEDIUM - Framework might produce false negatives

**Decision needed**: What's your acceptance criteria for "working"?

---

### Risk: eBPF Overhead Distorting Results

**Issue**: eBPF hooks add overhead that might mask or create race conditions

**Questions**:
- [ ] How much overhead is acceptable?
- [ ] Need baseline performance measurements?
- [ ] Should some tests run without eBPF for comparison?

**Measurement needed**:
```bash
# Run without eBPF
bazel test //chaos:rapid_modifications --test_arg=--no-ebpf

# Run with eBPF
bazel test //chaos:rapid_modifications --test_arg=--with-ebpf

# Compare: If >5% difference, overhead too high?
```

**Severity**: LOW - eBPF overhead is typically <5%

---

## Category 5: Build System & Dependencies

### Risk: Bazel for eBPF Compilation

**Issue**: Bazel isn't commonly used for eBPF development

**Questions**:
- [ ] Do you have experience with Bazel + eBPF?
- [ ] Are there Bazel rules for BPF compilation?
- [ ] Would Makefiles be simpler for eBPF parts?

**Current plan**:
```python
# genrule for eBPF (might be fragile)
genrule(
    name = "ebpf_obj",
    srcs = ["program.bpf.c"],
    cmd = "clang -target bpf -O2 -c $< -o $@",
)
```

**Alternative**:
- Use Makefiles for eBPF, Bazel for C code
- Shell script wrappers called from Bazel
- Look for existing Bazel BPF rules

**Severity**: LOW-MEDIUM - Workable but might need iteration

**Decision needed**: Hybrid build system or pure Bazel?

---

### Risk: liburing Version Compatibility

**Issue**: io_uring features depend on liburing version

**Questions**:
- [ ] Which liburing version are you targeting? (2.0+?)
- [ ] Does `IORING_OP_GETDENTS` exist in your liburing?
- [ ] If not, are you adding it yourself?

**Check**:
```bash
pkg-config --modversion liburing
# Need 2.x or newer

# Check for IORING_OP_GETDENTS
grep IORING_OP_GETDENTS /usr/include/liburing/io_uring.h
```

**Severity**: HIGH - Core dependency for testing

**Decision needed**: 
- Custom liburing build?
- Wait for upstream support?
- Mock the interface for testing?

---

### Risk: Dependency Management

**Issue**: VMs need consistent package versions

**Questions**:
- [ ] How to ensure all VMs have same versions?
- [ ] Use distro packages or build from source?
- [ ] Pin versions or track latest?

**Suggestion**: Create requirements manifest
```yaml
# xibalba-dependencies.yaml
kernel: "6.8.0-custom"
liburing: "2.4"
bpftool: "7.0.0"
clang: "16.0.0"
python: "3.11"
```

**Severity**: MEDIUM - Important for reproducibility

---

## Category 6: ZFS-Specific Issues

### Risk: ZFS Licensing (CDDL vs GPL)

**Issue**: OpenZFS is CDDL-licensed, potential conflicts with GPL kernel

**Questions**:
- [ ] Are you comfortable with ZFS licensing situation?
- [ ] Do you need ZFS, or can you test 4 filesystems?
- [ ] Distribution concerns if sharing VM images?

**Reality**:
- OpenZFS works fine on Linux in practice
- Most distros ship it (Ubuntu, Debian)
- Only an issue if you redistribute kernel modules

**Mitigation**:
- Document that ZFS is optional
- Provide instructions for users to install ZFS themselves
- Don't bundle ZFS modules in distributed VM images

**Severity**: LOW - Mostly legal/policy, not technical

**Decision needed**: Include ZFS or drop to 4 filesystems?

---

### Risk: ZFS Complexity

**Issue**: ZFS is significantly more complex than other filesystems

**Questions**:
- [ ] Do you have ZFS expertise?
- [ ] Worth the testing effort vs ext4/XFS?
- [ ] Specific ZFS features you want to test?

**Consideration**: 
- ZFS has its own caching (ARC), different from page cache
- ZFS directory iteration is very different from ext4/XFS
- May need ZFS-specific tests

**Severity**: LOW - Can defer ZFS to Phase 2

**Decision needed**: ZFS in MVP or later?

---

## Category 7: Scope & Timeline

### Risk: Timeline Optimism

**Issue**: 8-12 weeks is very aggressive

**Reality check**:
```
Phase 1 (DirectoryReader):     1 week    ✓ Reasonable
Phase 2 (Chaos framework):     1-2 weeks ✓ Reasonable
Phase 3 (eBPF):                2-3 weeks ⚠️ Could be longer
Phase 4 (VMs - base):          2-3 days  ✓ Reasonable
Phase 5 (VMs - 5 filesystems): 3-5 days  ⚠️ Assumes no issues
Phase 6 (Test data):           2-3 days  ✓ Reasonable
Phase 7 (Orchestration):       2-3 days  ⚠️ Assumes no bugs
Phase 8 (Integration):         3-5 days  ⚠️ Always takes longer
Phase 9 (Validation):          1-2 days  ⚠️ Will find issues

Total: 8-12 weeks (optimistic)
Realistic: 12-16 weeks with debugging
```

**Risk factors**:
- eBPF learning curve (if new to it)
- VM issues (networking, kernel boot problems)
- Bug fixing in framework itself
- Iterating on fault injection strategies

**Severity**: MEDIUM - Won't block progress but affects expectations

**Decision needed**: Is this a hard deadline or best-effort estimate?

---

### Risk: Scope Creep

**Issue**: Many "nice to have" features that could expand scope

**Temptations**:
- ❌ Add more filesystems (F2FS, NILFS2...)
- ❌ Add more fault types (network delays, disk corruption...)
- ❌ Build web UI for results
- ❌ Integrate with CI/CD
- ❌ Add machine learning for bug prediction
- ❌ Make it production-ready for general use

**Mitigation**: Define MVP clearly
```
MVP (Minimum Viable Product):
✅ DirectoryReader abstraction
✅ Basic chaos test (rapid modifications)
✅ Basic eBPF fault injection (pauses + EAGAIN)
✅ 2-3 filesystems (ext4, XFS, tmpfs)
✅ Manual test execution
✅ Text-based results

Phase 2 (after MVP works):
⏭️ Advanced fault injection
⏭️ All 5 filesystems
⏭️ Automated orchestration
⏭️ HTML reports
⏭️ CI integration
```

**Severity**: HIGH - Scope creep is the #1 project killer

**Decision needed**: Define MVP boundary clearly

---

## Category 8: Testing Philosophy

### Risk: False Sense of Security

**Issue**: Tests passing doesn't mean code is bug-free

**Questions**:
- [ ] What's the goal? (Find bugs vs prove correctness)
- [ ] How many bugs is "enough" to validate framework?
- [ ] What if no bugs are found? (Framework broken or code correct?)

**Reality**:
- Chaos testing finds bugs probabilistically
- May need to run for days/weeks to find rare races
- Absence of evidence ≠ evidence of absence

**Mitigation**:
- Start with known regressions (prove framework works)
- Set clear exit criteria (e.g., "run 1M iterations without crash")
- Combine with other tools (KASAN, UBSAN, Lockdep)

**Severity**: MEDIUM - Philosophical but important

---

### Risk: Reproducing Bugs

**Issue**: Finding a bug once isn't enough - need reproducibility

**Questions**:
- [ ] How much history/logging to keep?
- [ ] Can you replay specific fault sequences?
- [ ] Disk space for detailed logs?

**Suggestion**:
```bash
# Save complete history on failure
if chaos_test fails:
    save_history_to: test-results/failure-$(date)-$(random).json
    save_vm_state: snapshots/failure-$(date).qcow2
    save_ebpf_logs: /sys/kernel/debug/tracing/trace
```

**Severity**: MEDIUM - Critical for debugging but impacts performance

**Decision needed**: How much observability do you need?

---

## Category 9: Practical Considerations

### Risk: Development Environment

**Issue**: Where will you actually develop this?

**Questions**:
- [ ] Local machine, remote server, or cloud?
- [ ] Can you run VMs on dev machine or separate box?
- [ ] Access to physical hardware or all virtual?
- [ ] Root access available for eBPF loading?

**Requirements**:
```
For development:
- Root access (eBPF loading)
- KVM support (VM testing)
- Modern kernel (5.10+ for eBPF)
- Good network connectivity (downloading cloud images)

For CI/CD (future):
- Self-hosted runner with KVM
- Bare metal or nested virtualization
- Significant resources
```

**Severity**: MEDIUM - Affects daily workflow

---

### Risk: Debugging eBPF Programs

**Issue**: eBPF debugging is notoriously difficult

**Questions**:
- [ ] Familiar with bpftool and bpf_trace_printk()?
- [ ] Know how to read /sys/kernel/debug/tracing/trace_pipe?
- [ ] Have llvm-objdump for disassembly?

**Tools needed**:
```bash
# Essential eBPF debugging
bpftool prog list          # Show loaded programs
bpftool map dump           # Inspect maps
cat /sys/kernel/debug/tracing/trace_pipe  # See printk output
llvm-objdump -S program.bpf.o  # Disassemble

# Advanced
bpftrace -l                # List available tracepoints
```

**Mitigation**:
- Start with simple eBPF programs
- Use bpf_trace_printk() liberally
- Test eBPF separately before integrating

**Severity**: MEDIUM - Learning curve but manageable

---

## Category 10: Maintenance & Evolution

### Risk: Long-Term Maintenance

**Issue**: Who maintains this after initial development?

**Questions**:
- [ ] Is this a one-time effort or ongoing?
- [ ] Will you update as kernel APIs change?
- [ ] Who fixes broken tests?
- [ ] Documentation for future users?

**Maintenance burden**:
- VM images need updates (security patches)
- eBPF programs break with kernel changes
- Test harness needs bug fixes
- Dependencies need version bumps

**Mitigation**:
- Good documentation from day 1
- Automated VM rebuilds
- Version pin everything
- Plan for 20% maintenance time

**Severity**: LOW initially, MEDIUM long-term

---

## Summary: Actual Remaining Risks (VM Control Simplifies Everything!)

### ✅ RESOLVED by VM Control:

These are **NOT risks** because you control the VM environment:
- ~~Kernel configuration~~ → Build with exactly the config you need
- ~~eBPF feature availability~~ → Enable CONFIG_BPF_KPROBE_OVERRIDE
- ~~BTF availability~~ → Generate during kernel build
- ~~Kernel version compatibility~~ → Choose any version you want
- ~~Module signing~~ → Disable in VMs
- ~~Kernel installation~~ → Standard .deb workflow

### ⚠️ ACTUAL RISKS That Remain:

**1. ✅ Host machine resources** - RESOLVED!
   - ✅ 32GB+ RAM available
   - ✅ 500GB+ disk space available
   - ✅ Shared dev machine (use overnight for long benchmarks)
   - [ ] Verify KVM/libvirt working: `kvm-ok && virsh list`

**2. Scope definition** (IMPORTANT):
   - [ ] Define MVP: 3 filesystems (ext4, XFS, tmpfs) or all 5?
   - [ ] Define success criteria: what makes this "done"?
   - [ ] Define timeline: 12-16 weeks acceptable?

**3. Kernel source availability** (SIMPLE):
   - [ ] Do you have kernel git tree with your patches?
   - [ ] Which kernel version are you starting from? (6.8+, 6.10+?)
   - [ ] Are you developing async getdents patches, or testing existing code?

**4. liburing/io_uring opcode** (IMPORTANT):
   - [ ] Does `IORING_OP_GETDENTS` exist in your kernel?
   - [ ] If not, are you adding it yourself?
   - [ ] Or should DirectoryReader mock it / use syscalls initially?

**5. eBPF skill level** (MEDIUM):
   - [ ] Comfortable with eBPF development?
   - [ ] Or learning as you go? (add 2-3 weeks)

**Shared machine considerations**:
```bash
# Daytime development (low resource usage)
bazel run //vm:create_vm -- --name test-ext4  # Just one VM
bazel test //common:test_dir_reader           # Quick tests

# Overnight comprehensive testing
# Create all VMs
for fs in ext4 xfs btrfs tmpfs; do
  bazel run //vm:create_vm -- --name test-$fs --filesystem $fs
done

# Run long tests in parallel
for vm in test-{ext4,xfs,btrfs,tmpfs}; do
  bazel run //vm:run_tests -- --duration 8hours $vm > results-$vm.txt &
done

# Come back in morning to results
```

### Recommended Approach:

**Week 1-2**: Build minimal proof of concept
- DirectoryReader abstraction (classic only, skip io_uring initially)
- One simple chaos test
- Manual execution on local filesystem
- **Goal**: Validate approach before investing in VMs/eBPF

**Week 3-4**: Add eBPF (if PoC works)
- Simple eBPF program (just logging, no faults)
- Confirm it loads and runs
- **Goal**: Validate eBPF toolchain

**Week 5-8**: Scale up (if both above work)
- Add io_uring to DirectoryReader
- Add VM infrastructure (1-2 filesystems first)
- Add fault injection
- **Goal**: Full MVP

**Week 9-12**: Polish and extend
- Add remaining filesystems
- Improve fault injection
- Automation and reporting
- **Goal**: Production-ready framework

---

## Open Questions for Discussion

### Critical Questions (Must Answer Before Starting):

1. **✅ Hardware check** - RESOLVED!
   - ✅ 32GB+ RAM on host
   - ✅ 500GB+ disk space
   - ✅ Shared dev machine (use overnight for long benchmarks)
   - [ ] Just verify KVM: `kvm-ok && virsh list`

2. **Kernel situation**:
   - Do you have kernel source with async getdents patches?
   - Or are you developing these patches as you go?
   - Which kernel version? (recommend 6.8+ or 6.10+)

3. **io_uring opcode status**:
   - Does `IORING_OP_GETDENTS` exist in your kernel already?
   - If not, is adding it part of your scope?
   - Or should DirectoryReader use syscalls instead initially?

4. **MVP scope**:
   - 3 filesystems (ext4, XFS, tmpfs) or all 5?
   - With eBPF or defer eBPF to Phase 2?
   - Recommended: Start with 3 filesystems, basic eBPF

5. **Timeline expectations**:
   - Is 12-16 weeks acceptable?
   - Full-time or part-time effort?
   - Hard deadline or best-effort?

### Nice-to-Know Questions (Can Answer Later):

6. **Public vs private**: Open source or internal tool?

7. **Existing bugs**: Any known regressions to validate framework against?

8. **eBPF experience**: Comfortable with eBPF or learning as you go?

9. **ZFS**: Include in MVP or defer?

10. **Success criteria**: What proves the framework works?

---

## Risk Mitigation Priority

### ✅ Already Resolved (VM Control + Hardware Confirmed):
1. ✅ Kernel configuration (CONFIG_BPF_KPROBE_OVERRIDE, BTF, etc.) → **Build custom kernel**
2. ✅ Hardware resources → **32GB RAM, 500GB disk confirmed**
3. ✅ VM technology → **QEMU/KVM with libvirt (already in plan)**
4. ✅ Kernel installation → **Build .deb packages, deploy via dpkg**

### ⚠️ Must Address Before Phase 1:
1. **Kernel source location**: Where is your kernel git tree with patches?
2. **io_uring opcode**: Does IORING_OP_GETDENTS exist, or are you adding it?
3. **MVP scope**: 3 filesystems (faster) or all 5 (comprehensive)?
4. **Timeline**: Is 12-16 weeks acceptable? Full-time or part-time?

### 🔧 Address During Phase 1-2:
5. Validate eBPF toolchain works (Phase 3)
6. Test kernel build/deploy to VMs (Phase 4)
7. Prove fault injection concept (Phase 3)

### 🎯 Address During Phase 3+:
8. Optimize fault injection strategies
9. Add remaining filesystems (if starting with 3)
10. Build full automation

---

## Final Summary: What You Need to Decide

**The good news**: VMs + custom kernel control = most problems solved! ✅

**The questions**:

### 1️⃣ What are you testing?

**Option A: Testing patches you're developing**
```bash
# You're writing async getdents code
# Xibalba validates it works correctly
# Typical use: iterate on patches based on Xibalba findings
```

**Option B: Testing existing kernel features**
```bash
# Kernel already has the feature
# Xibalba validates stability/correctness
# Typical use: confidence before production deployment
```

**👉 Which one?**

---

### 2️⃣ What's your MVP?

**Conservative MVP** (8-10 weeks):
- ✅ DirectoryReader (classic + basic io_uring)
- ✅ Basic chaos test (concurrent read/write)
- ✅ Simple eBPF (logging first, then basic pauses)
- ✅ **3 filesystems** (ext4, XFS, tmpfs)
- ✅ Manual test execution
- ⏭️ Defer: ZFS, btrfs, automation, fancy reports

**Ambitious MVP** (12-16 weeks):
- ✅ Everything above, plus:
- ✅ **All 5 filesystems** (add ZFS, btrfs)
- ✅ Advanced eBPF (fault injection, adaptive)
- ✅ Full automation (`./run_all_tests.sh`)
- ✅ HTML reports

**👉 Which approach?** (Recommend: Conservative for first iteration)

---

### 3️⃣ Timeline & Commitment

- **Full-time** (40 hrs/week): 8-12 weeks realistic
- **Part-time** (20 hrs/week): 16-24 weeks realistic
- **Occasional** (10 hrs/week): 6-12 months

**👉 What's your commitment level?**

---

### 4️⃣ io_uring opcode availability

**If IORING_OP_GETDENTS exists**:
- ✅ DirectoryReader can use it directly
- ✅ Tests validate real io_uring path
- ✅ Straightforward

**If it doesn't exist yet**:
- Option A: Add the opcode yourself (adds scope)
- Option B: Mock it via syscall wrapper (simpler for testing)
- Option C: Focus on classic readdir() only initially

**👉 What's the status?**

---

## Next Steps

**Once you answer these 4 questions**:
1. Update IMPLEMENTATION-PLAN.md with your specific choices
2. Start Phase 0 (prerequisites)
3. Begin implementation!

**Ready to start?** Answer the questions above, then dive into the implementation plan.

---

*Xibalba: The path is clear, just need to choose your route through it.* ⚡

