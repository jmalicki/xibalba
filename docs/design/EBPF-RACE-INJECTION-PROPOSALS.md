# eBPF Race Injection Proposals

## Current Problem

**Your concern is 100% valid!**

```c
SEC("tracepoint/syscalls/sys_enter_getdents64")  // ENTRY point
int trace_getdents64(void *ctx) {
    // Busy-wait here for 10μs
    return 0;
}
```

**Timeline:**
1. Thread calls `getdents64()`
2. Hook fires **BEFORE** syscall executes
3. Busy-wait for 10μs **in the hook**
4. **THEN** getdents64 actually runs (acquires locks, reads directory)

**The Problem:**
- During the busy-wait, the thread **hasn't done anything yet**
- No locks acquired, no cursor positioned, no file_operations invoked
- Other threads just see the syscall hasn't started
- **No intermediate state to corrupt!**

This is like delaying at a traffic light **before** entering the intersection:
```
Thread A: [wait at light] → [DELAY 10μs] → [enter intersection] → [lock acquired] → [traverse]
Thread B:                                    [wait at light] → [wait for lock]...
```

Thread B doesn't race - it just waits for the lock normally!

---

## Prior Art Research

### Existing eBPF Fault Injection Tools:

**1. BPF Error Injection Framework** (Linux kernel built-in)
- Feature: `bpf_override_return()`
- Requires: `CONFIG_BPF_KPROBE_OVERRIDE=y`
- **What it does**: Can make syscalls FAIL and return errors
- **Example**: Make `kmalloc()` return NULL to simulate OOM

**2. Linux Kernel Fault Injection** (`CONFIG_FAULT_INJECTION`)
- Features: failslab, fail_page_alloc, fail_make_request
- **Not eBPF-based** - uses kernel config

**3. syzkaller**
- Uses eBPF for coverage-guided fuzzing
- **Does NOT do delay injection** - focuses on input fuzzing

**4. bpftrace / BCC**
- Can add delays via busy-wait in probes
- **Used for performance analysis**, not chaos testing

### **Finding**: No prior art for eBPF-based race window expansion!

This is novel territory. Closest is bpf_override_return for error injection, but nobody is using eBPF to widen race windows for concurrency testing.

---

## Proposal 1: Hook INSIDE ext4_readdir (After Lock, Before Read)

**Concept**: Hook at a point where locks are held but data hasn't been read yet.

### Hook Points in ext4_readdir:

Looking at the code:
```c
static int ext4_readdir(struct file *file, struct dir_context *ctx)
{
    // 1. Prepare (allocate buffers)
    // 2. Loop: while (ctx->pos < inode->i_size)
    //    a. Map block         ← **HOOK HERE (Proposal 1a)**
    //    b. Read buffer       
    //    c. Verify checksum
    //    d. **Check i_version** ← **HOOK HERE (Proposal 1b)** 
    //    e. Iterate entries
    //    f. dir_emit() for each
    //    g. Update ctx->pos   ← **HOOK HERE (Proposal 1c)**
}
```

### Proposal 1a: Hook After Block Mapping

```c
SEC("kprobe/ext4_bread")  // Called to read directory block
int trace_ext4_bread(struct pt_regs *ctx) {
    // At this point:
    // - ctx->pos is set (cursor positioned)
    // - About to read directory data
    // - Other threads can modify directory NOW
    
    if (should_inject_delay()) {
        bpf_busy_wait(10000); // 10μs delay
    }
    return 0;
}
```

**Effect**: Delay **between** cursor positioning and data read
- Thread A positions cursor
- Thread A **PAUSES** (eBPF)
- Thread B modifies directory (insert/delete/rename)
- Thread A resumes and reads **stale cursor position**
- **Potential duplicate or skip!**

**Pros:**
- Hooks at kernel function (works for all filesystems that use ext4_bread)
- Delay is DURING the operation

**Cons:**
- `ext4_bread` is ext4-specific
- Need separate hooks for XFS, btrfs

---

### Proposal 1b: Hook at i_version Check

```c
// Hook at the i_version check point
// This is where ext4 detects if directory changed
SEC("kprobe/inode_eq_iversion")
int trace_iversion_check(struct pt_regs *ctx) {
    // At this point:
    // - We're checking if directory changed since last read
    // - If changed, will rescan from block start
    // - PERFECT point to inject delay!
    
    if (should_inject_delay()) {
        bpf_busy_wait(10000);
    }
    return 0;
}
```

**Effect**: Delay at the **exact** moment ext4 checks for concurrent modifications

**Pros:**
- Targets the i_version mechanism (key to duplicate prevention)
- Filesystem-agnostic (all FSes use i_version)

**Cons:**
- inode_eq_iversion is inline function (might not be hookable)

---

### Proposal 1c: Hook After Entry Iteration

```c
SEC("kprobe/dir_emit")  // Called for each directory entry
int trace_dir_emit(struct pt_regs *ctx) {
    // At this point:
    // - We're about to emit an entry to userspace
    // - ctx->pos not yet updated
    // - Perfect race window!
    
    // Delay 10% of dir_emit calls
    if (should_inject_delay()) {
        bpf_busy_wait(5000); // 5μs
    }
    return 0;
}
```

**Effect**: Delay **between entries** during iteration

**Pros:**
- Generic (all filesystems use dir_emit)
- Creates many small race windows
- High probability of hitting a race

**Cons:**
- Very hot path (called for every entry)
- Might slow down testing significantly

---

## Proposal 2: Use fexit/fentry (BPF Trampolines)

**Modern eBPF feature** (kernel 5.5+): `fentry`/`fexit` programs

```c
SEC("fentry/ext4_readdir")
int BPF_PROG(fentry_ext4_readdir, struct file *file, struct dir_context *ctx)
{
    // Runs BEFORE ext4_readdir executes
    // Can access function arguments directly
    
    if (should_inject_delay()) {
        bpf_busy_wait(10000);
    }
    return 0;
}

SEC("fexit/ext4_readdir")
int BPF_PROG(fexit_ext4_readdir, struct file *file, struct dir_context *ctx, int ret)
{
    // Runs AFTER ext4_readdir completes
    // Can delay AFTER reading but BEFORE returning to userspace
    
    if (should_inject_delay()) {
        bpf_busy_wait(5000);
    }
    return 0;
}
```

**Advantage**: Lower overhead than kprobe, can hook entry AND exit

**Still has the same problem**: Delay is before/after, not DURING

---

## Proposal 3: Multiple Hook Points (Sandwich Attack)

**Concept**: Hook at MULTIPLE points in the call chain to create overlapping race windows

```c
// Hook 1: Syscall entry (widen window before locks)
SEC("tracepoint/syscalls/sys_enter_getdents64")
int hook_entry(void *ctx) {
    if (rand() % 100 < 20) bpf_busy_wait(5000);  // 20% @ 5μs
    return 0;
}

// Hook 2: VFS layer (after path resolution, before filesystem)
SEC("kprobe/iterate_dir")
int hook_vfs(struct pt_regs *ctx) {
    if (rand() % 100 < 30) bpf_busy_wait(10000); // 30% @ 10μs
    return 0;
}

// Hook 3: Filesystem layer (during actual read)
SEC("kprobe/ext4_readdir")
int hook_fs(struct pt_regs *ctx) {
    if (rand() % 100 < 50) bpf_busy_wait(15000); // 50% @ 15μs
    return 0;
}

// Hook 4: Entry emission (between entries)
SEC("kprobe/dir_emit")
int hook_emit(struct pt_regs *ctx) {
    if (rand() % 100 < 10) bpf_busy_wait(2000);  // 10% @ 2μs
    return 0;
}
```

**Effect**: Creates **multiple** race windows at different layers

**Pros:**
- Much higher chance of hitting a race
- Tests different types of races (VFS vs FS layer)
- Can find bugs at any level

**Cons:**
- More complex
- Higher performance impact

---

## Proposal 4: Use bpf_override_return (Error Injection)

**Most promising!** This is what the comments mention but code doesn't use!

```c
SEC("kprobe/ext4_readdir")
int inject_error(struct pt_regs *ctx) {
    // Randomly make ext4_readdir FAIL with -EAGAIN
    if (rand() % 100 < 10) {  // 10% failure rate
        bpf_override_return(ctx, -EAGAIN);  // Force syscall to fail!
        return 0;
    }
    return 0;
}
```

**Effect**: Forces getdents64 to **FAIL** and retry

**Why this creates races:**
```
Thread A: getdents64() [reads entries A, B]
Thread A: getdents64() again [eBPF injects -EAGAIN!]
Thread A: Retries getdents64()
Thread B: (during retry) renames C → D
Thread A: Resumes, might see D twice (at old and new position)
```

**Pros:**
- Actually mentioned in the code comments!
- Requires `CONFIG_BPF_KPROBE_OVERRIDE=y` (we control kernel via VMs!)
- More realistic (simulates transient I/O errors)
- Forces **retry logic** which is where races hide

**Cons:**
- Requires special kernel config
- Not all functions can be overridden (only whitelisted ones)

---

## Proposal 5: Hook at Lock Acquisition (Most Surgical)

**Target**: The moment **BEFORE** a lock is acquired

```c
// Hook at the mutex/spinlock just before acquisition
SEC("kprobe/mutex_lock")  // Or __mutex_lock
int hook_before_lock(struct pt_regs *ctx) {
    // Check if this is a directory inode lock
    // (need to filter to avoid pausing ALL locks)
    
    if (is_directory_lock(ctx)) {  // Filtering needed
        if (should_inject_delay()) {
            bpf_busy_wait(10000);  // Delay before lock acquired
        }
    }
    return 0;
}
```

**Effect**: Delay right before lock acquisition

**The race:**
```
Thread A: About to lock directory inode
Thread A: [eBPF PAUSES HERE] for 10μs
Thread B: Gets lock, modifies directory, releases lock
Thread A: Finally gets lock, but directory changed underneath
Thread A: Might have stale cursor expectations
```

**Pros:**
- Hooks at the exact critical section entry
- Works for all filesystems (all use locks)
- Surgical precision

**Cons:**
- mutex_lock is EXTREMELY hot (thousands/sec)
- Need sophisticated filtering to only delay directory locks
- Hard to identify "directory locks" from eBPF context

---

## Proposal 6: Hybrid - Error Injection + Delays

**Combine bpf_override_return with delays**:

```c
SEC("kprobe/ext4_readdir")
int hybrid_injection(struct pt_regs *ctx) {
    __u32 rand = bpf_get_prandom_u32();
    __u32 action = rand % 100;
    
    if (action < 10) {
        // 10%: Inject error (force retry)
        bpf_override_return(ctx, -EAGAIN);
    } else if (action < 30) {
        // 20%: Inject delay (widen window)
        bpf_busy_wait(10000);
    }
    // 70%: No injection (normal operation)
    
    return 0;
}
```

**Effect**: Combines both techniques
- Errors force retries (creates multiple passes)
- Delays widen race windows (increases probability)

---

## Proposal 7: Targeted VFS Layer Delays

**Hook at VFS layer functions** that ALL filesystems use:

```c
// Hook 1: iterate_dir (VFS wrapper for readdir)
SEC("kprobe/iterate_dir")
int hook_iterate_dir(struct pt_regs *ctx) {
    // This is called by getdents64 after entering VFS layer
    // At this point: path resolved, inode found, about to call ->iterate_shared
    
    if (should_inject_delay()) {
        bpf_busy_wait(10000);
    }
    return 0;
}

// Hook 2: After ->iterate_shared returns
SEC("kretprobe/iterate_dir")
int hook_iterate_dir_ret(struct pt_regs *ctx) {
    // Delay AFTER reading but BEFORE returning to userspace
    // Gives time for other threads to modify during "commit" phase
    
    if (should_inject_delay()) {
        bpf_busy_wait(5000);
    }
    return 0;
}
```

**Why this might work better:**

The ext4_readdir code shows:
```c
while (ctx->pos < inode->i_size) {
    // Map block
    // Read buffer
    // Check i_version ← **If directory changed, rescan**
    // Iterate entries
    // Update ctx->pos
}
```

If we delay at `iterate_dir` (which calls ext4_readdir), we delay **during** the VFS layer processing but **before** filesystem-specific logic.

---

## Proposal 8: Use LSM (Linux Security Modules) Hooks

**Novel approach**: LSM hooks for permission checks

```c
SEC("lsm/inode_permission")
int hook_permission(struct inode *inode, int mask) {
    // This fires when kernel checks if we can read directory
    // Happens DURING getdents64, not before!
    
    // Only inject for directories
    if (S_ISDIR(inode->i_mode)) {
        if (should_inject_delay()) {
            bpf_busy_wait(5000);
        }
    }
    return 0;
}
```

**Why this might work:**
- LSM hooks fire DURING syscall execution
- Called multiple times per syscall
- Already kernel-internal (not at syscall boundary)

---

## Recommendation Priority

### **Tier 1: High Probability of Success**

1. **Proposal 4**: `bpf_override_return` error injection
   - Why: Explicitly designed for fault injection
   - Forces retries = multiple scan passes = higher race probability
   - We control kernel (can enable CONFIG_BPF_KPROBE_OVERRIDE)
   
2. **Proposal 7**: VFS layer delays (iterate_dir)
   - Why: Hooks after path resolution but during iteration
   - More likely to be "mid-operation" than syscall entry
   - Filesystem-agnostic

### **Tier 2: Worth Trying**

3. **Proposal 3**: Multiple hooks (sandwich attack)
   - Why: Increases probability through volume
   - Even if individual hooks don't work perfectly, cumulative effect might
   
4. **Proposal 1a**: Hook ext4_bread
   - Why: Definitely during operation (reading directory blocks)
   - But ext4-specific

### **Tier 3: Experimental**

5. **Proposal 8**: LSM hooks
   - Why: Novel, untested, might work
   
6. **Proposal 5**: Lock-level hooks
   - Why: Too broad, filtering is hard

### **Not Recommended**

7. **Current approach** (syscall entry tracepoint)
   - Your skepticism is correct - likely ineffective

---

## Testing the Hypothesis

### Quick Experiment:

Run with current implementation and check:
```bash
# Do we find ANY duplicates?
bazel run //chaos:simple_chaos_test -- --posix /tmp/test

# If YES: Current approach works (surprising!)
# If NO: Current approach fails (as suspected)
```

**If we find 0 duplicates**: Confirms the flaw you identified!

---

## Detailed Implementation Plan (Proposal 4 - Error Injection)

**Step 1**: Create new eBPF program:
```c
// File: chaos/error_injector.bpf.c
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

// Hook ext4_readdir and randomly make it fail
SEC("kprobe/ext4_readdir")
int inject_readdir_error(struct pt_regs *ctx) {
    __u32 rand = bpf_get_prandom_u32();
    
    if ((rand % 100) < 10) {  // 10% failure rate
        // Override return value - make syscall fail!
        bpf_override_return(ctx, -EAGAIN);
    }
    
    return 0;
}
```

**Step 2**: Enable kernel config in VM:
```bash
# In vm/build-scripts/build-kernel.sh
CONFIG_BPF_KPROBE_OVERRIDE=y
CONFIG_FUNCTION_ERROR_INJECTION=y
```

**Step 3**: Test if it causes retries and races

---

## Next Steps

1. **Validate current approach**: Run test, check if we find ANY duplicates
2. **If 0 duplicates**: Implement Proposal 4 (error injection)
3. **If duplicates found**: Current approach works! (surprising but good)
4. **Either way**: Document findings and iterate

---

## Open Questions

1. **Does the current busy-wait at syscall entry actually work?**
   - Need experimental validation
   - Might work due to scheduler effects we don't understand
   
2. **Can we hook in the MIDDLE of ext4_readdir?**
   - eBPF can only hook at function boundaries
   - Can't hook inside loops
   
3. **Is bpf_override_return the killer feature?**
   - Makes syscalls fail → forces retries
   - Retries = multiple passes = more chances for races

---

*Created: 2025-10-12*
*Branch: enhance/ebpf-injection*

