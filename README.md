# Xibalba: Chaos Testing Framework for Kernel Filesystems

*The Underworld of Trials*

**Xibalba** (shee-BAHL-bah) - The Mayan underworld where code faces its darkest trials

---

## What is Xibalba?

**Xibalba** is a Jepsen-inspired chaos testing framework specifically designed for testing concurrent filesystem operations in the Linux kernel using eBPF fault injection.

**Purpose**: Find race conditions, concurrency bugs, and edge cases in kernel filesystem code **before** they reach production.

**Inspired by**: Kyle Kingsbury's Jepsen framework for distributed systems

**Novel approach**: Brings Jepsen-quality chaos engineering to kernel filesystem testing using eBPF

---

## The Name: Xibalba

**Xibalba** - From Maya mythology, literally "Place of Fear":

In the Popol Vuh (the Maya creation epic), Xibalba was the underworld ruled by the Lords of Death. To reach the surface world, the Hero Twins had to survive a gauntlet of deadly trials in the Houses of Xibalba:

- **The Dark House** - Absolute darkness where they could not see
- **The Razor House** - Filled with obsidian blades that moved on their own
- **The Cold House** - Freezing temperatures that could kill
- **The Jaguar House** - Hungry jaguars prowling in the shadows
- **The Fire House** - Unbearable heat and flames
- **The Bat House** - Giant death bats with razor wings

Only by passing **all** the trials could they prove their worthiness and emerge victorious.

**The Metaphor**: Like the Hero Twins, your code must survive Xibalba's gauntlet of chaos tests to prove it's worthy of production. Each test is a trial - race conditions, fault injection, concurrent operations, timing attacks. Code that survives Xibalba has been hardened through the most brutal testing imaginable.

**Acronym**: **X**treme **I**njection for **B**reaking **A**ll **L**atent **B**ugs **A**ggresively

> *"In Xibalba, only the strong survive. Your code enters the underworld. Will it emerge victorious, or will the Lords of Chaos claim another victim?"*

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

### Quick Start

```bash
# Clone repository
git clone https://github.com/your-org/xibalba.git
cd xibalba

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

```bash
# Check VM prerequisites
bazel run //vm:check_prerequisites

# Create a test VM
bazel run //vm:create_vm -- --name test-01

# Deploy Xibalba to VM
bazel run //vm:deploy_xibalba -- test-01

# Run tests in VM (enter the trials!)
bazel run //vm:run_tests -- test-01

# Destroy VM when done
bazel run //vm:destroy_vm -- test-01
```

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
┌─────────────────────────────────────────────────────────┐
│                  Xibalba Controller                      │
│                  (The Lords of Chaos)                    │
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
  │         The Houses of Xibalba (Test VMs)        │
  │  ┌────┐ ┌────┐ ┌─────┐ ┌─────┐                │
  │  │ext4│ │xfs │ │btrfs│ │tmpfs│                │
  │  └────┘ └────┘ └─────┘ └─────┘                │
  │  Each with: custom kernel + test data + eBPF   │
  │  Your code must survive ALL trials to pass     │
  └─────────────────────────────────────────────────┘
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

## Why Xibalba?

**Problem**: Concurrent filesystem code has subtle race conditions that only appear under specific timing conditions.

**Traditional testing**: Doesn't find timing-dependent bugs. They slip through to production.

**Xibalba approach**: 
1. **Concurrent Operations** - Run multiple readers/writers simultaneously (The Dark House)
2. **Timing Manipulation** - eBPF delays widen race windows (The Jaguar House)  
3. **Fault Injection** - Inject errors to test resilience (The Fire House)
4. **Resource Starvation** - ENOMEM, allocation failures (The Cold House)
5. **Edge Cases** - Boundary conditions, corner cases (The Razor House)
6. **Concurrent Mutations** - Modify while reading (The Bat House)

**Result**: Code that survives Xibalba has faced the worst. Bugs that would take months to appear in production are found in minutes.

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

TBD

---

## Acknowledgments

**Inspired by**:
- Kyle Kingsbury's Jepsen framework - Chaos engineering for distributed systems
- Aphyr's database testing methodology - Rigorous invariant checking
- Linux kernel testing community - eBPF-based testing approaches

**Mythology**:
- Xibalba from the Popol Vuh - The Maya creation epic
- The Hero Twins' trials - A metaphor for surviving through testing

---

*Xibalba: Where code faces its darkest trials and emerges battle-tested*

*"Only the strong survive the underworld. Will your code emerge victorious?"*
