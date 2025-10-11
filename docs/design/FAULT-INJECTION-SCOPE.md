# Fault Injection Scope: What to Test and What Not to Test

*Clarification Document: Legitimate vs Illegitimate Faults*
*Version: 1.0*
*Date: October 10, 2025*

## Overview

**Critical distinction**: Our testing should find bugs in **our async getdents implementation**, not test for violations of Linux's fundamental guarantees.

**This document clarifies**:
- ✅ What faults are legitimate (test our code)
- ❌ What faults violate the model (test kernel itself)
- ⚠️ What faults are optional (advanced testing)

---

## The Basic Model of Linux Filesystems

### Guarantees Linux MUST Provide

**These are fundamental guarantees** that we should ASSUME are correct:

1. **Write durability** (after fsync):
   - If `fsync()` returns success, data is on disk
   - Power loss won't lose the data
   - We should NOT test "what if fsync doesn't actually write"

2. **Atomic metadata updates** (within single syscall):
   - `create()` either creates file or doesn't
   - No partial creation
   - We should NOT test "what if create partially succeeds"

3. **Lock semantics**:
   - If you hold a lock, others can't
   - Lock is eventually released
   - We should NOT test "what if lock doesn't actually lock"

4. **Memory safety**:
   - Kernel doesn't corrupt userspace memory
   - No arbitrary memory access
   - We should NOT test "what if kernel writes to random addresses"

5. **I/O integrity** (on healthy hardware):
   - Data written is data read back
   - No silent corruption (without media failure)
   - We should NOT test "what if block layer corrupts data"

**These are kernel's responsibility, not ours.**

---

## What We ARE Testing

### Our Code's Responsibility

**We are implementing**:
- New VFS function: `vfs_getdents_async()`
- New io_uring opcode: `IORING_OP_GETDENTS`
- New filesystem functions: `iterate_async()`
- New liburing helpers

**We need to test**:
- Do these handle concurrency correctly?
- Do these handle errors gracefully?
- Do these maintain correctness under load?

**We assume**: The kernel primitives we use (locks, memory, I/O) work correctly

---

## Fault Injection Categories: In-Scope vs Out-of-Scope

### Category 1: Resource Exhaustion (✅ In-Scope)

**These CAN happen in real systems**:

| Fault | Why It's Real | How to Inject | What We Test |
|-------|---------------|---------------|--------------|
| **-ENOMEM** | System low on memory | eBPF override kmalloc | Our error handling |
| **-EAGAIN** | Lock busy (NOWAIT) | High contention | NOWAIT code path |
| **-EIO** | Disk failure | dm-flakey device | I/O error handling |
| **-EINTR** | Signal during syscall | Send SIGUSR1 | Signal handling |
| **-ENOSPC** | Disk full | Fill filesystem | Space handling |

**Legitimate**: These errors happen in production, our code must handle them

---

### Category 2: Timing and Concurrency (✅ In-Scope)

**These CAN happen due to scheduling**:

| Fault | Why It's Real | How to Inject | What We Test |
|-------|---------------|---------------|--------------|
| **Pauses** | Context switch, page fault | SIGSTOP/SIGCONT | Race conditions |
| **CPU migration** | Scheduler moves thread | sched_setaffinity | Memory ordering |
| **Lock contention** | Many threads compete | 50 concurrent threads | Fairness |
| **Cache miss** | Data not in cache | drop_caches | NOWAIT behavior |

**Legitimate**: Thread scheduling is non-deterministic, our code must be race-free

---

### Category 3: Concurrent Modifications (✅ In-Scope)

**These CAN happen (POSIX allows)**:

| Fault | Why It's Real | How to Inject | What We Test |
|-------|---------------|---------------|--------------|
| **File created** | Another process creates | Writer thread | Cursor validity |
| **File deleted** | Another process deletes | Writer thread | Cursor validity |
| **File renamed** | Another process renames | Writer thread | Cursor validity |
| **Directory reorganized** | htree rebalances | Many creates/deletes | Offset encoding |

**Legitimate**: POSIX explicitly allows concurrent modifications during readdir

**What we test**: Our code handles this gracefully (no crashes, no corruption)

**What we DON'T test**: That we see a perfectly consistent snapshot (POSIX doesn't guarantee this)

---

### Category 4: Data Corruption (❌ OUT OF SCOPE)

**These would violate kernel guarantees**:

| Fault | Why It's Wrong | Don't Inject | Not Our Problem |
|-------|----------------|--------------|-----------------|
| **Lost writes** | Clear dirty bit | ❌ No | Kernel bug |
| **Corrupt data** | Flip bits in block | ❌ No | Block layer bug |
| **Lock violations** | Lock doesn't lock | ❌ No | Kernel bug |
| **Use-after-free** | Access freed memory | ❌ No | Kernel bug |
| **Buffer overflow** | Write past end | ❌ No | Kernel bug |

**WHY NOT**: If these occur, it's a **kernel bug**, not an async getdents bug

**Exception**: Can test with KASAN/KMSAN enabled to catch OUR bugs, but shouldn't inject kernel bugs

---

### Category 5: Hardware Failures (⚠️ OPTIONAL)

**These can happen but are lower priority**:

| Fault | Reality | Inject? | Notes |
|-------|---------|---------|-------|
| **Media errors** | Disk sector fails | ⚠️ Optional | dm-flakey, test -EIO handling |
| **Power loss** | System crashes | ⚠️ Optional | Test recovery, not real-time |
| **Bit flips** | Cosmic rays | ⚠️ Optional | Only with checksums |

**These are legitimate** but:
- Lower priority (handled by block layer)
- Our code should propagate -EIO, not much else to do
- Advanced testing (Phase 2, not MVP)

---

## Corrected Fault Injection Matrix

### Legitimate Faults (Test These)

| Location | Fault Type | Injection Method | Tests What |
|----------|------------|------------------|------------|
| **vfs_getdents_async entry** | Pause 1-10ms | eBPF → SIGSTOP | Race conditions |
| **vfs_getdents_async entry** | Return -EAGAIN | eBPF override | Retry logic |
| **kmalloc** | Return NULL | eBPF override | -ENOMEM handling |
| **ext4_bread** | Return -EIO | dm-flakey | I/O error handling |
| **down_read_trylock** | Return 0 (fail) | High contention | NOWAIT behavior |
| **copy_to_user** | Return -EFAULT | madvise(DONTNEED) | Bad buffer handling |
| **io_uring_enter** | Return -EINTR | Send signal | Signal handling |
| **Between operations** | Pause thread | SIGSTOP | Concurrent modification races |
| **Context switch** | Migrate CPU | sched_setaffinity | Memory ordering |
| **Cache** | Force miss | drop_caches | NOWAIT + cold cache |

---

### Illegitimate Faults (DO NOT Test)

| Location | Fault Type | Why NOT | What It Would Mean |
|----------|------------|---------|-------------------|
| **Write operations** | Lost updates | Violates guarantee | **KERNEL BUG** |
| **Lock implementation** | Lock doesn't work | Violates guarantee | **KERNEL BUG** |
| **Memory safety** | Corrupt random memory | Violates guarantee | **KERNEL BUG** |
| **Block layer** | Silently corrupt data | Violates guarantee | **KERNEL BUG** |
| **VFS** | Return wrong file type | Violates guarantee | **KERNEL BUG** |

**If these occur in real kernel, it's a CRITICAL BUG**

**We don't test for kernel bugs** - we assume kernel is correct

---

## What Correctness Means for Our Code

### We Guarantee (Must Test)

1. **No data races** in our code
   - Use proper locking
   - Atomic operations where needed
   - Memory barriers
   - **Test with**: Thread Sanitizer, concurrent stress

2. **Handle all error returns** from kernel functions
   - kmalloc can return NULL → handle -ENOMEM
   - I/O can fail → handle -EIO
   - Locks can be busy → handle -EAGAIN
   - **Test with**: Fault injection

3. **Maintain cursor validity**
   - Offset encoding is correct
   - Resumption works
   - Handles concurrent modifications gracefully
   - **Test with**: Concurrent modification chaos

4. **No crashes** under any combination of:
   - Concurrent operations
   - Memory pressure
   - I/O errors
   - Lock contention
   - **Test with**: Chaos engineering

5. **Correct results** (within POSIX model)
   - No duplicate entries in single scan
   - No impossible entries (never existed)
   - May miss concurrent adds (POSIX allows)
   - May see concurrent deletes (POSIX allows)
   - **Test with**: Invariant checking

---

### We DO NOT Guarantee (Don't Test)

1. **Perfect snapshot** during concurrent modifications
   - POSIX doesn't require this
   - Traditional readdir doesn't provide this
   - Testing for it would be wrong

2. **Seeing all files** if rapidly modified
   - If file created and deleted during scan, may miss it
   - This is POSIX-compliant behavior
   - Not a bug

3. **Specific ordering** of entries
   - Order can vary (hash-based iteration)
   - Traditional readdir order varies too
   - Not a bug

4. **Durability** of directory operations
   - That's fsync's job
   - We don't implement fsync
   - Not our responsibility

---

## Revised Fault Injection Strategy

### Tier 1: Must Test (Core Correctness)

**Focus**: Things that can happen in production

```
Tier 1 Faults:
  ✅ Memory allocation failures (OOM)
  ✅ Lock contention (concurrent access)
  ✅ Concurrent modifications (creates/deletes)
  ✅ Pauses (expand race windows)
  ✅ Signals (interruption)
  ✅ Cache misses (cold storage)

These test OUR code under realistic conditions.
```

---

### Tier 2: Should Test (Robustness)

**Focus**: Less common but still real

```
Tier 2 Faults:
  ✅ I/O errors (disk failures)
  ✅ CPU migration (NUMA effects)
  ✅ Memory pressure (swapping)
  ✅ FD exhaustion (too many opens)
  ✅ Extreme concurrency (1000 threads)

These test robustness under stress.
```

---

### Tier 3: Optional (Advanced/Research)

**Focus**: Pathological or research scenarios

```
Tier 3 Faults:
  ⚠️ Data integrity with checksums (optional)
  ⚠️ Power loss simulation (fsck testing)
  ⚠️ Extreme delays (100s+ pauses)
  ⚠️ Deliberate corruption (test detection)

Only if implementing advanced features like checksums.
Clearly marked as optional, not part of MVP.
```

---

### Tier 4: Never Test (Kernel Bugs)

**Focus**: Things that would be kernel bugs

```
Tier 4 Faults:
  ❌ Lost writes without media failure
  ❌ Silent data corruption
  ❌ Lock not actually locking
  ❌ Memory safety violations
  ❌ VFS returning wrong data

If these happen, it's a KERNEL BUG.
File kernel bug report, don't test for it in our code.
```

---

## Updated Jepsen Mapping

### What Changes

**In JEPSEN-INSPIRED-FILESYSTEM-TESTING.md, Table needs update**:

**Before** (incorrect):
```
| Jepsen Nemesis | Filesystem Nemesis | eBPF Implementation |
|----------------|-------------------|---------------------|
| Packet loss    | Lost updates      | Clear dirty bit     | ❌ WRONG
```

**After** (correct):
```
| Jepsen Nemesis | Filesystem Nemesis | eBPF Implementation | Notes |
|----------------|-------------------|---------------------|-------|
| Packet loss    | I/O errors        | dm-flakey device    | ✅ Legitimate |
| Packet loss    | Cache miss        | drop_caches         | ✅ Legitimate |
| ~~Packet loss~~| ~~Lost updates~~  | ~~Clear dirty bit~~ | ❌ Violates model |
```

**Why the difference**:
- In distributed systems: Packet loss is a **real network fault**
- In filesystems: "Lost updates" would be a **kernel bug**, not a fault

---

## What POSIX/Linux Actually Guarantees

### For Directory Operations

**POSIX guarantees**:
1. ✅ `readdir()` returns valid entries (existed at some point)
2. ✅ No crashes during concurrent modification
3. ✅ Entries have correct types, inodes

**POSIX does NOT guarantee**:
1. ❌ Seeing all files if directory modified during scan
2. ❌ Seeing files in any particular order
3. ❌ Atomic snapshot of directory
4. ❌ Seeing files added during scan

**Our testing should match POSIX model**:
- Test for guarantees (1-3 above)
- Don't test for non-guarantees (4-7 above)

---

### For Metadata Operations

**Linux guarantees**:
1. ✅ `create()` is atomic (file exists or doesn't)
2. ✅ `unlink()` is atomic (file gone or not)
3. ✅ Metadata updates are atomic (inode updates)

**Linux does NOT guarantee**:
1. ❌ Instant visibility across all threads (caches exist)
2. ❌ No reordering of independent operations

**Our testing**:
- ✅ Test that our code doesn't break atomicity
- ✅ Test that we handle cache staleness
- ❌ Don't test "what if create isn't atomic" (that's kernel's job)

---

## Concrete Examples: What to Test vs Not Test

### Example 1: Lost Update Scenario

**Scenario**:
```
Thread A: write(fd, "data")
Thread B: read(fd)

Question: Can Thread B see partial "data"?
```

**Answer**:
- If both use proper syscalls: **NO** (kernel guarantees atomicity)
- If we inject "clear dirty bit": We'd be **testing kernel bug**

**Correct test**:
```c
// ✅ GOOD: Test concurrent read during write
void test_concurrent_read_write() {
    // Thread A writes
    // Thread B reads
    // Check: Either sees old data or new data, NOT partial
    // This tests: Our locking is correct
}

// ❌ BAD: Test lost write
void test_lost_write() {
    // Write data
    // Inject: Clear dirty bit (VIOLATES KERNEL GUARANTEE)
    // Read data
    // Check: Data is lost
    // This tests: Kernel bug, not our code!
}
```

---

### Example 2: Directory Modification

**Scenario**:
```
Thread A: Reading directory
Thread B: Deletes file during Thread A's scan

Question: Can Thread A see the deleted file?
```

**Answer**:
- **YES** - POSIX allows this (no snapshot guarantee)
- This is **normal, not a bug**

**Correct test**:
```c
// ✅ GOOD: Test concurrent modification handling
void test_concurrent_delete() {
    // Thread A: Scan directory
    // Thread B: Delete files
    // Check: No crashes, no corruption
    // Acceptable: Thread A may see deleted file (it existed when read)
    // This tests: Our code is race-free
}

// ❌ BAD: Test that we see consistent snapshot
void test_atomic_snapshot() {
    // Scan directory
    // Check: Snapshot is atomic (doesn't change during scan)
    // This expects: Something POSIX doesn't guarantee
    // This would fail even with correct implementation!
}
```

---

### Example 3: Lock Contention

**Scenario**:
```
50 threads try to acquire same inode lock

Question: Can lock be corrupted?
```

**Answer**:
- **NO** - Kernel locks are correct
- But our code must handle **contention**

**Correct test**:
```c
// ✅ GOOD: Test high contention
void test_lock_contention() {
    // 50 threads, same directory
    // Some use NOWAIT
    // Check: NOWAIT returns -EAGAIN, others wait correctly
    // This tests: Our NOWAIT logic
}

// ❌ BAD: Test lock corruption
void test_lock_corruption() {
    // Try to corrupt lock state via eBPF
    // Check: Two threads hold lock simultaneously
    // This tests: Kernel bug, not ours!
}
```

---

## Revised Fault Injection Tiers

### Tier 1: Core Testing (Always Enable)

**Purpose**: Find bugs in our code

```yaml
tier1_faults:
  # Resource exhaustion
  - name: kmalloc_failure
    probability: 5%
    location: kmalloc
    action: return NULL
    tests: -ENOMEM handling
  
  # Concurrency
  - name: timing_pause
    probability: 10%
    location: [vfs_entry, after_lock, before_copy]
    action: pause 1-10ms
    tests: race conditions
  
  # Lock contention
  - name: high_concurrency
    method: 50 threads
    tests: NOWAIT, fairness
  
  # Concurrent modifications
  - name: rapid_modify
    method: writer threads
    tests: cursor validity
```

**These are mandatory** - find bugs in our implementation

---

### Tier 2: Robustness Testing (Enable for Full Validation)

**Purpose**: Test edge cases

```yaml
tier2_faults:
  # I/O errors
  - name: block_io_failure
    probability: 2%
    location: ext4_bread
    action: return -EIO
    tests: error propagation
  
  # Memory pressure
  - name: memory_pressure
    method: allocate 16GB
    tests: page faults during copy_to_user
  
  # Extreme concurrency
  - name: thundering_herd
    method: 1000 threads
    tests: scalability limits
```

**These are important** but not critical for MVP

---

### Tier 3: Advanced/Optional (Only for Special Features)

**Purpose**: Test advanced features if implemented

```yaml
tier3_faults:
  # Data integrity (only if we add checksums)
  - name: checksum_validation
    condition: if filesystem has checksums
    method: inject bit flips
    tests: checksum detection
    marker: OPTIONAL
  
  # Power loss recovery (only if we add crash recovery)
  - name: crash_consistency
    condition: if implementing crash recovery
    method: simulate power loss
    tests: recovery correctness
    marker: OPTIONAL
```

**Clearly marked OPTIONAL**

---

### Tier 4: Never Inject (Kernel Assumptions)

**These would test kernel correctness, not ours**:

```yaml
tier4_never:
  - name: lost_write
    reason: Violates kernel guarantee
    action: NEVER INJECT
  
  - name: lock_corruption
    reason: Violates kernel guarantee
    action: NEVER INJECT
  
  - name: silent_data_corruption
    reason: Violates kernel guarantee (without media failure)
    action: NEVER INJECT
  
  - name: memory_safety_violation
    reason: Violates kernel guarantee
    action: NEVER INJECT
```

---

## Configuration: Fault Injection Profiles

### Profile 1: Development (Fast Iteration)

```yaml
development_profile:
  name: "Quick smoke test"
  duration: 30s
  faults:
    pause_probability: 5%
    pause_duration: 1-5ms
    fault_probability: 2%
  threads:
    readers: 4
    writers: 2
  
  enabled_tiers: [tier1]
  
  purpose: Quick feedback during development
```

---

### Profile 2: CI/CD (Balanced)

```yaml
ci_profile:
  name: "Continuous integration"
  duration: 5min
  faults:
    pause_probability: 10%
    pause_duration: 1-10ms
    fault_probability: 5%
  threads:
    readers: 20
    writers: 10
  
  enabled_tiers: [tier1, tier2]
  
  purpose: Catch most bugs before merge
```

---

### Profile 3: Pre-Release (Comprehensive)

```yaml
prerelease_profile:
  name: "Full validation"
  duration: 1hour
  faults:
    pause_probability: 20%
    pause_duration: 1-50ms
    fault_probability: 10%
  threads:
    readers: 50
    writers: 20
  
  enabled_tiers: [tier1, tier2]
  
  purpose: Final validation before release
```

---

### Profile 4: Research (Extreme)

```yaml
research_profile:
  name: "Find everything"
  duration: 24hours
  faults:
    pause_probability: 50%
    pause_duration: 1-100ms
    fault_probability: 20%
  threads:
    readers: 100
    writers: 50
  
  enabled_tiers: [tier1, tier2, tier3]
  
  purpose: Research, find rare bugs
  warning: Very slow, only for deep investigation
```

---

## Rationale: Why This Matters

### Avoid False Positives

**Bad test**:
```c
// Test expects perfect snapshot
assert(entries_seen == all_files_at_start);
// FAILS even with correct implementation!
// POSIX doesn't guarantee this!
```

**Good test**:
```c
// Test checks actual guarantee
for (each entry in entries_seen) {
    assert(entry_existed_at_some_point_during_scan);
    // This is what POSIX actually guarantees
}
```

---

### Avoid Testing Kernel

**Bad test**:
```c
// Inject: Corrupt lock state
// Check: Two threads hold lock simultaneously
// This tests: Kernel bug, not our code
// If this fails: File kernel bug report, don't fix in our code
```

**Good test**:
```c
// Create high contention (50 threads)
// Check: Our NOWAIT code returns -EAGAIN correctly
// This tests: Our code, assumes kernel locks work
```

---

### Focus Testing Effort

**Limited time and resources**

**Priority**:
1. **High**: Bugs in our code (Tier 1)
2. **Medium**: Edge cases in our code (Tier 2)
3. **Low**: Advanced features (Tier 3)
4. **Never**: Kernel bugs (Tier 4)

**Benefit**: Efficient use of testing resources

---

## Summary

### Scope Clarification

**We test**:
- ✅ Our async getdents implementation
- ✅ Against realistic faults (OOM, I/O errors, contention)
- ✅ Under concurrent stress
- ✅ With strategic pauses to find races

**We don't test**:
- ❌ Kernel lock correctness
- ❌ Block layer data integrity
- ❌ VFS fundamental guarantees
- ❌ Things that would be kernel bugs

**Optional testing** (Tier 3):
- ⚠️ Checksum validation (if we add checksums)
- ⚠️ Power loss recovery (if we add crash recovery)
- ⚠️ Extreme edge cases (research)

---

### Updated Fault Matrix

**Removed** from legitimate faults:
- ❌ Lost updates (clear dirty bit)
- ❌ Silent data corruption
- ❌ Lock violations

**Focus** on legitimate faults:
- ✅ Memory allocation failures
- ✅ I/O errors (via dm-flakey)
- ✅ Lock contention (via concurrent threads)
- ✅ Timing (via strategic pauses)
- ✅ Concurrent modifications (via writer threads)

---

### Action Items

**To fix documentation**:

1. **Update JEPSEN-INSPIRED-FILESYSTEM-TESTING.md**:
   - [ ] Remove "lost updates" from fault table
   - [ ] Add section on "What We Assume Is Correct"
   - [ ] Clarify focus is on OUR code, not kernel

2. **Update RACE-CONDITIONS-AND-FAULT-INJECTION.md**:
   - [ ] Add tier system (1-4)
   - [ ] Mark illegitimate faults clearly
   - [ ] Add "Testing Scope" section

3. **Update IMPLEMENTATION-PLAN.md**:
   - [ ] Note that Tier 4 faults are NOT implemented
   - [ ] Focus on Tier 1-2
   - [ ] Mark Tier 3 as optional

---

**Thank you for catching this!** This is exactly the kind of critical thinking needed.

**The principle**: Test for things that CAN happen, not things that SHOULDN'T happen (unless we're specifically testing that we detect them, like checksums detecting bit flips).

---

*Fault injection scope: Now correctly bounded to realistic faults, not kernel bug injection*

*Testing OUR code for correctness, assuming kernel primitives work*

*Advanced features (checksums, crash recovery) can have optional Tier 3 tests*

