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

### **→ START HERE: [Implementation Plan](docs/IMPLEMENTATION-PLAN.md)** ⭐

**265 checkboxes** across 10 phases - follow them sequentially to build RUDRA.

---

### Quick Links

- **📋 [Implementation Plan](docs/IMPLEMENTATION-PLAN.md)** - Step-by-step guide (START HERE!)
- **🎓 [Jepsen Principles](docs/JEPSEN-INSPIRED-FILESYSTEM-TESTING.md)** - Conceptual foundation
- **🔧 [Race Conditions](docs/RACE-CONDITIONS-AND-FAULT-INJECTION.md)** - Technical details
- **📊 [Testing Framework](docs/TESTING-FRAMEWORK.md)** - Complete specification
- **🎯 [Fault Injection Scope](docs/FAULT-INJECTION-SCOPE.md)** - What to test

---

### Prerequisites

- Linux host with KVM support
- 32GB+ RAM recommended
- 500GB+ disk space
- Ubuntu 22.04 or later

### Setup (When Implemented)

```bash
# Clone repository
git clone https://github.com/your-org/rudra.git
cd rudra

# Follow the implementation plan
cat docs/IMPLEMENTATION-PLAN.md

# One-command setup (when complete)
./SETUP_ALL.sh
```

### Running Tests

```bash
# Start VMs
cd vms
./vm_control.sh start

# Run full test suite across all filesystems
./run_all_tests.sh

# View results
firefox test-results/latest/summary.html
```

### Running Specific Tests

```bash
# Test ext4 only
./run_tests_on_vm.sh async-getdents-ext4 ext4 /test/ext4

# Run chaos test with eBPF injection
sudo rudra-chaos --fs ext4 --enable-ebpf --duration 60s

# Quick comparison test (classic vs io_uring)
./test_compare_implementations /test/ext4/small_dir
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

**Full implementation**: 8-12 weeks

**Phases**:
- Week 1-2: DirectoryReader abstraction
- Week 3-4: Basic chaos framework
- Week 5-7: eBPF fault injection
- Week 8-9: VM infrastructure
- Week 10-12: Integration and polish

**Minimal viable**: 2 weeks (just abstraction layer + one VM)

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
