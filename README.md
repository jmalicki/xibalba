# Xibalba: Chaos Testing Framework for Linux Kernel Filesystems

[![Xibalba CI](https://github.com/jmalicki/rudra-chaos/actions/workflows/ci.yml/badge.svg)](https://github.com/jmalicki/rudra-chaos/actions/workflows/ci.yml)
[![eBPF Build](https://github.com/jmalicki/rudra-chaos/actions/workflows/ebpf.yml/badge.svg)](https://github.com/jmalicki/rudra-chaos/actions/workflows/ebpf.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

*The Underworld of Trials*

**Xibalba** (shee-BAHL-bah) - The Mayan underworld where code faces its darkest trials

**Linux-specific**: This framework uses Linux eBPF for fault injection, which is not available on other kernels (BSD, Windows, macOS)

---

## What is Xibalba?

**Xibalba** is a chaos testing framework that finds subtle bugs in **Linux kernel** filesystem code—the kind that only show up when many programs access files at the same time.

### Why This Matters

**The Problem**: Imagine 100 programs all trying to list files in the same directory simultaneously. One program might see a file that doesn't exist, another might see the same file twice, and a third might miss files entirely. These bugs are **nearly impossible to find** with normal testing because they depend on exact timing—threads hitting the same code at just the right microsecond.

**Real-world impact**:
- Database corruption when `ls` misses transaction log files
- Build failures when `make` doesn't see all source files
- Backup tools skip files, leading to data loss
- Package managers install incomplete software

**Traditional testing fails** because running tests 1000 times might never hit the exact timing that triggers the bug. The bug might appear once per million operations—**in production**, not in testing.

**Xibalba's solution**: Don't wait for luck. **Force** the timing bugs to appear by:
1. Running many threads concurrently (amplify the chaos)
2. Injecting strategic delays with eBPF (widen the narrow timing windows)
3. Validating that every read operation is correct (catch the bugs)

If your code passes Xibalba's tests, it can handle the chaos of real production systems.

### Technical Details

**Xibalba** is a [Jepsen](https://jepsen.io/)-inspired chaos testing framework specifically designed for testing concurrent filesystem operations in the **Linux kernel** using eBPF fault injection.

**Linux-only**: eBPF is a Linux kernel feature. Other operating systems (FreeBSD, Windows, macOS) don't have equivalent fault injection capabilities at the kernel level.

**Inspired by**: [Kyle Kingsbury](https://aphyr.com/)'s [Jepsen framework](https://github.com/jepsen-io/jepsen) for distributed systems

**Novel approach**: Brings Jepsen-quality chaos engineering to Linux kernel filesystem testing using eBPF

---

## The Name: Xibalba

**[Xibalba](https://en.wikipedia.org/wiki/Xibalba)** (shee-bahl-BAH) — literally **"Place of Fear"** or **"Place of Phantoms"** — is the Maya underworld from the *[Popol Vuh](https://en.wikipedia.org/wiki/Popol_Vuh)*, the sacred K'iche' Maya text. Far below the surface world, Xibalba is a realm of darkness ruled by the **Ajawab' Xib'alb'a** (Lords of Xibalba), death gods who delight in tormenting souls through elaborate trials and deadly games.

### The Myth: The Hero Twins' Descent

The most famous story of Xibalba tells of the Hero Twins—**Hunahpu** and **Xbalanque**—who were summoned to the underworld by its jealous lords. Their father and uncle had been killed there before them, defeated by the trials. The twins descended knowing they faced almost certain death, but they were clever, brave, and determined to avenge their ancestors.

To escape Xibalba alive, they had to survive the **Six Houses of Xibalba**, each a deadly ordeal designed by the Lords of Death:

- 🌑 **The Dark House** (*Ch'umil Ha*) — Absolute darkness where you cannot see the dangers that surround you
- ❄️  **The Cold House** (*Shiqiripat*) — Freezing winds and icy hail that drain all warmth and will
- 🐆 **The Jaguar House** (*Balam Ha*) — Prowling jaguars with razor fangs, hungry and waiting
- 🦇 **The Bat House** (*Sotz' Ha*) — Giant death bats with obsidian wings that decapitate victims
- 🗡️  **The Razor House** (*Chamiabak*) — Rooms filled with moving blades of obsidian and flint
- 🔥 **The Fire House** (*Kaqix Ha*) — Unbearable flames that consume everything within

Each house represented a different form of death—darkness, cold, beasts, blades, heat. The lords expected the twins to fail, as all others had. But through **cunning, resilience, and ingenuity**, Hunahpu and Xbalanque survived every trial. They eventually defeated the Lords of Death in a ritual ball game, died and were reborn, and ascended from Xibalba as gods—the Sun and the Moon.

### The Metaphor: Your Code's Descent into Chaos

Like the Hero Twins descending into Xibalba, **your code must face orchestrated chaos designed to expose every weakness.** Each test is a House of Xibalba—an ordeal that seems designed to make your code fail:

- 🌑 **The Dark House** → Race conditions in darkness (timing you can't see)
- ❄️  **The Cold House** → Delay injection (operations frozen at critical moments)
- 🐆 **The Jaguar House** → Resource contention (threads competing like hungry jaguars)
- 🦇 **The Bat House** → Concurrent modifications (sudden, deadly changes)
- 🗡️  **The Razor House** → Edge cases (cuts that find every boundary condition)
- 🔥 **The Fire House** → Stress testing (burning through resource limits)

**Just as the Hero Twins couldn't see in the Dark House**, your code faces race conditions where timing is invisible. **Just as they shivered in the Cold House**, your operations are frozen by strategic delays. **Just as jaguars prowled**, your threads compete for scarce resources.

But if your code survives—like the Hero Twins—it doesn't just work. It has been **battle-tested against chaos itself**. It emerges not as a fragile prototype, but as **hardened, production-ready software** worthy of the surface world.

### The Six Houses: Chaos Test Categories

| House | Trial | Test Category | What It Finds |
|-------|-------|---------------|---------------|
| 🌑 Dark House | Darkness | Race conditions | Timing bugs invisible in normal testing |
| ❄️ Cold House | Freezing | Delay injection | Timeout handling, deadlocks |
| 🐆 Jaguar House | Beasts | Resource contention | Lock contention, starvation |
| 🦇 Bat House | Decapitation | Concurrent modifications | Lost updates, torn reads |
| 🗡️ Razor House | Blades | Edge cases | Boundary conditions, overflow |
| 🔥 Fire House | Flames | Stress testing | Resource exhaustion, memory leaks |

> *"Are they not merely wicked? They are evil, and great are their thoughts of deceit."*  
> — Popol Vuh, describing the Lords of Xibalba

**Your code enters Xibalba. Will it emerge victorious like the Hero Twins, or will the Lords of Chaos claim another victim?**

---

## Key Features

### 🌪️ **[Jepsen](https://jepsen.io/)-Style Chaos Engineering**
- Concurrent readers + writers
- 60-second torture runs
- Invariant checking
- Operation history recording

### ⚡ **eBPF Fault Injection**
- Kernel-level pause injection
- Strategic timing manipulation
- Memory allocation failures
- I/O error simulation

### 🔍 **DirectoryReader Abstraction**
- Test both classic `readdir()` and io_uring `getdents` with same code
- Automatic result comparison
- Statistics tracking

### 🖥️ **VM Infrastructure**
- 4 filesystem VMs (ext4, xfs, btrfs, tmpfs)
- Automated creation and orchestration
- One-command setup and testing
- Detailed result reports

### 📊 **History & Analysis**
- Complete operation logging
- Happens-before relationship tracking
- Minimal reproducer generation
- Timeline visualization

---

## 🚀 Getting Started

**Current Status**: ✅ Core implementation complete, eBPF fault injection working

**⚠️ IMPORTANT**: [CI and VM Testing Strategy](docs/CI-AND-VM-TESTING.md) - **Read this first!**

**Key points**:
- CI validates builds/packages (✅ automated)
- VM testing requires KVM (run locally or self-hosted)
- **Why**: We test CUSTOM KERNELS - containers can't do this!

### Two Paths Forward:

---

### **Path 1: Tech De-Risking (RECOMMENDED)** 🚀

**→ [Tech De-Risking Plan](docs/TECH-DERISKING-PLAN.md)** ⭐ **START HERE!**

**Why this first**: Validate eBPF fault injection works in 2-3 weeks before committing to 12-16 week full build

**What you'll build**:
- Minimal DirectoryReader (~200 lines)
- Simple chaos test (~100 lines)
- Basic eBPF pause injection (~50 lines eBPF + ~150 lines userspace)
- Total: ~500 lines of code

**What you'll prove**:
- ✅ eBPF toolchain works on your machine
- ✅ Can inject pauses via eBPF → userspace coordination
- ✅ Pauses increase race detection (find at least one bug)
- ✅ Approach is viable

**Investment**: 2-3 weeks | **Risk reduction**: 80%+

**Then**: If successful, proceed to full implementation with confidence!

---

### **Path 2: Full Implementation**

**→ [Full Implementation Plan](docs/IMPLEMENTATION-PLAN.md)**

**Choose this if**: You're already confident in eBPF and ready to commit 12-16 weeks

**What you'll build**: Complete Xibalba with VMs, 5 filesystems, full automation

**265 checkboxes** across 9 phases

---

### **Supporting Documents**

**Must read**:
- **⚠️ [Risks & Open Questions](docs/RISKS-AND-OPEN-QUESTIONS.md)** - Critical decisions needed

**Good news**: Most risks resolved! VMs + custom kernels = full control ✅

---

### Documentation

**Start Here**:
1. **📖 [START HERE Guide](docs/guides/START-HERE.md)** - Navigation hub ⭐
2. **⚠️ [Risks & Open Questions](docs/plans/RISKS-AND-OPEN-QUESTIONS.md)** - Critical decisions
3. **🚀 [Tech De-Risking Plan](docs/plans/TECH-DERISKING-PLAN.md)** - 2-3 week PoC (RECOMMENDED)
4. **📋 [Full Implementation Plan](docs/plans/IMPLEMENTATION-PLAN.md)** - Complete 12-16 week guide

**Recommended path**: Start with tech de-risking (2-3 weeks) to prove eBPF fault injection works, THEN commit to full implementation.

**Getting Started** (`docs/guides/`):
- **📖 [START HERE](docs/guides/START-HERE.md)** - Navigation hub ⭐
- **⚡ [Quick Start](docs/guides/QUICK-START.md)** - Run tests in 15 minutes
- **🧪 [Run Test Now](docs/guides/RUN-TEST-NOW.md)** - Two-terminal validation
- **✅ [Setup Complete](docs/guides/SETUP-COMPLETE.md)** - Initial setup verification

**Implementation Plans** (`docs/plans/`):
- **🚀 [Tech De-Risking Plan](docs/plans/TECH-DERISKING-PLAN.md)** - 2-3 week PoC (RECOMMENDED)
- **📋 [Full Implementation Plan](docs/plans/IMPLEMENTATION-PLAN.md)** - Complete 12-16 week guide
- **🗺️ [After De-Risking](docs/plans/AFTER-DERISKING.md)** - Roadmap for Weeks 4-15
- **⚠️ [Risks & Open Questions](docs/plans/RISKS-AND-OPEN-QUESTIONS.md)** - Critical decisions

**Design & Concepts** (`docs/design/`):
- **🎓 [Jepsen Principles](docs/design/JEPSEN-INSPIRED-FILESYSTEM-TESTING.md)** - Conceptual foundation
- **🔧 [Race Conditions & Fault Injection](docs/design/RACE-CONDITIONS-AND-FAULT-INJECTION.md)** - Technical details
- **🎯 [Fault Injection Scope](docs/design/FAULT-INJECTION-SCOPE.md)** - What to test vs not test
- **📊 [Testing Framework](docs/design/TESTING-FRAMEWORK.md)** - Complete specification
- **🗂️ [Filesystem Consistency Models](docs/design/FILESYSTEM-CONSISTENCY-MODELS.md)** - 10+ filesystems documented
- **📝 [State Tracking & Validation](docs/design/STATE-TRACKING-AND-VALIDATION.md)** - Ground truth approach

**Status & Progress** (`docs/status/`):
- **📈 [Tech De-Risking Status](docs/status/TECH-DERISKING-STATUS.md)** - Current progress
- **🎉 [Completed Today](docs/status/COMPLETED-TODAY.md)** - Day 1 achievements

---

### Prerequisites

**Host Machine**:
- Linux host with KVM support
- 8GB+ RAM (16GB+ recommended for parallel VM testing)
- 50GB+ disk space (for VM images)
- Ubuntu 22.04 or later

**System Prerequisites** (one-time install):
```bash
sudo apt install -y libvirt-daemon-system qemu-kvm virtinst cloud-image-utils
sudo usermod -aG libvirt,kvm $USER  # No more sudo needed after this!
```

**Bazel validates automatically** when you run VM tests (no separate step needed!)

**Kernel Development**:
- Kernel source tree (with your patches or baseline)
- Build environment (gcc, make, pahole for BTF)
- Will build custom kernels with eBPF support enabled

**VM Technology**: QEMU/KVM managed via libvirt
- Fast, hardware-accelerated virtualization
- Standard Linux VM tooling (virsh, virt-install)

### Quick Start

```bash
# Clone repository
git clone https://github.com/your-org/xibalba.git
cd xibalba

# (Optional but recommended) Setup pre-commit hooks for local linting
# Requires: pipx or pip install pre-commit
# pre-commit install  # Catches shellcheck/formatting issues before commit
# Note: CI runs shellcheck anyway, so this is optional

# Build everything
bazel build //...

# Grant eBPF capabilities (one-time, requires sudo)
sudo ./grant_caps.sh

# Run chaos test
mkdir -p /tmp/xibalba_test
touch /tmp/xibalba_test/file{1..100}
bazel run //chaos:simple_chaos_test -- /tmp/xibalba_test

# Run with eBPF fault injection (in separate terminal)
bazel run //chaos:pause_controller -- 50 11
```

### VM Testing

**Step 1 - One-Time System Setup** (install prerequisites):
```bash
# Install system services and tools
sudo apt install -y libvirt-daemon-system qemu-kvm virtinst cloud-image-utils

# Grant yourself VM permissions (no more sudo needed!)
sudo usermod -aG libvirt,kvm $USER

# Enable KVM module (for AMD CPUs)
echo "kvm-amd" | sudo tee /etc/modules-load.d/kvm.conf
sudo modprobe kvm-amd
# For Intel CPUs use: kvm-intel

# Fix Bazel cache permissions for libvirt
sudo chmod o+x /home/$USER/.cache

# Create libvirt network for user session
virsh --connect qemu:///session net-define /dev/stdin <<'EOF'
<network>
  <name>default</name>
  <forward mode='nat'/>
  <bridge name='virbr1' stp='on' delay='0'/>
  <ip address='192.168.124.1' netmask='255.255.255.0'>
    <dhcp>
      <range start='192.168.124.2' end='192.168.124.254'/>
    </dhcp>
  </ip>
</network>
EOF
sudo virsh --connect qemu:///session net-start default
virsh --connect qemu:///session net-autostart default

# Log out and back in for group changes to take effect
```

**Step 2 - Verify Setup**:
```bash
# Check all prerequisites are met
bazel test //vm:verify_host_deps

# If it fails, follow the instructions in the output
```

**Step 3 - Quick Smoke Test** (recommended first):
```bash
# Fast infrastructure validation (~2 minutes)
# Validates: VM boot, package install, filesystem setup, test execution
bazel test //vm:vm_smoke_test

# Perfect for testing setup or infrastructure changes!
```

**Step 4 - Full Gauntlet** (when ready):
```bash
# Run progressive gauntlet on ext4 and ZFS (parallel)
# Automatically validates host dependencies first!
# Tests with 3 consistency models: EVENTUAL → WEAK → STRICT
bazel test //vm:parallel_vm_tests_validated

# Results saved to: test-results/parallel-vm-tests-{timestamp}/
# Duration: ~15 minutes with KVM acceleration
```

**Or validate separately first**:
```bash
# Optional: Check dependencies first
bazel test //vm:verify_host_deps
# If it fails, install missing packages (shown in test output)

# Then run tests
bazel test //vm:parallel_vm_tests_validated
```

**Or manual VM operations**:
```bash
# Create a test VM
bazel run //vm:create_vm -- --name test-01

# Deploy Xibalba to VM
bazel run //vm:deploy_xibalba -- test-01

# Run tests in VM (enter the trials!)
bazel run //vm:run_tests -- test-01

# Destroy VM when done
bazel run //vm:destroy_vm -- test-01
```

**See**: `docs/VM-PERMISSIONS.md` for detailed permission setup

### The Trials (Running Tests)

**Terminal 1 - Activate the Lords of Chaos**:
```bash
# Build first
bazel build //chaos:pause_controller

# Grant capabilities (one-time)
sudo ./grant_caps.sh

# Unleash chaos
bazel run //chaos:pause_controller -- 50 11
# Args: <probability%> <delay_iterations>
```

**Terminal 2 - Enter the Underworld**:
```bash
# Create test directory
mkdir -p /tmp/xibalba_test
touch /tmp/xibalba_test/file{1..100}

# Send your code into Xibalba
bazel run //chaos:simple_chaos_test -- /tmp/xibalba_test
```

### Building Individual Components

```bash
# Build directory reader library
bazel build //common:dir_reader

# Build chaos test
bazel build //chaos:simple_chaos_test

# Build eBPF injector and controller
bazel build //chaos:pause_controller
bazel build //chaos:pause_injector_bpf

# Build everything
bazel build //...

# Create Debian package
bazel build //packaging:xibalba-deb
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Xibalba Controller                           │
│                    (The Lords of Chaos)                         │
│                                                                 │
│  ┌───────────────┐   ┌──────────────┐    ┌──────────────┐       │
│  │DirectoryReader│   │   Nemesis    │    │   Checker    │       │
│  │  Abstraction  │   │(eBPF Faults) │    │ (Invariants) │       │
│  └───────┬───────┘   └───────┬──────┘    └───────┬──────┘       │
│          │                   │                    │             │
│          ▼                   ▼                    ▼             │
│     Operations            Faults             Validation         │
└──────────┬───────────────────┬────────────────────┬─────────────┘
           │                   │                    │
           ▼                   ▼                    ▼
┌─────────────────────────────────────────────────────────────────┐
│              The Houses of Xibalba (Test VMs)                   │
│                                                                 │
│        ┌────┐      ┌────┐      ┌─────┐      ┌─────┐             │
│        │ext4│      │xfs │      │btrfs│      │tmpfs│             │
│        └────┘      └────┘      └─────┘      └─────┘             │
│                                                                 │
│     Each with: custom kernel + test data + eBPF                 │
│     Your code must survive ALL trials to pass                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## The Trials (Test Categories)

### Trial 1: The Dark House - Race Conditions
Concurrent operations in complete chaos. Can your code maintain correctness when timing is unpredictable?

### Trial 2: The Razor House - Edge Cases
Sharp corner cases that slice through weak error handling. Directory boundaries, empty directories, massive directories.

### Trial 3: The Cold House - Resource Starvation
Memory allocation failures, ENOMEM errors. Can your code survive with nothing?

### Trial 4: The Jaguar House - Timing Attacks
Strategic delays injected via eBPF to widen race windows. The jaguars hunt for timing bugs.

### Trial 5: The Fire House - I/O Errors
Simulated I/O failures, filesystem errors. Can your code handle when everything goes wrong?

### Trial 6: The Bat House - Concurrent Mutations
Multiple writers changing the directory while you read. The death bats of concurrency.

---

## What Xibalba Tests

### ✅ The Trials Your Code Faces

- Concurrent directory iteration correctness
- Race condition resistance
- Error handling (ENOMEM, EIO, EAGAIN, EINTR)
- Lock contention behavior
- NOWAIT semantics
- Cursor validity under concurrent modifications
- Recovery from failures

### ❌ What We Don't Test (Kernel Guarantees)

- Lock correctness (assume kernel locks work)
- Data integrity (assume block layer is correct)
- Atomic operations (assume kernel provides atomicity)
- Memory safety (assume kernel doesn't corrupt memory)

**Principle**: Test YOUR code, assume kernel primitives work correctly.

---

## How It Works (Simple Explanation)

Think of it like stress-testing a bridge, but for filesystem code:

**Step 1: Create Chaos**
- Launch 10 threads all reading the same directory
- Launch 3 threads creating/deleting files
- Let them fight for 5 seconds

**Step 2: Make Bugs Appear**
- Use eBPF to inject tiny delays (microseconds) at critical moments
- These delays turn "might happen once in a million operations" into "happens every few seconds"
- It's like slow-motion replay for race conditions

**Step 3: Check for Correctness**
- Track every file that should exist
- Validate every directory read
- Catch bugs: missing files, duplicate files, phantom files

**Real Example**:
```
Without Xibalba: Test passes 999,999 times, fails once (in production)
With Xibalba:    Test fails in 30 seconds (before production)
```

**Result**: Bugs that would take months to find in production are caught in minutes during testing.

---

## Prerequisites

**Host Machine**:
- Linux host with KVM support (QEMU/KVM + libvirt)
- 32GB+ RAM recommended
- 500GB+ disk space
- Ubuntu 22.04 or later
- Shared dev machine OK (use overnight for long benchmarks)

**Kernel Development**:
- Kernel source tree (with your patches or baseline)
- Build environment (gcc, make, pahole for BTF)
- Will build custom kernels with eBPF support enabled

**VM Technology**: QEMU/KVM managed via libvirt
- Fast, hardware-accelerated virtualization
- Standard Linux VM tooling (virsh, virt-install)

---

## Status

**Current**: ✅ Core implementation complete

- ✅ DirectoryReader abstraction
- ✅ Multi-threaded chaos test
- ✅ eBPF fault injection (delay + error modes)
- ✅ Debian packaging (.deb)
- ✅ VM infrastructure (scripts ready)
- ✅ CI/CD pipeline (parallel testing across filesystems)

**Next Steps**: 
- Implement full VM automation
- Add more sophisticated invariant checkers
- Expand to test io_uring getdents
- Add history recording and analysis

---

## CI/CD

Every PR automatically faces the trials:

1. **Build Stage** - Compile all binaries, create .deb package
2. **Smoke Test** - Quick sanity check
3. **Parallel VM Trials** - 4 VMs test simultaneously:
   - ext4 filesystem
   - xfs filesystem  
   - btrfs filesystem
   - tmpfs filesystem
4. **Report** - Collect results from all trials

**Time**: ~21 minutes (4x faster than sequential)

---

## Contributing

Xibalba is actively developed. Want to help strengthen the trials?

**How to contribute**:
1. Add new fault injection modes
2. Implement additional invariant checkers
3. Create new test scenarios
4. Improve VM automation
5. Add support for more filesystems

---

## License

MIT License - see [LICENSE](LICENSE) file for details.

**Summary**: Free to use, modify, and distribute. No warranty provided.

---

## Acknowledgments

**Inspired by**:
- [Kyle Kingsbury (Aphyr)](https://aphyr.com/)'s [Jepsen framework](https://github.com/jepsen-io/jepsen) - Chaos engineering for distributed systems
- [Jepsen.io](https://jepsen.io/) database testing methodology - Rigorous invariant checking
- Linux kernel testing community - eBPF-based testing approaches

**Mythology**:
- [Xibalba](https://en.wikipedia.org/wiki/Xibalba) from the [Popol Vuh](https://en.wikipedia.org/wiki/Popol_Vuh) - The Maya creation epic
- The [Hero Twins](https://en.wikipedia.org/wiki/Maya_Hero_Twins)' trials - A metaphor for surviving through testing

---

*Xibalba: Where code faces its darkest trials and emerges battle-tested*

*"Only the strong survive the underworld. Will your code emerge victorious?"*
