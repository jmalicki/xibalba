# RUDRA: Chaos Testing Framework for Kernel Filesystems

*The Howling Storm of Testing*

**RUDRA** - Vedic storm god, fierce form of Shiva, "The Howler"

---

## What is RUDRA?

**RUDRA** is a Jepsen-inspired chaos testing framework specifically designed for testing concurrent filesystem operations in the Linux kernel using eBPF fault injection.

**Purpose**: Find race conditions, concurrency bugs, and edge cases in kernel filesystem code **before** they reach production.

**Inspired by**: Kyle Kingsbury's Jepsen framework for distributed systems

**Novel approach**: Brings Jepsen-quality chaos engineering to kernel filesystem testing using eBPF

---

## The Name: RUDRA

**Rudra** (रुद्र) - From Vedic Hinduism:
- **The Howler**: Storm god who roars
- **The Destroyer**: Fierce aspect of Shiva
- **The Healer**: Destroys disease (bugs) to bring health (robustness)

**Acronym**: **R**ace **U**ncovering **D**irectory **R**ead **A**ssessment

**Metaphor**: Like the howling storm, RUDRA unleashes chaos on your code to reveal hidden weaknesses.

---

## Key Features

### 🌪️ **Jepsen-Style Chaos Engineering**
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
- 5 filesystem VMs (ext4, XFS, ZFS, btrfs, tmpfs)
- Automated creation and orchestration
- One-command setup and testing
- HTML result reports

### 📊 **History & Analysis**
- Complete operation logging
- Happens-before relationship tracking
- Minimal reproducer generation
- Timeline visualization

---

## 🚀 Getting Started

**Current Status**: Design complete, ready for implementation

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

**What you'll build**: Complete RUDRA with VMs, 5 filesystems, full automation

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
- **📛 [Naming Options](docs/design/NAMING-OPTIONS.md)** - Why "RUDRA"

**Status & Progress** (`docs/status/`):
- **📈 [Tech De-Risking Status](docs/status/TECH-DERISKING-STATUS.md)** - Current progress
- **🎉 [Completed Today](docs/status/COMPLETED-TODAY.md)** - Day 1 achievements

---

### Prerequisites

**Host Machine**:
- Linux host with KVM support (QEMU/KVM + libvirt)
- 32GB+ RAM ✅ (confirmed available)
- 500GB+ disk space ✅ (confirmed available)
- Ubuntu 22.04 or later
- Shared dev machine OK (use overnight for long benchmarks)

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
git clone https://github.com/your-org/rudra.git
cd rudra

# Build everything
bazel build //...

# Grant eBPF capabilities (one-time, requires sudo)
sudo ./grant_caps.sh

# Run chaos test
mkdir -p /tmp/rudra_test
touch /tmp/rudra_test/file{1..100}
bazel run //chaos:simple_chaos_test -- /tmp/rudra_test

# Run with eBPF fault injection (in separate terminal)
bazel run //chaos:pause_controller -- 50 11
```

### VM Testing

```bash
# Check VM prerequisites
bazel run //vm:check_prerequisites

# Create a test VM
bazel run //vm:create_vm -- --name test-01

# Deploy RUDRA to VM
bazel run //vm:deploy_rudra -- test-01

# Run tests in VM
bazel run //vm:run_tests -- test-01

# Destroy VM when done
bazel run //vm:destroy_vm -- test-01
```

### Running Tests

**Terminal 1 - Start eBPF Fault Injector**:
```bash
# Build first
bazel build //chaos:pause_controller

# Grant capabilities (one-time)
sudo ./grant_caps.sh

# Run injector
bazel run //chaos:pause_controller -- 50 11
# Args: <probability%> <delay_iterations>
```

**Terminal 2 - Run Chaos Test**:
```bash
# Create test directory
mkdir -p /tmp/rudra_test
touch /tmp/rudra_test/file{1..100}

# Run test
bazel run //chaos:simple_chaos_test -- /tmp/rudra_test
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
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                  RUDRA Controller                        │
│                                                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │ DirectoryReader│ │    Nemesis   │  │   Checker    │ │
│  │  Abstraction  │  │ (eBPF Faults)│  │ (Invariants) │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │
│         │                  │                 │          │
│         ▼                  ▼                 ▼          │
│    Operations          Faults           Validation      │
└─────────────────────────────────────────────────────────┘
         │                  │                 │
         ▼                  ▼                 ▼
  ┌─────────────────────────────────────────────────┐
  │              Test VMs                            │
  │  ┌────┐ ┌────┐ ┌────┐ ┌─────┐ ┌─────┐         │
  │  │ext4│ │XFS │ │ZFS │ │btrfs│ │tmpfs│         │
  │  └────┘ └────┘ └────┘ └─────┘ └─────┘         │
  │  Each with: custom kernel + test data + eBPF   │
  └─────────────────────────────────────────────────┘
```

---

## Components

### 1. DirectoryReader Abstraction
- Unified interface for classic `readdir()` and io_uring `getdents`
- Write tests once, run against both implementations
- Automatic validation they produce same results

### 2. Chaos Framework
- Concurrent reader/writer threads (20 readers + 10 writers)
- Rapid file creation/deletion
- Random delay injection
- 60-second stress runs

### 3. eBPF Nemesis (Fault Injector)
- **4 operational modes**:
  - Probabilistic (random exploration)
  - Deterministic (reproducible scenarios)
  - Adaptive (learns from bugs)
  - Adversarial (targets specific invariants)

- **Injection types**:
  - Pause injection (expand race windows)
  - Fault injection (ENOMEM, EIO, EAGAIN)
  - Timing manipulation
  - Lock contention simulation

### 4. History Recorder
- JSON operation log with nanosecond timestamps
- Tracks all operations and faults
- Enables post-mortem analysis
- Supports replay and minimization

### 5. Invariant Checkers
- No duplicate entries in single scan
- Snapshot consistency (weak POSIX model)
- Progress (liveness, no deadlocks)
- Cursor validity

### 6. VM Infrastructure
- Automated VM creation for 5 filesystems
- Test data generation and deployment
- Orchestrated test execution
- HTML reporting

---

## Documentation

### Getting Started
- `docs/IMPLEMENTATION-PLAN.md` - Step-by-step guide with 265 checkboxes

### Conceptual Design
- `docs/JEPSEN-INSPIRED-FILESYSTEM-TESTING.md` - Jepsen principles applied to filesystems
- `docs/FAULT-INJECTION-SCOPE.md` - What to test vs not test

### Technical Details
- `docs/RACE-CONDITIONS-AND-FAULT-INJECTION.md` - Fault injection mechanics
- `docs/TESTING-FRAMEWORK.md` - Complete framework specification

### Reference
- `docs/NAMING-OPTIONS.md` - Why "RUDRA"
- `docs/CHAOS-TESTING-READY.md` - Implementation readiness

---

## What RUDRA Tests

### ✅ We Test (Our Code)

- Concurrent directory iteration correctness
- Race condition resistance
- Error handling (ENOMEM, EIO, EAGAIN, EINTR)
- Lock contention behavior
- NOWAIT semantics
- Cursor validity under concurrent modifications
- Recovery from failures

### ❌ We Don't Test (Kernel Guarantees)

- Lock correctness (assume kernel locks work)
- Data integrity (assume block layer is correct)
- Atomic operations (assume kernel provides atomicity)
- Memory safety (assume kernel doesn't corrupt memory)

**Principle**: Test OUR code, assume kernel primitives work correctly.

---

## Timeline

**Optimistic**: 8-10 weeks (if everything works smoothly)

**Realistic**: 12-16 weeks (including learning curve, debugging, iteration)

**Phases**:
- Week 1-2: DirectoryReader abstraction + basic chaos
- Week 3-5: eBPF fault injection (⚠️ learning curve)
- Week 6-8: VM infrastructure (base + 3 filesystems for MVP)
- Week 9-12: Orchestration, automation, testing
- Week 13-16: Polish, add remaining filesystems, documentation

**MVP** (6-8 weeks): DirectoryReader + basic chaos + eBPF + 3 filesystems (ext4, XFS, tmpfs)

**Full system** (12-16 weeks): All 5 filesystems, complete automation, production-ready

---

## Status

**Current**: ✅ Design complete, ready to implement

**Documents**: 8,251 lines of specifications

**Checkboxes**: 265 implementation tasks

**Next**: Follow `docs/IMPLEMENTATION-PLAN.md` and start checking boxes!

---

## Why RUDRA?

**Problem**: Concurrent filesystem code has subtle race conditions

**Traditional testing**: Doesn't find timing-dependent bugs

**RUDRA approach**: 
1. Run concurrent operations (readers + writers)
2. Inject pauses at critical points (expand race windows)
3. Inject faults (test error handling)
4. Record complete history
5. Validate invariants
6. Find bugs that would take months to appear in production

**Result**: Jepsen-quality confidence for kernel filesystem code

---

## Contributing

RUDRA is currently in design phase. Implementation is starting soon.

**Want to help?**
1. Review the design documents
2. Implement components (see IMPLEMENTATION-PLAN.md)
3. Run experiments (see JEPSEN-INSPIRED-FILESYSTEM-TESTING.md)
4. Report findings

---

## License

TBD

---

## Acknowledgments

**Inspired by**:
- Kyle Kingsbury's Jepsen framework
- Aphyr's database testing methodology
- Linux kernel testing community

**Mythology**:
- Rudra from Vedic Hinduism - The Howling Storm that destroys to transform

---

*RUDRA: Where kernel code faces the storm and emerges stronger*

*"The Howler destroys bugs with fierce testing"*
