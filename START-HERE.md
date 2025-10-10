# Start Here: RUDRA Chaos Testing Framework

*Welcome to RUDRA - The Howling Storm of Filesystem Testing*

---

## What is This?

**RUDRA** is a chaos testing framework for finding race conditions and concurrency bugs in kernel filesystem code.

**Inspired by**: Jepsen (distributed systems testing)

**Powered by**: eBPF fault injection + concurrent stress testing

**Target**: Linux kernel async directory iteration (io_uring)

---

## 🚀 **READY TO BUILD? → [Implementation Plan](docs/IMPLEMENTATION-PLAN.md)** ⭐

**265 checkboxes, 10 phases, 8-12 weeks**

**Start at checkbox 1 and work sequentially!**

---

## Reading Order

### If You Want to Implement (→ START HERE!)

**1. [Implementation Plan](docs/IMPLEMENTATION-PLAN.md)** ⭐ **← BEGIN HERE**
   - 265-checkbox step-by-step guide
   - Every task broken down
   - Estimated time for each phase

**2. [Testing Framework](docs/TESTING-FRAMEWORK.md)**
   - Complete framework specification
   - Code examples
   - Architecture details

**3. [Race Conditions & Fault Injection](docs/RACE-CONDITIONS-AND-FAULT-INJECTION.md)**
   - Technical mechanics
   - Specific fault types
   - Working eBPF examples

---

### If You Want to Understand the Concept (30 min)

1. **README.md** ← Overview
2. **[Jepsen Principles](docs/JEPSEN-INSPIRED-FILESYSTEM-TESTING.md)** ← Conceptual foundation
3. **[Fault Injection Scope](docs/FAULT-INJECTION-SCOPE.md)** ← What we test vs don't test

---

### If You're Curious About the Name

1. **[Naming Options](docs/NAMING-OPTIONS.md)** ← Why "RUDRA"?

---

## Quick Concepts

### The Core Idea

```
Normal testing:
  Run operations sequentially
  Check results
  → Finds functional bugs
  → Misses race conditions

RUDRA testing:
  Run operations concurrently (20+ threads)
  Inject pauses at critical points (expand race windows 1000x)
  Inject faults (test error handling)
  Record complete history
  Check invariants
  → Finds race conditions
  → Finds bugs that appear "once in a million operations"
```

### The Three Pillars

**1. DirectoryReader Abstraction**
- Test both classic `readdir()` and io_uring `getdents`
- With same test code
- Automatic validation they match

**2. eBPF Nemesis**
- Pause threads at critical points
- Inject failures (ENOMEM, EIO, EAGAIN)
- Four modes: probabilistic, deterministic, adaptive, adversarial

**3. Chaos Operations**
- Concurrent readers (20 threads)
- Concurrent writers (10 threads)
- Rapid creation/deletion
- Strategic pauses
- Invariant checking

---

## Status

**Phase**: Design complete, ready for implementation

**Documentation**: 8,251 lines of specifications

**Timeline**: 8-12 weeks to full implementation

**Next step**: Follow `docs/IMPLEMENTATION-PLAN.md` checkboxes

---

## The Mythology

**Rudra** appears in the Rigveda (ancient Vedic texts) as:

> "The Howler" - a fierce storm god
> 
> "He who makes adversaries cry" - destroys enemies
>
> "The healer" - brings health through destruction of disease

**Perfect for testing**:
- **Howling storm** = Chaos testing
- **Makes adversaries cry** = Makes bugs visible
- **Healer** = Improves code by destroying bugs

In later tradition, Rudra becomes Shiva the Destroyer/Transformer.

**RUDRA destroys buggy code to transform it into robust code.**

---

## Directory Structure

```
rudra/
├── README.md                 # This overview
├── START-HERE.md             # You are here
├── docs/                     # Complete specifications
│   ├── IMPLEMENTATION-PLAN.md
│   ├── JEPSEN-INSPIRED-FILESYSTEM-TESTING.md
│   ├── RACE-CONDITIONS-AND-FAULT-INJECTION.md
│   ├── TESTING-FRAMEWORK.md
│   ├── FAULT-INJECTION-SCOPE.md
│   └── NAMING-OPTIONS.md
├── common/                   # DirectoryReader abstraction
│   └── (to be implemented)
├── chaos/                    # Chaos tests and eBPF injection
│   └── (to be implemented)
├── vms/                      # VM infrastructure
│   └── (to be implemented)
├── helpers/                  # Test data generation
│   └── (to be implemented)
├── benchmarks/               # Performance measurement
│   └── (to be implemented)
└── test-results/             # Test outputs
```

---

## Contributing

**Current phase**: Implementation starting

**How to help**:
1. Review design documents
2. Pick a component from IMPLEMENTATION-PLAN.md
3. Implement and test
4. Submit pull request

**Key components needed**:
- [ ] DirectoryReader abstraction (Week 1-2)
- [ ] Basic chaos framework (Week 3-4)
- [ ] eBPF pause injector (Week 5-7)
- [ ] VM automation (Week 8-9)
- [ ] Integration (Week 10-12)

---

## Related Projects

**Async getdents implementation**: See `../io-uring-enhancements`

**RUDRA tests**: The implementation that RUDRA will test

**Relationship**: RUDRA is the testing framework, io-uring-enhancements is what we're testing

---

## License

TBD (likely GPL-2.0 for kernel-related code, MIT for userspace)

---

*Let the howling storm of testing begin!* 🌪️

*RUDRA: Destroyer of Bugs, Transformer of Code*

