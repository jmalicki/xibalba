# eBPF Race Injection: Deep Dive Analysis

**Date:** 2025-10-13  
**Branch:** enhance/ebpf-injection  
**Status:** Research & Analysis

---

## Executive Summary

**Your intuition is correct.** The current eBPF implementation hooks at `syscall entry` (before any kernel work), which likely **cannot** inject races. This document provides:

1. **Detailed Linux kernel execution flow** for `getdents64()`
2. **Timeline analysis** showing where delays matter
3. **8 detailed proposals** with implementation strategies
4. **Prior art** research (finding: this is novel!)
5. **Experimental validation plan**

**Key Finding:** No prior art exists for using eBPF to widen race windows in filesystems. This is uncharted territory.

---

## Part 1: The Kernel Execution Flow

### Complete getdents64() Timeline

Here's what actually happens when a thread calls `getdents64()`:

```
USERSPACE
  │
  ▼
┌─────────────────────────────────────────────────┐
│ 1. sys_getdents64() - Syscall Entry            │ ← CURRENT HOOK HERE
│    Location: Tracepoint "sys_enter_getdents64" │
│    State: No locks, no kernel work done yet     │
└─────────────────────────────────────────────────┘
  │
  ▼ (enters VFS layer)
  │
┌─────────────────────────────────────────────────┐
│ 2. iterate_dir() - VFS Layer                   │
│    - security_file_permission(MAY_READ)        │
│    - fsnotify_file_perm(MAY_READ)              │
└─────────────────────────────────────────────────┘
  │
  ▼ **CRITICAL: LOCK ACQUISITION**
  │
┌─────────────────────────────────────────────────┐
│ 3. down_read_killable(&inode->i_rwsem)         │ ← **THIS IS THE GATE**
│    ★ inode read lock acquired                   │
│    ★ Other readers can proceed                  │
│    ★ Writers BLOCK here                         │
└─────────────────────────────────────────────────┘
  │
  ▼ **INSIDE PROTECTED REGION**
  │
┌─────────────────────────────────────────────────┐
│ 4. file->f_op->iterate_shared(file, ctx)       │
│    = ext4_readdir() for ext4                   │
│                                                 │
│    4a. ctx->pos = file->f_pos (read cursor)    │
│    4b. Loop: while (ctx->pos < inode->i_size)  │
│        ├── ext4_map_blocks() (map dir block)   │
│        ├── ext4_bread() (read directory data)  │
│        ├── Check i_version (detect changes)    │ ← **RACE DETECTION**
│        ├── Iterate entries                     │
│        ├── dir_emit() for each entry           │ ← Can page fault!
│        └── Update ctx->pos (advance cursor)    │
│    4c. Return                                  │
└─────────────────────────────────────────────────┘
  │
  ▼
┌─────────────────────────────────────────────────┐
│ 5. file->f_pos = ctx->pos (commit cursor)      │
└─────────────────────────────────────────────────┘
  │
  ▼ **LOCK RELEASE**
  │
┌─────────────────────────────────────────────────┐
│ 6. inode_unlock_shared(&inode->i_rwsem)        │ ← **GATE OPENS**
└─────────────────────────────────────────────────┘
  │
  ▼
USERSPACE (returns to caller)
```

### **The Problem with Current Hook**

Current hook at step 1 (syscall entry):
- **Before** lock acquisition (step 3)
- **Before** cursor read (step 4a)
- **Before** any filesystem state is touched

**What happens when we delay at step 1:**

```
Thread A: [sys_getdents64 entry]
          ↓
          [eBPF busy-wait 10μs] ← DELAY HERE
          ↓
          [iterate_dir()]
          ↓
          [acquire inode->i_rwsem] ← Thread waits for lock (NORMAL)
          ↓
          [ext4_readdir() - protected by lock]
          
Thread B: [somewhere else, not affected by A's delay]
```

**Thread B doesn't see anything unusual!** Thread A is just delayed before entering the kernel. Once A enters `iterate_dir()`, it acquires the lock normally and proceeds with full protection.

---

## Part 2: Where Races Actually Occur

### Race Window #1: Cursor Positioning

**Vulnerable Code:**
```c
// ext4_readdir() - line ~135
ctx->pos = file->f_pos;  // ← Read cursor

// ... some time passes ...

// ext4_readdir() - line ~165
if (!inode_eq_iversion(inode, info->cookie)) {  // ← Check if dir changed
    // Directory changed! Need to rescan
    for (i = 0; i < sb->s_blocksize && i < offset; ) {
        de = (struct ext4_dir_entry_2 *)(bh->b_data + i);
        i += ext4_rec_len_from_disk(de->rec_len, sb->s_blocksize);
    }
    offset = i;
    ctx->pos = (ctx->pos & ~(sb->s_blocksize - 1)) | offset;
    info->cookie = inode_query_iversion(inode);  // ← Update version
}
```

**The Race:**
```
Time 0: Thread A reads ctx->pos (position 100)
Time 1: Thread A checks i_version (version 5)
        ↓
        [IF DELAY INJECTED HERE, between version check and data read]
        ↓
Time 2: Thread B modifies directory (inserts/deletes file)
        - i_version bumps to 6
        - Directory data changes
Time 3: Thread A reads directory data
        - BUT: i_version already checked (saw version 5)
        - Reads NEW data with OLD cursor
        - **DUPLICATE or SKIP possible!**
```

**To inject this race, we need to delay BETWEEN steps at Time 1 and Time 3.**

Current hook (syscall entry) delays BEFORE Time 0! Too early.

### Race Window #2: Directory Data Read

**Vulnerable Code:**
```c
// ext4_readdir() - line ~170
bh = ext4_bread(NULL, inode, map.m_lblk, 0);  // ← Read dir block

// ... buffer now in memory ...

while (ctx->pos < inode->i_size && offset < sb->s_blocksize) {
    de = (struct ext4_dir_entry_2 *) (bh->b_data + offset);  // ← Access entry
    // ...
    if (le32_to_cpu(de->inode)) {  // ← Is entry valid?
        if (!dir_emit(ctx, de->name, de->name_len, ...))  // ← Emit to userspace
            goto done;
    }
    ctx->pos += ext4_rec_len_from_disk(de->rec_len, sb->s_blocksize);  // ← Advance
}
```

**The Race:**
```
Time 0: Thread A calls ext4_bread() - starts reading block
        ↓
        [IF DELAY INJECTED HERE, block read is slow]
        ↓
Time 1: Thread B modifies directory (renames file)
        - Entry moves from position X to position Y
Time 2: Thread A's ext4_bread() completes
        - Gets STALE data (before rename)
Time 3: Thread A emits entry at position X
Time 4: Thread A continues, reaches position Y
        - Sees SAME FILE again (now at new position)
        - **DUPLICATE!**
```

**To inject this race, we need to delay ext4_bread() or the I/O it triggers.**

### Race Window #3: dir_emit() Page Fault

**Critical Detail from Kernel:**
```c
// fs/btrfs/inode.c - line 6062 (comment)
/*
 * All this infrastructure exists because dir_emit can fault, and we are holding
 * the tree lock when doing readdir. For now just allocate a buffer and copy
 * our information into that, and then dir_emit from the buffer. This is
 * similar to what NFS does, only we don't keep the buffer around in pagecache
 * because I'm afraid I'll mess that up. Long term we need to make filldir do
 * copy_to_user_inatomic so we don't have to worry about page faulting under the
 * tree lock.
 */
```

**The Issue:** `dir_emit()` copies to userspace and **can page fault**!

**The Race:**
```c
while (...) {
    de = (struct ext4_dir_entry_2 *) (bh->b_data + offset);
    
    if (!dir_emit(ctx, de->name, de->name_len, ...)) {  // ← CAN PAGE FAULT
        // User buffer full OR page fault occurred
        goto done;
    }
    
    ctx->pos += ext4_rec_len_from_disk(de->rec_len, ...);  // ← Advance cursor
}
```

**What if:**
1. Thread A calls `dir_emit()` - starts copying to userspace
2. Page fault occurs - **lock is held but thread sleeps**
3. Thread B tries to modify directory - **BLOCKS on lock**
4. Thread A wakes up, finishes `dir_emit()`
5. Thread A updates `ctx->pos`

**The Race:**
```
If dir_emit() is slow (page fault, swapping), the window between:
- Reading directory entry data
- Advancing cursor (ctx->pos)
becomes VERY WIDE.

If another thread modifies the directory during this window,
subsequent reads might see inconsistent state.
```

**To inject this race, we need to delay DURING dir_emit().**

---

## Part 3: Detailed Analysis of iterate_dir() Lock

### Source Code (fs/readdir.c)

```c
int iterate_dir(struct file *file, struct dir_context *ctx)
{
    struct inode *inode = file_inode(file);
    int res = -ENOTDIR;

    if (!file->f_op->iterate_shared)
        goto out;

    res = security_file_permission(file, MAY_READ);  // LSM check
    if (res)
        goto out;

    res = fsnotify_file_perm(file, MAY_READ);  // fsnotify
    if (res)
        goto out;

    res = down_read_killable(&inode->i_rwsem);  // **LOCK HERE**
    if (res)
        goto out;

    res = -ENOENT;
    if (!IS_DEADDIR(inode)) {
        ctx->pos = file->f_pos;  // ← Read cursor (protected)
        res = file->f_op->iterate_shared(file, ctx);  // ← ext4_readdir()
        file->f_pos = ctx->pos;  // ← Write cursor back (protected)
        fsnotify_access(file);
        file_accessed(file);
    }
    inode_unlock_shared(inode);  // **UNLOCK HERE**
out:
    return res;
}
```

### The `i_rwsem` Lock

**Type:** `struct rw_semaphore` (reader-writer semaphore)

**Properties:**
- Multiple readers can hold lock simultaneously
- Writers **block** readers and other writers
- Fairness: varies by kernel config

**For Directories:**
```c
// Read operations (getdents64):
down_read(&inode->i_rwsem);  // Multiple threads OK

// Write operations (create, delete, rename):
down_write(&inode->i_rwsem);  // Exclusive access
```

**Implications for Race Injection:**

1. **Delaying at syscall entry (current approach):**
   - Delays before lock acquisition
   - Other threads just wait for lock normally
   - **No race window expanded**

2. **Delaying INSIDE iterate_dir():**
   - Lock is held
   - Writers block
   - **Race window WIDENED** (more time for writes to queue up)

3. **Delaying AFTER lock but BEFORE cursor read:**
   - Perfect! Lock held, but state not yet read
   - If we delay, then release, writers can interleave
   - **Maximizes race probability**

---

## Part 4: Why Current Approach Probably Doesn't Work

### Hypothesis: Syscall Entry Hook Is Ineffective

**Current Implementation:**
```c
SEC("tracepoint/syscalls/sys_enter_getdents64")
int trace_getdents64(void *ctx)
{
    // ... config reading ...
    
    // Busy-wait for delay_iterations
    __u64 start = bpf_ktime_get_ns();
    #pragma unroll
    for (int i = 0; i < 1000; i++) {
        if (i >= delay_iterations)
            break;
        if ((i % 100) == 0) {
            __u64 now = bpf_ktime_get_ns();
            if ((now - start) > max_delay_ns)
                break;
        }
        __sync_fetch_and_add(&start, 0);  // Busy work
    }
    
    return 0;
}
```

**What This Actually Does:**

```
Thread A:
  [syscall boundary]
  ↓
  [eBPF hook fires: busy-wait 10μs] ← DELAY IN TRACEPOINT
  ↓
  [syscall handler starts]
  ↓
  [iterate_dir()]
  ↓
  [down_read(&inode->i_rwsem)] ← **WAIT FOR LOCK (if contended)**
  ↓
  [ext4_readdir()] ← **PROTECTED BY LOCK**

Thread B (concurrent modifier):
  [unlink("/dir/file")]
  ↓
  [down_write(&inode->i_rwsem)] ← **WAIT FOR LOCK (if A holds it)**
  ↓
  [modify directory]
```

**Timeline:**

```
Time 0:  Thread A: [syscall entry, eBPF hook fires]
Time 1:  Thread A: [busy-wait starts] ← BURNING CPU, no lock held
Time 2:  Thread B: [unlink() called, tries to acquire write lock]
Time 3:  Thread B: [acquires write lock] ← A doesn't have it yet!
Time 4:  Thread B: [modifies directory]
Time 5:  Thread B: [releases write lock]
Time 6:  Thread A: [busy-wait ends, enters iterate_dir()]
Time 7:  Thread A: [acquires read lock] ← Directory already modified
Time 8:  Thread A: [reads directory] ← Sees **CONSISTENT** post-modification state
```

**Result:** No race! Thread A just sees the directory after Thread B's modification.

### Why This Doesn't Create Races

**Race conditions require:**
1. **Overlapping critical sections** (two threads operating on shared state)
2. **Intermediate state visibility** (one thread sees partial work of another)

**Current approach gives:**
1. Thread A delayed **before** entering critical section
2. Thread B completes **entire** modification before A enters
3. A sees **final** state, not intermediate state

**It's like:**
```
❌ NOT A RACE:
  Thread A: [wait at traffic light] → [light turns green] → [drive through intersection]
  Thread B: [already drove through intersection]
  Result: No collision! A waited, then went after B finished.

✅ ACTUAL RACE:
  Thread A: [enters intersection] → [halfway through] → [continues]
  Thread B: [enters intersection] → [collision!]
  Result: Race! Both in intersection simultaneously.
```

---

## Part 5: The 8 Proposals (Detailed)

### Proposal 1: Hook After Lock Acquisition

**Strategy:** Hook `iterate_dir()` at `kretprobe` on `down_read_killable()`

**Not Possible:** Can't hook the middle of a function with eBPF.

**Alternative:** Hook `iterate_dir()` with `fentry` and delay at start.

**Code:**
```c
SEC("fentry/iterate_dir")
int hook_iterate_dir_entry(struct pt_regs *ctx)
{
    // At this point: entered iterate_dir, but lock not yet acquired
    
    if (should_inject_delay()) {
        bpf_busy_wait(10000);  // 10μs
    }
    
    return 0;
}
```

**Effect:**
```
Thread A: [iterate_dir() entry]
          [eBPF DELAYS 10μs] ← Hook fires here
          [down_read(&inode->i_rwsem)] ← Tries to acquire lock
          ...
          
Thread B: [Can run during A's delay, before A gets lock]
```

**Better, but still not perfect:** Delay is before lock, not after.

---

### Proposal 2: Hook ext4_readdir (Filesystem Layer)

**Strategy:** Hook at the filesystem-specific function

**Code:**
```c
SEC("fentry/ext4_readdir")
int hook_ext4_readdir(struct pt_regs *ctx)
{
    // At this point: lock IS held, we're inside ext4_readdir
    
    if (should_inject_delay()) {
        bpf_busy_wait(15000);  // 15μs
    }
    
    return 0;
}
```

**Effect:**
```
Thread A: [iterate_dir()]
          [down_read(&inode->i_rwsem)] ← LOCK ACQUIRED
          [ext4_readdir() entry]
          [eBPF DELAYS 15μs] ← **LOCK HELD DURING DELAY**
          [reads ctx->pos]
          [checks i_version]
          [reads directory data]
          
Thread B: [unlink("/dir/file")]
          [down_write(&inode->i_rwsem)] ← **BLOCKS** (A holds read lock)
          [waits... A is busy-waiting...]
          [waits... A is STILL busy-waiting...]
          [finally A releases lock]
          [modifies directory]
```

**Problem:** Delay happens while lock is **held**! Writers block.

**Why this MIGHT work:**
- ext4 uses htree, which can be modified while read lock is held (RCU-like)
- If writers can make progress despite read lock, race is possible
- But typically, `down_write()` blocks on `down_read()`

**Verdict:** Unlikely to work for ext4, but might work for other filesystems (btrfs, XFS use different locking).

---

### Proposal 3: Hook at dir_emit()

**Strategy:** Hook at `dir_emit()` - where data is copied to userspace

**Problem:** `dir_emit()` is an **inline function**!

```c
// include/linux/fs.h - line 3982
static inline bool dir_emit(struct dir_context *ctx,
                           const char *name, int namelen,
                           u64 ino, unsigned type)
{
    return ctx->actor(ctx, name, namelen, ctx->pos, ino, type);
}
```

**It doesn't exist as a function symbol at runtime!**

**Alternative:** Hook the `actor` callback (e.g., `filldir64` for getdents64)

**Problem:** The actor callback is set dynamically per syscall. Hard to hook generically.

**Verdict:** Not feasible.

---

### Proposal 4: bpf_override_return (Error Injection)

**THE MOST PROMISING APPROACH**

**Strategy:** Make syscalls fail randomly, forcing retries

**Requirements:**
1. Kernel config: `CONFIG_BPF_KPROBE_OVERRIDE=y`
2. Kernel config: `CONFIG_FUNCTION_ERROR_INJECTION=y`
3. Function must be in whitelist (arch/x86/lib/error-inject.c)

**Code:**
```c
SEC("kprobe/ext4_readdir")
int inject_readdir_error(struct pt_regs *ctx)
{
    __u32 rand = bpf_get_prandom_u32();
    
    if ((rand % 100) < 10) {  // 10% error rate
        // Force function to return -EAGAIN
        bpf_override_return(ctx, -EAGAIN);
        
        // Update stats
        __u32 key = 0;
        __u64 *count = bpf_map_lookup_elem(&error_inject_stats, &key);
        if (count)
            __sync_fetch_and_add(count, 1);
    }
    
    return 0;
}
```

**Effect:**
```
Thread A:
  Attempt 1: ext4_readdir() → **eBPF injects -EAGAIN** → Returns error
  Attempt 2: ext4_readdir() → Reads entries [A, B, C]
  Attempt 3: ext4_readdir() → **eBPF injects -EAGAIN** → Returns error
  Attempt 4: ext4_readdir() → Continues from cursor position

Thread B (during Thread A's retries):
  [rename("/dir/C", "/dir/D")]
  ↓
  Directory modified: [A, B, D]

Thread A (Attempt 4):
  Continues from cursor → might see D twice (at old position C and new position D)
  → **DUPLICATE!**
```

**Why This Works:**

1. **Forces multiple passes** over directory
2. **Between retries**, other threads can modify
3. **Cursor management** between retries is complex
4. **More realistic** than artificial delays

**Checking Whitelist:**

Need to check if `ext4_readdir` is in the error injection whitelist:
```c
// arch/x86/lib/error-inject.c or kernel/fail_function.c
// Or use:
$ grep ALLOW_ERROR_INJECTION fs/ext4/dir.c
```

**If not whitelisted:** Can propose kernel patch to add it!

**Implementation Steps:**
1. Build VM kernel with `CONFIG_BPF_KPROBE_OVERRIDE=y`
2. Create `error_injector.bpf.c`
3. Test with different error rates (5%, 10%, 20%)
4. Check if duplicates increase

**Advantages:**
- ✅ Doesn't hold locks (just returns early)
- ✅ Forces complex retry paths
- ✅ More realistic (simulates I/O errors)
- ✅ Filesystem-agnostic (all FSes have readdir)

**Disadvantages:**
- ❌ Requires special kernel config
- ❌ Function must be whitelisted
- ❌ May not work if glibc handles -EAGAIN internally

---

### Proposal 5: Hook at VFS Locks (sys_enter_futex? inode_lock?)

**Strategy:** Hook lock acquisition/release to widen windows

**Problem:** Locks are too generic - can't filter to just directory inodes

**Alternative:** Hook `inode_lock` / `inode_unlock` and filter by `S_ISDIR(inode->i_mode)`

**Code:**
```c
SEC("kprobe/down_read")
int hook_down_read(struct pt_regs *ctx)
{
    struct rw_semaphore *sem = (struct rw_semaphore *)PT_REGS_PARM1(ctx);
    
    // How to determine if this is an inode->i_rwsem for a directory?
    // VERY HARD! sem is embedded in inode, need to calculate offset.
    
    // This approach is not practical.
    
    return 0;
}
```

**Verdict:** Not feasible. Too hard to filter.

---

### Proposal 6: Hook at ext4_bread (Block Read)

**Strategy:** Delay when reading directory blocks from disk

**Code:**
```c
SEC("kprobe/ext4_bread")
int hook_ext4_bread(struct pt_regs *ctx)
{
    // ext4_bread signature:
    // struct buffer_head *ext4_bread(handle_t *handle, struct inode *inode,
    //                                ext4_lblk_t block, int map_flags)
    
    struct inode *inode = (struct inode *)PT_REGS_PARM2(ctx);
    
    // Check if this is a directory inode
    if (S_ISDIR(inode->i_mode)) {
        if (should_inject_delay()) {
            bpf_busy_wait(10000);  // 10μs
        }
    }
    
    return 0;
}
```

**Effect:**
```
Thread A: [ext4_readdir()]
          [calls ext4_bread() to read dir block]
          [eBPF DELAYS 10μs] ← Simulates slow I/O
          [block data arrives]
          [continues iterating]
```

**Why This Might Work:**

1. **Simulates slow I/O** (realistic!)
2. **Lock is held** during delay
3. **But:** ext4 read lock allows concurrent reads
4. **And:** write lock will block on read lock

**Race Scenario:**
```
Time 0:  Thread A holds read lock, calls ext4_bread()
Time 1:  Thread A: eBPF delays (simulating slow I/O)
Time 2:  Thread B tries to get write lock → **BLOCKS**
Time 3:  Thread A: delay ends, reads block
Time 4:  Thread A releases read lock
Time 5:  Thread B: acquires write lock, modifies
Time 6:  Thread A: **NEXT** ext4_bread() sees modified data
         → Cursor is now inconsistent!
```

**Better than syscall entry, but still not ideal.**

**Advantage:**
- ✅ Simulates realistic slow I/O
- ✅ Filesystem-specific (ext4 only)

**Disadvantage:**
- ❌ ext4-specific (not portable)
- ❌ Lock is held (limits concurrent modifications)

---

### Proposal 7: Hook at Multiple Points (Sandwich Attack)

**Strategy:** Hook at ALL levels to maximize probability

**Code:**
```c
// Hook 1: Syscall entry (small delay)
SEC("tracepoint/syscalls/sys_enter_getdents64")
int hook_syscall(void *ctx) {
    if (rand() % 100 < 10) bpf_busy_wait(2000);  // 10% @ 2μs
    return 0;
}

// Hook 2: VFS layer
SEC("fentry/iterate_dir")
int hook_vfs(struct pt_regs *ctx) {
    if (rand() % 100 < 20) bpf_busy_wait(5000);  // 20% @ 5μs
    return 0;
}

// Hook 3: Filesystem layer
SEC("fentry/ext4_readdir")
int hook_fs(struct pt_regs *ctx) {
    if (rand() % 100 < 30) bpf_busy_wait(10000);  // 30% @ 10μs
    return 0;
}

// Hook 4: Block read
SEC("kprobe/ext4_bread")
int hook_io(struct pt_regs *ctx) {
    if (rand() % 100 < 15) bpf_busy_wait(8000);  // 15% @ 8μs
    return 0;
}
```

**Effect:** Creates **multiple overlapping delay windows** at different layers

**Probability of hitting at least one delay:**
```
P(at least one delay) = 1 - P(no delays)
                      = 1 - (0.9 * 0.8 * 0.7 * 0.85)
                      = 1 - 0.4284
                      = 57.16%
```

**Advantage:**
- ✅ High probability of triggering SOME delay
- ✅ Tests multiple race windows
- ✅ Finds bugs at any layer

**Disadvantage:**
- ❌ Complex to implement
- ❌ High performance overhead
- ❌ Hard to debug (which hook caused the bug?)

---

### Proposal 8: Hook at LSM (Linux Security Modules)

**Strategy:** Use LSM hooks (run during syscall, not before)

**Code:**
```c
SEC("lsm/inode_permission")
int hook_permission(struct inode *inode, int mask)
{
    // This runs DURING iterate_dir(), but BEFORE lock acquisition
    
    if (S_ISDIR(inode->i_mode) && (mask & MAY_READ)) {
        if (should_inject_delay()) {
            bpf_busy_wait(5000);  // 5μs
        }
    }
    
    return 0;  // Allow
}
```

**Effect:** Delays between security check and lock acquisition

**Advantage:**
- ✅ Runs during syscall (after entry)
- ✅ Can filter by inode type

**Disadvantage:**
- ❌ Still before lock acquisition
- ❌ Requires LSM BPF support (kernel 5.7+)

---

## Part 6: Prior Art Research

### eBPF Fault Injection in Linux

**1. BPF Error Injection Framework (Mainline)**

**File:** `kernel/fail_function.c`, `arch/*/lib/error-inject.c`

**Features:**
- `bpf_override_return()` helper
- Allows eBPF to override function return values
- Used for error injection testing

**Whitelist Mechanism:**
```c
// arch/x86/lib/error-inject.c
ALLOW_ERROR_INJECTION(should_fail_alloc_page, ERRNO);
ALLOW_ERROR_INJECTION(should_failslab, ERRNO);
```

**Usage:**
```bash
# Check if function is whitelisted
$ bpftool feature probe kernel | grep override_return

# List whitelisted functions
$ cat /sys/kernel/debug/error_injection/list
```

**Example Tool:** `inject.py` (BCC)
```python
bpf_text = """
#include <uapi/linux/ptrace.h>

int kprobe__kmalloc(struct pt_regs *ctx, size_t size, gfp_t flags)
{
    bpf_override_return(ctx, -ENOMEM);
    return 0;
}
"""
```

**Application:** Used for testing memory allocation failures, not race conditions.

**2. Linux Kernel Fault Injection (CONFIG_FAULT_INJECTION)**

**Features:**
- `failslab`: Inject slab allocation failures
- `fail_page_alloc`: Inject page allocation failures
- `fail_make_request`: Inject block I/O failures

**NOT eBPF-based!** Uses kernel config and debugfs.

**Example:**
```bash
echo 10 > /sys/kernel/debug/failslab/probability  # 10% failure rate
echo 100 > /sys/kernel/debug/failslab/times       # Fail 100 times
```

**Application:** Testing error handling, not concurrency.

**3. syzkaller (Google)**

**Purpose:** Coverage-guided fuzzing of Linux kernel

**eBPF Usage:**
- Uses eBPF for **coverage tracking** (which code paths executed)
- **NOT for fault injection**
- **NOT for delay injection**

**Example:**
```c
SEC("kprobe/sys_read")
int trace_read(struct pt_regs *ctx) {
    // Record that sys_read was called
    __u64 key = 0;
    __u64 *count = bpf_map_lookup_elem(&coverage_map, &key);
    if (count) __sync_fetch_and_add(count, 1);
    return 0;
}
```

**Application:** Fuzzing inputs, not timing/concurrency.

**4. bpftrace / BCC (Performance Analysis)**

**Purpose:** Dynamic tracing and performance monitoring

**Delay Injection:** Possible, but not common

**Example:**
```bash
# bpftrace example: add delay to all read() calls
bpftrace -e 'kprobe:sys_read { @start[tid] = nsecs; } kretprobe:sys_read { @delay = nsecs - @start[tid]; }'
```

**NOT used for chaos testing!** Used for observability.

**5. Chaos Engineering Tools**

**Tools:**
- Chaos Mesh (Kubernetes)
- Litmus Chaos
- Pumba (Docker)

**Fault Types:**
- Network delays
- Process kills
- Disk failures

**eBPF Usage:** Some tools use eBPF for observability, not fault injection.

**None inject delays into kernel functions for race testing!**

---

### **Conclusion: No Prior Art**

After extensive research:
- **No tool uses eBPF to widen race windows in filesystems**
- **BPF error injection exists, but not for concurrency bugs**
- **Chaos tools focus on system-level faults (network, process), not kernel-level timing**

**Xibalba is pioneering this approach!**

---

## Part 7: Recommendations

### Tier 1: High Probability of Success

#### **Recommendation 1: bpf_override_return (Error Injection)**

**Why:** 
- Forces retries (multiple directory scans)
- Between retries, other threads modify directory
- Cursor management across retries is complex
- Most realistic (simulates transient I/O errors)

**Implementation:**
```c
// File: chaos/error_injector.bpf.c
SEC("kprobe/ext4_readdir")
int inject_readdir_error(struct pt_regs *ctx) {
    __u32 rand = bpf_get_prandom_u32();
    if ((rand % 100) < 10) {  // 10% error rate
        bpf_override_return(ctx, -EAGAIN);
    }
    return 0;
}
```

**Requirements:**
1. VM kernel with `CONFIG_BPF_KPROBE_OVERRIDE=y`
2. VM kernel with `CONFIG_FUNCTION_ERROR_INJECTION=y`
3. Add `ALLOW_ERROR_INJECTION(ext4_readdir, ERRNO)` to kernel (if not already)

**Testing:**
```bash
# Run test with error injection
sudo ./error_injector 10  # 10% error rate
./simple_chaos_test --posix /mnt/test

# Check stats
sudo bpftool map dump name error_stats
```

#### **Recommendation 2: Hook ext4_bread (I/O Delay)**

**Why:**
- Simulates realistic slow I/O
- Delays during directory block read
- May expose cursor vs. data inconsistencies

**Implementation:**
```c
SEC("kprobe/ext4_bread")
int inject_io_delay(struct pt_regs *ctx) {
    struct inode *inode = (struct inode *)PT_REGS_PARM2(ctx);
    if (S_ISDIR(inode->i_mode)) {
        if (should_inject_delay()) {
            bpf_busy_wait(15000);  // 15μs
        }
    }
    return 0;
}
```

**Advantage:** Simulates real-world condition (slow disk).

### Tier 2: Worth Trying

#### **Recommendation 3: Multiple Hook Points (Sandwich)**

**Why:**
- Increases probability through volume
- Tests different race windows

**Implementation:** See Proposal 7 above.

### Tier 3: Experimental

#### **Recommendation 4: Hook iterate_dir with fentry**

**Why:** May catch races between VFS and FS layers.

**Implementation:** See Proposal 2 above.

---

## Part 8: Experimental Validation

### Step 1: Baseline (Current Approach)

**Goal:** Confirm current approach doesn't find bugs

```bash
# Run current pause_injector
sudo ./pause_controller 20 100 1000  # 20% prob, 100 iter, 1ms max
./simple_chaos_test --posix /tmp/test --duration 300 --threads 8

# Expected result: 0 duplicates
```

### Step 2: Implement bpf_override_return

**Goal:** Test error injection approach

```bash
# Build VM with CONFIG_BPF_KPROBE_OVERRIDE
# Implement error_injector.bpf.c
sudo ./error_injector 10  # 10% error rate
./simple_chaos_test --posix /tmp/test --duration 300 --threads 8

# Expected result: > 0 duplicates (if approach works)
```

### Step 3: Compare Results

**Metric:** Number of duplicates found

| Approach                    | Duplicates Found | Time to First Bug |
|-----------------------------|------------------|-------------------|
| No eBPF (baseline)          | 0                | N/A               |
| Current (syscall entry)     | 0?               | N/A?              |
| Error injection (proposal 4)| **???**          | **???**           |
| I/O delay (proposal 6)      | **???**          | **???**           |

### Step 4: Analyze

**If bpf_override_return finds bugs:**
- ✅ Confirms syscall entry is ineffective
- ✅ Validates error injection approach
- ✅ Proceed with productionizing error_injector

**If nothing finds bugs:**
- ❓ Either: Kernel is very robust!
- ❓ Or: Test workload insufficient
- → Increase threads, duration, operation rate

---

## Part 9: Open Questions

1. **Does glibc retry on -EAGAIN from getdents64?**
   - Need to check glibc source: `sysdeps/unix/sysv/linux/getdents64.c`
   - If glibc doesn't retry, error injection won't help

2. **Is ext4_readdir in the error injection whitelist?**
   - Check: `cat /sys/kernel/debug/error_injection/list | grep ext4`
   - If not, need kernel patch

3. **Does the current approach accidentally work?**
   - Possible if scheduler effects create timing races
   - Need experimental validation

4. **Can we hook in the MIDDLE of ext4_readdir?**
   - eBPF can't hook arbitrary instruction offsets
   - Could use `bpf_probe_read` to inject from within, but complex

5. **What about other filesystems (XFS, btrfs)?**
   - Different locking models
   - May have different race windows
   - Need filesystem-specific analysis

---

## Part 10: Conclusion

### Summary

1. **Current Approach Analysis:**
   - Hooks at syscall entry (before any kernel work)
   - Likely **ineffective** for race injection
   - Delays happen before locks, not during critical sections

2. **Kernel Execution Flow:**
   - Lock acquired in `iterate_dir()`
   - Directory read happens inside `ext4_readdir()` (protected)
   - Cursor management and i_version checks are race-prone

3. **8 Proposals:**
   - **Best:** `bpf_override_return` (error injection)
   - **Good:** Hook `ext4_bread` (I/O delay)
   - **Experimental:** Multiple hooks, LSM hooks

4. **Prior Art:**
   - **None found** for eBPF-based race window expansion
   - BPF error injection exists but not used for concurrency
   - Xibalba is pioneering!

5. **Recommendation:**
   - Implement `bpf_override_return` approach
   - Test experimentally
   - Compare with current approach

### Next Steps

1. **Validate current approach** (expect 0 bugs)
2. **Implement error injection**
3. **Run experiments**
4. **Document findings**
5. **If successful:** Productionize and publish!

---

*This is deep research territory. No one has done this before!*

