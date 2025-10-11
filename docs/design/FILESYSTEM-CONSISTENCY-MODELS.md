# Filesystem Consistency Models and Testing Strategy

**Critical for understanding what bugs Xibalba can find**

---

## The Fundamental Question

**What does a filesystem guarantee about the visibility and ordering of operations?**

This determines:
- What is a "bug" vs "expected behavior"
- How to validate test results
- Which consistency model to use for testing

---

## POSIX and Single UNIX Specification (SUS)

### What POSIX Actually Says

**POSIX.1-2024 / Single UNIX Specification v4:**

#### File Operations (Strong Guarantees)

✅ **Writes are immediately visible to reads**:
- After successful `write()`, subsequent `read()` sees the data
- This is a **strong consistency** guarantee

✅ **Directory operations are atomic and serializable**:
- Directory modifications appear atomic
- Concurrent directory ops serialize correctly
- No partial updates visible

#### Directory Reading (Weak Guarantees)

❓ **readdir() with concurrent modifications**:

From POSIX spec (paraphrased from various man pages and specifications):

> "If a file is removed from or added to the directory after the most recent call to opendir() or rewinddir(), whether a subsequent call to readdir() returns an entry for that file is **unspecified**."

**Translation**:
- Files added DURING a readdir() scan MAY or MAY NOT appear
- Files deleted DURING a readdir() scan MAY or MAY NOT appear
- **Both outcomes are POSIX-compliant!**

**This is deliberate** - POSIX allows "weak consistency" for directory scanning to permit efficient implementations.

---

## Linux VFS Layer Guarantees

### What the Kernel VFS Promises

**Directory Consistency (from Linux kernel documentation)**:

1. **No duplicates within a single scan** ✅ GUARANTEED
   - A file MUST NOT appear twice in one readdir() scan
   - If this happens, it's a kernel bug

2. **Atomicity of individual operations** ✅ GUARANTEED
   - Each create/delete/rename is atomic
   - No partial directory entries visible

3. **Ordering within a transaction** ✅ GUARANTEED (with fsync)
   - If you `fsync()` a directory, all prior operations are durable
   - Crash recovery sees all-or-nothing

### What VFS Does NOT Promise

❌ **Visibility during concurrent scanning**:
- Files created during scan may/may not appear
- Files deleted during scan may/may not disappear
- Ordering of entries is not guaranteed

❌ **Snapshot semantics**:
- readdir() does NOT take a snapshot
- You see a "fuzzy view" of directory state
- State can change during the scan

---

## Filesystem-Specific Behavior

### ext4 (Extended Filesystem 4)

**Directory Implementation**: Hash tree (htree) or linear list

**Consistency Model**:
- ✅ **Strong for individual operations** (atomic creates/deletes)
- ⚠️  **Weak for directory scanning** (no snapshot)
- ✅ **With fsync**: Durability guaranteed

**readdir() Behavior**:
- Iterates htree in hash order (NOT alphabetical)
- Concurrent modifications: MAY see new files, MAY miss deleted files
- Cursor position: Tracked by hash value + offset
- **Race window**: Wide (directory lock held briefly)

**Bug potential**: High - complex cursor management, hash collisions

### XFS (SGI Filesystem)

**Directory Implementation**: B+ tree for large directories

**Consistency Model**:
- ✅ **Strong for individual operations** (transactional)
- ⚠️  **Weak for directory scanning** (no snapshot)
- ✅ **With fsync**: Full transaction log guarantees

**readdir() Behavior**:
- Iterates B+ tree in key order
- Concurrent modifications: Can cause cursor skips/duplicates
- Cursor position: Tracked by B+ tree position
- **Race window**: Medium (more sophisticated locking)

**Bug potential**: Medium - B+ tree is well-tested, but cursor races exist

### btrfs (B-tree Filesystem)

**Directory Implementation**: Copy-on-write B-tree

**Consistency Model**:
- ✅ **Strong for individual operations** (COW transactions)
- ✅ **Snapshot support** (can create true snapshots)
- ⚠️  **Weak for directory scanning without snapshot** (standard readdir)
- ✅ **With fsync**: Full transaction guarantees

**readdir() Behavior**:
- Iterates COW B-tree
- Concurrent modifications: Can see inconsistent view
- Cursor position: Tree generation + offset
- **Race window**: Small (COW reduces locking)
- **Special**: Can use snapshot for consistent view (not via readdir)

**Bug potential**: Low-Medium - COW helps, but cursor management still complex

### tmpfs (In-Memory Filesystem)

**Directory Implementation**: Simple linked list or radix tree (in memory)

**Consistency Model**:
- ✅ **Strong for individual operations** (atomic in-memory updates)
- ⚠️  **Weak for directory scanning** (no snapshot by default)
- ✅ **No durability needed** (RAM-based)

**readdir() Behavior**:
- Iterates in-memory structure
- Concurrent modifications: Direct memory updates, visible immediately
- Cursor position: Memory pointer or index
- **Race window**: Tiny (all in RAM, fast)

**Bug potential**: Low - simplest implementation, but races still possible

---

## What This Means for Xibalba

### Testing Strategy by Filesystem

| Filesystem | Consistency Model | Xibalba Approach |
|------------|-------------------|------------------|
| **ext4** | Weak | Use `--weak` mode, expect high bug rate with delays |
| **XFS** | Weak | Use `--weak` mode, medium bug detection |
| **btrfs** | Weak (can snapshot) | Use `--weak` mode, may test snapshots later |
| **tmpfs** | Weak | Use `--weak` or `--eventual`, fewer bugs expected |

### Three Consistency Models in Xibalba

#### 1. STRICT (Linearizable)
```bash
bazel run //chaos:simple_chaos_test -- --strict /tmp/test
```

**Assumes**: All operations visible instantly (strongest model)

**Use when**:
- Testing theoretical correctness
- Finding ALL possible race conditions
- Benchmark for "perfect" filesystem

**Expected bugs**: Most (anything not perfectly ordered triggers)

#### 2. WEAK_POSIX (Default)
```bash
bazel run //chaos:simple_chaos_test -- --weak /tmp/test
# OR just:
bazel run //chaos:simple_chaos_test -- /tmp/test
```

**Assumes**: POSIX weak consistency (snapshot at read start)

**Rules**:
- Files created BEFORE read → MUST appear
- Files deleted BEFORE read → MUST NOT appear
- Files created/deleted DURING read → Either is valid

**Use when**:
- Testing real Linux filesystems (ext4, XFS, btrfs)
- Finding actual kernel bugs vs spec

**Expected bugs**: Medium (real races, not spec violations)

#### 3. EVENTUAL (Most Permissive)
```bash
bazel run //chaos:simple_chaos_test -- --eventual /tmp/test
```

**Assumes**: Operations may take time to propagate

**Rules**:
- Only duplicates are bugs
- Missing/phantom may be propagation delays

**Use when**:
- Testing distributed filesystems (NFS, CIFS)
- Network filesystems with caching
- Testing eventual consistency guarantees

**Expected bugs**: Low (only hard duplicates)

---

## The Key Insight

**POSIX is deliberately vague about directory scan consistency!**

This is **intentional** to allow efficient implementations. The spec says:

> "If a file is removed from or added to the directory ... whether a subsequent call to readdir() returns an entry for that file is **unspecified**."

**Why**: Different filesystems have different directory structures (htrees, B+ trees, linked lists). Requiring snapshot semantics would:
- Force expensive locking
- Kill performance
- Prevent innovative designs

**Result**: Filesystems are free to show "fuzzy views" during concurrent modifications.

---

## What Xibalba Tests

### Always Bugs (All Consistency Models)

❌ **Duplicates** - Same file appears twice in ONE scan  
❌ **Crashes** - Kernel panics, hangs  
❌ **Corruption** - Invalid directory entries, garbage data  

### Bugs Depending on Model

With **--strict**:
- ❌ Missing entries (files created during scan)
- ❌ Phantom entries (files deleted during scan)
- ❌ Any ordering inconsistency

With **--weak** (default):
- ❌ Missing entries (files created BEFORE scan)
- ❌ Phantom entries (files deleted BEFORE scan)
- ✅ OK if files created/deleted DURING scan appear or don't

With **--eventual**:
- ❌ Only duplicates
- ✅ Everything else may be propagation delay

---

## Filesystem Comparison Table

| Filesystem | Directory Structure | Lock Granularity | Snapshot Support | Typical Bug Rate |
|------------|---------------------|------------------|------------------|------------------|
| **ext4** | Hash tree | Directory-level | No | High |
| **XFS** | B+ tree | Fine-grained | No | Medium |
| **btrfs** | COW B-tree | Very fine | Yes* | Low-Medium |
| **tmpfs** | In-memory | Very fine | No | Low |
| **NFS** | Remote | Protocol-level | No | Very High |

*btrfs snapshots require special API, not standard readdir()

---

## Race Conditions by Filesystem

### ext4 Race Conditions

**Common races**:
1. **Hash collision handling** - Adding entry during scan
2. **Cursor skipping** - Delete moves cursor incorrectly
3. **Htree split** - Directory grows during scan

**eBPF delays make worse**: Yes - widens windows significantly

### XFS Race Conditions

**Common races**:
1. **B+ tree rebalancing** - During concurrent scan
2. **Cursor invalidation** - Delete invalidates position
3. **Transaction boundaries** - Seeing partial transaction

**eBPF delays make worse**: Yes - transaction windows widen

### btrfs Race Conditions

**Common races**:
1. **COW generation mismatch** - Reading old generation
2. **Tree root updates** - Root changes during scan
3. **Extent allocation** - Metadata updates

**eBPF delays make worse**: Somewhat - COW provides natural atomicity

### tmpfs Race Conditions

**Common races**:
1. **List modification during iteration** - Classic linked list race
2. **Memory reordering** - CPU reordering visibility
3. **Lock-free operations** - If implemented

**eBPF delays make worse**: Minimally - all in RAM, very fast

---

## Recommendations for Testing

### For CI (Stable Kernel, tmpfs)

```bash
# Use EVENTUAL model - only catch hard bugs (duplicates)
bazel run //chaos:simple_chaos_test -- --eventual /tmp/xibalba_test
```

**Expected**: 0 bugs (stable kernel, simple tmpfs)

### For Local Testing (ext4/XFS)

```bash
# Use WEAK model - catch POSIX violations
bazel run //chaos:simple_chaos_test -- --weak /mnt/ext4/test
```

**Expected without eBPF**: 0-5 bugs (rare races)  
**Expected with eBPF**: 10-50 bugs (widened race windows)

### For Research (Finding ALL Races)

```bash
# Use STRICT model - maximum sensitivity
bazel run //chaos:simple_chaos_test -- --strict /mnt/xfs/test
```

**Expected**: Many bugs (includes spec-allowed races)

---

## Future Work

### Test Filesystem-Specific Features

1. **btrfs snapshots**: Test snapshot consistency
2. **XFS realtime**: Test realtime subvolume
3. **ext4 indexed dirs**: Test htree-specific races
4. **NFS**: Test network filesystem consistency

### Add More Consistency Models

1. **Session consistency**: Per-client session view
2. **Read-your-writes**: See own writes immediately
3. **Monotonic reads**: Never go backwards in time
4. **Causal consistency**: Respect causality chains

---

## References

- POSIX.1-2024 (IEEE Std 1003.1-2024)
- Single UNIX Specification Version 4
- Linux kernel VFS documentation
- Filesystem-specific documentation (ext4, XFS, btrfs)

---

**Key Takeaway**: POSIX deliberately provides weak guarantees for directory scanning to permit efficient implementations. Xibalba supports multiple consistency models to test different guarantees and find bugs at different levels of strictness.

