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

**Xibalba recommendation**: `--weak` model, high eBPF delay (50% @ 500 iterations)

---

### XFS (SGI Filesystem)

**Directory Implementation**: B+ tree for large directories, shortform for small

**Consistency Model**:
- ✅ **Strong for individual operations** (transactional log)
- ⚠️  **Weak for directory scanning** (no snapshot)
- ✅ **With fsync**: Full transaction log guarantees
- ✅ **Delayed logging**: Metadata updates batched

**readdir() Behavior**:
- Iterates B+ tree in key order
- Concurrent modifications: Can cause cursor skips/duplicates
- Cursor position: Tracked by B+ tree position
- **Race window**: Medium (more sophisticated locking)
- **Special**: Directory i-node locking more granular

**Bug potential**: Medium - B+ tree well-tested, but cursor races exist

**Xibalba recommendation**: `--weak` model, medium eBPF delay (50% @ 300 iterations)

---

### btrfs (B-tree Filesystem)

**Directory Implementation**: Copy-on-write B-tree

**Consistency Model**:
- ✅ **Strong for individual operations** (COW transactions)
- ✅ **Snapshot support** (can create true snapshots)
- ⚠️  **Weak for directory scanning without snapshot** (standard readdir)
- ✅ **With fsync**: Full transaction guarantees
- ✅ **ACID properties**: Full transactional semantics

**readdir() Behavior**:
- Iterates COW B-tree
- Concurrent modifications: Can see inconsistent view
- Cursor position: Tree generation + offset
- **Race window**: Small (COW reduces locking)
- **Special**: Can use snapshot for consistent view (not via readdir)

**Bug potential**: Low-Medium - COW helps, but cursor management still complex

**Xibalba recommendation**: `--weak` model, lower eBPF delay (30% @ 200 iterations)

---

### ZFS (Zettabyte Filesystem)

**Directory Implementation**: ZAP (ZFS Attribute Processor) - hash table or tree

**Consistency Model**:
- ✅ **Strong for individual operations** (COW + transaction groups)
- ✅ **Snapshot support** (true snapshots, even for readdir if using snapshots)
- ⚠️  **Weak for directory scanning** (no snapshot by default)
- ✅ **With fsync**: Transaction group commit guaranteed
- ✅ **ACID+**: Stronger than ACID (checksums, self-healing)

**readdir() Behavior**:
- Iterates ZAP object (hash table or tree depending on size)
- Concurrent modifications: COW means seeing old or new, not partial
- Cursor position: Object ID + iteration state
- **Race window**: Very small (COW + transaction groups)
- **Special**: Snapshots provide true point-in-time consistency

**Bug potential**: Very Low - COW architecture prevents many race classes

**Xibalba recommendation**: `--weak` or `--eventual`, low eBPF delay (20% @ 100 iterations)

**Note**: ZFS on Linux (ZoL/OpenZFS) may behave slightly differently from Solaris ZFS

---

### tmpfs (In-Memory Filesystem)

**Directory Implementation**: Simple radix tree (in memory)

**Consistency Model**:
- ✅ **Strong for individual operations** (atomic in-memory updates)
- ⚠️  **Weak for directory scanning** (no snapshot by default)
- ✅ **No durability needed** (RAM-based, lost on reboot)

**readdir() Behavior**:
- Iterates in-memory structure
- Concurrent modifications: Direct memory updates, visible immediately
- Cursor position: Memory pointer or index
- **Race window**: Tiny (all in RAM, nanosecond ops)

**Bug potential**: Low - simplest implementation, but races still possible

**Xibalba recommendation**: `--eventual` model, very low eBPF delay (10% @ 50 iterations)

---

### F2FS (Flash-Friendly Filesystem)

**Directory Implementation**: Hash-based directory for flash optimization

**Consistency Model**:
- ✅ **Strong for individual operations** (atomic)
- ⚠️  **Weak for directory scanning** (no snapshot)
- ✅ **With fsync**: Checkpoint-based durability
- ⚠️  **Flash-optimized**: May delay metadata updates

**readdir() Behavior**:
- Optimized for flash storage patterns
- Concurrent modifications: Hash-based iteration
- Cursor position: Hash bucket + offset
- **Race window**: Medium (flash write patterns)

**Bug potential**: Medium - newer filesystem, less battle-tested

**Xibalba recommendation**: `--weak` model, medium eBPF delay (40% @ 300 iterations)

---

### NILFS2 (New Implementation of Log-structured Filesystem)

**Directory Implementation**: Log-structured with B-tree indexing

**Consistency Model**:
- ✅ **Strong for individual operations** (log-structured)
- ✅ **Snapshot support** (continuous snapshots)
- ⚠️  **Weak for directory scanning** (unless using snapshot)
- ✅ **With fsync**: Log flush guarantees

**readdir() Behavior**:
- Reads from current checkpoint or snapshot
- Concurrent modifications: May see older checkpoint
- Cursor position: Log position + tree position
- **Race window**: Small (log-structured reduces conflicts)

**Bug potential**: Low-Medium - log structure helps consistency

**Xibalba recommendation**: `--weak` model, medium eBPF delay (30% @ 250 iterations)

---

### NFS (Network Filesystem)

**Directory Implementation**: Remote (depends on server filesystem)

**Consistency Model**:
- ⚠️  **Weak for all operations** (network delays)
- ⚠️  **Cache coherency issues** (client-side caching)
- ⚠️  **Eventual consistency** (close-to-open semantics)
- ⚠️  **With fsync**: Only client→server, not server→storage

**readdir() Behavior**:
- May use cached directory contents
- Concurrent modifications: Extreme staleness possible
- Cursor position: Server-side, client has cookie
- **Race window**: Huge (network + caching + server)

**Bug potential**: Very High - network filesystem complexity

**Xibalba recommendation**: `--eventual` model ONLY, high eBPF delay (70% @ 800 iterations)

**Note**: NFS bugs are often "working as designed" due to network filesystem semantics

---

### FUSE-based Filesystems (User-space FS)

**Directory Implementation**: Varies by implementation

**Consistency Model**:
- Varies widely (depends on FUSE implementation)
- Generally weaker than kernel filesystems

**readdir() Behavior**:
- Depends entirely on FUSE implementation
- Can be anything from strict to very weak
- Cursor management varies

**Bug potential**: Very High - user-space implementations vary widely

**Xibalba recommendation**: `--eventual` model, test case-by-case

**Examples**: sshfs, s3fs, GlusterFS, CephFS (FUSE mode)

---

### NTFS (on Linux via ntfs-3g)

**Directory Implementation**: B+ tree (NTFS native), accessed via FUSE

**Consistency Model**:
- ⚠️  **FUSE layer** introduces additional complexity
- ⚠️  **NTFS semantics** different from POSIX
- ⚠️  **Translation layer** may have bugs

**readdir() Behavior**:
- Goes through FUSE to ntfs-3g to NTFS B+ tree
- Multiple layers of abstraction
- Cursor management complex

**Bug potential**: High - complex translation, multiple layers

**Xibalba recommendation**: `--eventual` model, high eBPF delay (60% @ 600 iterations)

---

### exFAT (Extended FAT)

**Directory Implementation**: Linked cluster chain

**Consistency Model**:
- ⚠️  **Very weak** (designed for flash media, not concurrency)
- ❌ **No atomicity guarantees** for directory operations
- ❌ **No journaling** (crash consistency poor)

**readdir() Behavior**:
- Sequential iteration through cluster chain
- Concurrent modifications: Undefined behavior (not designed for it)
- Cursor position: Cluster + offset

**Bug potential**: Extreme - not designed for concurrent operations

**Xibalba recommendation**: `--eventual` model ONLY, expect chaos

**Warning**: exFAT should not be used for concurrent workloads!

---

### procfs / sysfs (Virtual Filesystems)

**Directory Implementation**: Virtual (generated on demand)

**Consistency Model**:
- ✅ **Snapshot on open** (directory contents generated)
- ✅ **Strong consistency** within one scan
- ⚠️  **Different semantics** (not real files)

**readdir() Behavior**:
- Returns generated list
- Concurrent modifications: New scan sees new view
- No persistence (virtual)

**Bug potential**: Low - generated, not persistent

**Xibalba recommendation**: Not applicable (virtual FS, different semantics)

---

## Summary Table

| Filesystem | Implementation | Consistency | Bug Potential | Xibalba Model | eBPF Config |
|------------|----------------|-------------|---------------|---------------|-------------|
| **ext4** | Hash tree | Weak | High | `--weak` | 50% @ 500 iter |
| **XFS** | B+ tree | Weak | Medium | `--weak` | 50% @ 300 iter |
| **btrfs** | COW B-tree | Weak* | Low-Med | `--weak` | 30% @ 200 iter |
| **ZFS** | COW ZAP | Weak* | Very Low | `--weak`/`--eventual` | 20% @ 100 iter |
| **tmpfs** | In-memory | Weak | Low | `--eventual` | 10% @ 50 iter |
| **F2FS** | Flash-optimized | Weak | Medium | `--weak` | 40% @ 300 iter |
| **NILFS2** | Log-structured | Weak* | Low-Med | `--weak` | 30% @ 250 iter |
| **NFS** | Network | Eventual | Very High | `--eventual` | 70% @ 800 iter |
| **FUSE** | User-space | Varies | Very High | `--eventual` | Case-by-case |
| **NTFS** | FUSE/ntfs-3g | Weak | High | `--eventual` | 60% @ 600 iter |
| **exFAT** | Cluster chain | Very Weak | Extreme | `--eventual` | Not recommended |

*Can provide snapshots, but not via standard readdir()

---

## Advanced: Filesystem Categories

### Category 1: Traditional Unix Filesystems
**Examples**: ext2, ext3, ext4  
**Design**: Inodes + directory entries  
**Consistency**: Weak for scanning  
**Testing**: `--weak` model  

### Category 2: Enterprise Filesystems
**Examples**: XFS, JFS  
**Design**: Journaling + B-trees  
**Consistency**: Weak for scanning, strong transactions  
**Testing**: `--weak` model  

### Category 3: Copy-on-Write Filesystems
**Examples**: btrfs, ZFS, NILFS2  
**Design**: COW + snapshots  
**Consistency**: Weak for readdir, strong via snapshots  
**Testing**: `--weak` or `--eventual` depending on workload  

### Category 4: Flash-Optimized Filesystems
**Examples**: F2FS, JFFS2, UBIFS  
**Design**: Log-structured or FTL-aware  
**Consistency**: Varies (flash constraints)  
**Testing**: `--weak` model  

### Category 5: Network Filesystems
**Examples**: NFS, CIFS/SMB, AFS  
**Design**: Client-server with caching  
**Consistency**: Eventual (network latency)  
**Testing**: `--eventual` model ONLY  

### Category 6: User-Space Filesystems
**Examples**: FUSE implementations (sshfs, s3fs, GlusterFS)  
**Design**: Varies wildly  
**Consistency**: Varies wildly  
**Testing**: `--eventual` model, case-by-case tuning  

### Category 7: Virtual Filesystems
**Examples**: procfs, sysfs, debugfs  
**Design**: Kernel-generated  
**Consistency**: Snapshot on open  
**Testing**: Not applicable (different semantics)  

---

## Filesystem Features Affecting Consistency

### Journaling

**With journaling** (ext4, XFS, JFS):
- ✅ Crash recovery guaranteed
- ✅ Atomicity of operations
- ⚠️  Does NOT guarantee snapshot semantics for readdir

### Copy-on-Write (COW)

**With COW** (btrfs, ZFS, NILFS2):
- ✅ Natural atomicity (old or new, never partial)
- ✅ Can create true snapshots
- ⚠️  Standard readdir still sees fuzzy view

### Transaction Support

**With transactions** (XFS, btrfs, ZFS):
- ✅ All-or-nothing operations
- ✅ Durability guarantees
- ⚠️  Transaction boundaries don't align with readdir scans

### Snapshot Support

**With snapshots** (btrfs, ZFS, NILFS2):
- ✅ Can get true point-in-time view
- ❌ Requires special API (not standard POSIX readdir)
- ⚠️  Xibalba currently tests standard readdir only

---

## Future Xibalba Enhancements

### Test Snapshot APIs

For btrfs and ZFS:
```c
// Create snapshot
snapshot = filesystem_create_snapshot(dir);

// Read from snapshot (guaranteed consistent)
entries = read_snapshot_directory(snapshot);

// Validate (should be PERFECT - no races possible)
validate_strict(entries);  // Should find 0 bugs
```

### Test Filesystem-Specific Features

1. **ext4 htree splitting**: Trigger directory growth during scan
2. **XFS delayed allocation**: Test metadata updates
3. **btrfs balance**: Test while filesystem is rebalancing
4. **ZFS scrub**: Test during data integrity checking

### Test Different Mount Options

```bash
# ext4 with data=journal (strictest)
mount -o data=journal /dev/sdb1 /mnt/test

# ext4 with data=writeback (weakest)
mount -o data=writeback /dev/sdb1 /mnt/test
```

Different mount options → Different consistency guarantees!

---

## Key Insights

1. **POSIX is deliberately vague** about readdir consistency
2. **All filesystems have weak guarantees** for directory scanning
3. **COW filesystems** (btrfs, ZFS) have fewer race classes
4. **Network filesystems** have extreme weak consistency
5. **Snapshots help** but require special APIs

**Bottom line**: Xibalba's multi-model approach is necessary because different filesystems and scenarios need different validation strategies!

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

| Filesystem | Directory Structure | Lock Granularity | Snapshot Support | Typical Bug Rate | Xibalba Model |
|------------|---------------------|------------------|------------------|------------------|---------------|
| **ext4** | Hash tree | Directory-level | No | High | `--weak` |
| **XFS** | B+ tree | Fine-grained | No | Medium | `--weak` |
| **btrfs** | COW B-tree | Very fine | Yes* | Low-Medium | `--weak` |
| **ZFS** | COW ZAP | Very fine | Yes* | Very Low | `--weak`/`--eventual` |
| **tmpfs** | In-memory | Very fine | No | Low | `--eventual` |
| **F2FS** | Hash + flash | Medium | No | Medium | `--weak` |
| **NILFS2** | Log + B-tree | Fine | Yes* | Low-Medium | `--weak` |
| **NFS** | Remote | Protocol-level | No | Very High | `--eventual` |
| **FUSE** | Varies | Varies | Varies | Very High | `--eventual` |
| **NTFS** | B+ tree (FUSE) | Medium | No | High | `--eventual` |

*Snapshots require special API, not standard readdir()

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

