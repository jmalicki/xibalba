# Btrfs Lost Update Analysis: Race Conditions in Directory Operations

**Date:** 2025-10-13  
**Branch:** enhance/ebpf-injection  
**Focus:** Finding "half-done" state where concurrent directory updates can be lost

---

## Executive Summary

**You're absolutely right - getdents64 is the wrong place!** The real bugs happen during directory **modifications** (create, unlink, rename) where:

1. **State is "half-done"** between multiple operations
2. **Locks are released** between steps of a compound operation
3. **Other threads can see intermediate state** or lose updates

This document analyzes btrfs directory operations to find critical windows where:
- **Lost updates**: Two concurrent modifications, one disappears
- **Visibility windows**: File temporarily invisible/visible during rename
- **Index corruption**: Directory index gets out of sync

---

## Historical Btrfs Bugs: Directory Lost Updates

### Bug 1: Race Between Reading and Modifying Directories (2023)

**CVE:** None (internal consistency bug)  
**Kernel:** Fixed in 5.15-stable  
**Bug:** `index_cnt` corruption due to race

**What Happened:**
```c
// Thread A and Thread B both try to add entries to the same directory

Timeline:
1. Directory inode loaded with index_cnt = (u64)-1 (not initialized)
2. Thread A: Reads last directory index → sets index_cnt = 100
3. Thread B: Reads last directory index → sets index_cnt = 100 (SAME!)
4. Thread A: Creates file with index 101
5. Thread B: Creates file with index 101 (DUPLICATE INDEX!)
```

**Result:** Two files with the same directory index → corruption

**Link:** https://www.spinics.net/lists/stable-commits/msg334048.html

**Fix:** Proper locking around `index_cnt` initialization

---

### Bug 2: Concurrent lseek Leading to Memory Leaks (2024)

**Kernel:** Fixed in mainline  
**Bug:** Race in `file->private_data` assignment

**What Happened:**
```c
// Two threads call lseek(fd, ..., SEEK_DATA) concurrently

Thread A:
1. Checks file->private_data == NULL
2. Allocates private structure (malloc)
3. Assigns: file->private_data = priv_A

Thread B:
1. Checks file->private_data == NULL (still NULL!)
2. Allocates private structure (malloc)
3. Assigns: file->private_data = priv_B

Result: priv_A is leaked! Memory leak.
```

**Link:** https://patchwork.kernel.org/project/linux-btrfs/patch/a351fd3b0397b6fe8d94f11a6744e1655349d687.1725356877.git.fdmanana%40suse.com/

**Fix:** Lock around private_data check-and-set

---

## Btrfs Directory Operation Analysis

### The Rename Operation: A Goldmine of Race Windows

**Function:** `btrfs_rename()` (fs/btrfs/inode.c:8367)

**Steps:**
1. Start transaction
2. Insert new inode ref at destination (`btrfs_insert_inode_ref`)
3. **Unlink from old location** (`__btrfs_unlink_inode`)
4. **Link at new location** (`btrfs_add_link`)
5. Update log
6. End transaction

**The Critical Race Window:**

```c
// btrfs_rename() - simplified

ret = __btrfs_unlink_inode(trans, old_dir, old_inode, old_name, &rename_ctx);
// ↑ File is NOW REMOVED from old directory
// ↓ BUT NOT YET added to new directory!

if (new_inode) {
    ret = btrfs_unlink_inode(trans, new_dir, new_inode, new_name);
    // ↑ If destination exists, unlink it first
}

ret = btrfs_add_link(trans, new_dir, old_inode, new_name, 0, index);
// ↑ File is NOW ADDED to new directory
```

**The Race:**

```
Thread A: rename("foo", "bar")
  Time 0: __btrfs_unlink_inode("foo") ← File removed from old location
  Time 1: [WINDOW OPEN - file invisible in ANY directory!]
  Time 2: btrfs_add_link("bar") ← File added to new location

Thread B: getdents64() during Time 1
  Result: File is MISSING! Neither "foo" nor "bar" exist!
```

**Is this a bug?**
- **POSIX:** NO! POSIX allows this. Rename is atomic *within a directory*, but across directories, transient visibility is allowed.
- **Application:** YES! If app expects atomicity, data is lost during window.

---

### The Unlink Operation: Lost Link Reference

**Function:** `__btrfs_unlink_inode()` (fs/btrfs/inode.c:4239)

**Steps:**
1. Look up directory item (`btrfs_lookup_dir_item`)
2. Delete directory name (`btrfs_delete_one_dir_name`)
3. **Free path** (releases locks!)
4. Delete inode reference (`btrfs_del_inode_ref`)
5. Delete delayed dir index (`btrfs_delete_delayed_dir_index`)
6. Update directory inode (`btrfs_update_inode`)

**Critical Code:**

```c
di = btrfs_lookup_dir_item(trans, root, path, dir_ino, name, -1);
if (IS_ERR_OR_NULL(di)) {
    btrfs_free_path(path);
    return di ? PTR_ERR(di) : -ENOENT;
}
ret = btrfs_delete_one_dir_name(trans, root, path, di);

/*
 * Down the call chains below we'll also need to allocate a path, so no
 * need to hold on to this one for longer than necessary.
 */
btrfs_free_path(path);  // ← **LOCK RELEASED HERE!**
if (ret)
    return ret;

// ... more work below without lock ...

ret = btrfs_del_inode_ref(trans, root, name, ino, dir_ino, &index);
```

**The Race:**

```
Thread A: unlink("foo")
  Time 0: btrfs_delete_one_dir_name("foo") ← Name removed from directory
  Time 1: btrfs_free_path() ← **LOCKS RELEASED**
  Time 2: [WINDOW OPEN]
  Time 3: btrfs_del_inode_ref() ← Inode reference removed

Thread B: link("foo", "bar") during Time 2
  Time 2.5: Sees "foo" is gone (dir name deleted)
  Time 2.6: But inode ref still exists!
  Time 2.7: Creates hard link to inode
  Time 2.8: **CORRUPTION**: Inode ref count wrong!
```

**Result:** Reference count corruption, potential use-after-free

---

### The Add Link Operation: Directory Index Collision

**Function:** `btrfs_add_link()` (fs/btrfs/inode.c:6833)

**Steps:**
1. Insert inode reference (`btrfs_insert_inode_ref`)
2. **If that fails**, try to clean up
3. Insert directory item (`btrfs_insert_dir_item`)
4. **If that fails**, try to delete inode ref (cleanup)
5. Update parent inode size
6. Update parent inode metadata

**Critical Code:**

```c
int btrfs_add_link(...)
{
    // Step 1: Add inode reference
    if (add_backref) {
        ret = btrfs_insert_inode_ref(trans, root, name, ino, parent_ino, index);
    }
    
    if (ret)
        return ret;  // Early return - inode ref added but nothing else!

    // Step 2: Add directory item
    ret = btrfs_insert_dir_item(trans, name, parent_inode, &key,
                                btrfs_inode_type(inode), index);
    if (ret == -EEXIST || ret == -EOVERFLOW)
        goto fail_dir_item;  // Cleanup path
    
    // Step 3: Update directory size
    btrfs_i_size_write(parent_inode, parent_inode->vfs_inode.i_size + name->len * 2);
    
    // Step 4: Update directory inode
    ret = btrfs_update_inode(trans, parent_inode);
    return ret;

fail_dir_item:
    // Cleanup: Try to remove the inode ref we just added
    if (add_backref) {
        ret2 = btrfs_del_inode_ref(trans, root, name, ino, parent_ino, NULL);
        if (ret2)
            btrfs_abort_transaction(trans, ret2);  // Double fault!
    }
    return ret;
}
```

**The Race:**

```
Thread A: link("foo", "/dir/bar")
  Time 0: btrfs_insert_inode_ref() succeeds ← Inode ref added
  Time 1: [WINDOW OPEN]
  Time 2: btrfs_insert_dir_item() returns -EEXIST ← Name collision!
  Time 3: goto fail_dir_item
  Time 4: Try to remove inode ref (cleanup)

Thread B: stat("/dir/bar") during Time 1-2
  Result: Inode ref exists, but no directory entry!
         stat() might find inode via other path, but state is inconsistent.

Thread C: link("foo", "/dir2/bar") during Time 1-2
  Result: BOTH threads trying to add inode refs!
         If they succeed in order: ref count = 2
         But only ONE directory entry exists.
         When one is unlinked: ref count = 1, but NO directory entries!
         **LOST LINK!**
```

---

## The Transaction System: Why Races Still Exist

**Btrfs uses transactions** (`struct btrfs_trans_handle`)

**You might think:** "Transactions prevent races!"

**Reality:** Transactions provide **crash consistency**, not **concurrency control**!

### What Transactions Guarantee

```c
trans = btrfs_start_transaction(root, num_items);

// All operations in transaction are:
// 1. Atomic on crash (either all or none persist to disk)
// 2. Isolated from other transactions on disk

btrfs_end_transaction(trans);
```

### What Transactions DON'T Guarantee

```c
// Transaction A
trans_A = btrfs_start_transaction(...);
btrfs_insert_inode_ref(trans_A, ...);  // Step 1
// ← **OTHER THREADS CAN RUN HERE!**
btrfs_insert_dir_item(trans_A, ...);   // Step 2
btrfs_end_transaction(trans_A);

// Transaction B (concurrent)
trans_B = btrfs_start_transaction(...);
// ← Can read state between Step 1 and Step 2 of Trans A!
btrfs_lookup_dir_item(trans_B, ...);
btrfs_end_transaction(trans_B);
```

**Key Insight:** Transactions serialize *on-disk* writes, not *in-memory* operations!

---

## Critical Lock-Free Windows

### Window 1: Between Unlink and Add Link (Rename)

**Location:** `btrfs_rename()` line 8558-8583

```c
ret = __btrfs_unlink_inode(trans, BTRFS_I(old_dir),
                           BTRFS_I(d_inode(old_dentry)),
                           &old_fname.disk_name, &rename_ctx);
// ↑ **FILE REMOVED FROM OLD DIRECTORY**

if (unlikely(ret)) {
    btrfs_abort_transaction(trans, ret);
    goto out_fail;
}

ret = btrfs_update_inode(trans, BTRFS_I(old_inode));
// ← **LOCK-FREE WINDOW: File exists but in NO directory!**

if (unlikely(ret)) {
    btrfs_abort_transaction(trans, ret);
    goto out_fail;
}

// ... handle new_inode if it exists ...

ret = btrfs_add_link(trans, BTRFS_I(new_dir), BTRFS_I(old_inode),
                     &new_fname.disk_name, 0, index);
// ↑ **FILE ADDED TO NEW DIRECTORY**
```

**How to Inject Race:**

Hook at `__btrfs_unlink_inode` exit:
```c
SEC("kretprobe/__btrfs_unlink_inode")
int hook_unlink_exit(struct pt_regs *ctx) {
    // File is now removed from old directory
    // But not yet added to new directory (in rename)
    
    if (in_rename_context()) {  // Need to detect this
        bpf_busy_wait(10000);  // 10μs delay
        // Other threads can now see file missing!
    }
    return 0;
}
```

### Window 2: Between Path Free and Inode Ref Delete (Unlink)

**Location:** `__btrfs_unlink_inode()` line 4262-4277

```c
ret = btrfs_delete_one_dir_name(trans, root, path, di);

/*
 * Down the call chains below we'll also need to allocate a path, so no
 * need to hold on to this one for longer than necessary.
 */
btrfs_free_path(path);  // ← **RELEASES BTREE LOCKS!**
if (ret)
    return ret;

// ... [LOCK-FREE WINDOW] ...

ret = btrfs_del_inode_ref(trans, root, name, ino, dir_ino, &index);
// ↑ Inode ref deleted
```

**How to Inject Race:**

```c
SEC("kprobe/btrfs_free_path")
int hook_free_path(struct pt_regs *ctx) {
    // Check if we're in __btrfs_unlink_inode
    if (in_unlink_context()) {
        bpf_busy_wait(5000);  // 5μs delay
        // Directory name deleted, but inode ref still exists
        // Other operations can see inconsistent state!
    }
    return 0;
}
```

### Window 3: Between Inode Ref Insert and Dir Item Insert (Add Link)

**Location:** `btrfs_add_link()` line 6853-6868

```c
if (add_backref) {
    ret = btrfs_insert_inode_ref(trans, root, name,
                                 ino, parent_ino, index);
}

/* Nothing to clean up yet */
if (ret)
    return ret;  // ← **EARLY RETURN: Inode ref added, dir item NOT!**

ret = btrfs_insert_dir_item(trans, name, parent_inode, &key,
                            btrfs_inode_type(inode), index);
```

**How to Inject Race:**

```c
SEC("kretprobe/btrfs_insert_inode_ref")
int hook_inode_ref_exit(struct pt_regs *ctx) {
    int ret = PT_REGS_RC(ctx);
    
    if (ret == 0) {  // Success
        bpf_busy_wait(8000);  // 8μs delay
        // Inode ref exists, but directory item doesn't yet
        // Other threads can link/unlink and corrupt ref count!
    }
    return 0;
}
```

---

## Our Test Code Analysis

### What We're Currently Testing

**File:** `chaos/simple_chaos_test.c`

**Operations:**
```c
// Writer threads (line 262-312):
while (!stop) {
    // Create file
    fd = open(filepath, O_CREAT | O_WRONLY, 0644);
    close(fd);
    tracker_record_create(tracker, filename);
    
    // Delete previous file (every 3rd iteration)
    if (file_num > 0 && (file_num % 3) == 0) {
        unlink(filepath);
        tracker_record_delete(tracker, filename);
    }
}

// Reader threads (line 94-204):
while (!stop) {
    dir_reader_open(reader, test_dir);
    tracker_record_read_start(tracker, thread_id);
    
    // Read all entries
    while ((count = dir_reader_read(reader, entries, 100)) > 0) {
        for (int i = 0; i < count; i++) {
            tracker_record_read_entry(tracker, entries[i].name, thread_id);
        }
    }
    
    tracker_record_read_end(tracker, thread_id);
    
    // Validate
    result = tracker_validate_read(tracker, entry_names, total_read, ...);
}
```

**What We're Missing:**

❌ **No rename operations!** This is where the biggest race windows are!  
❌ **No hard links!** This would test reference count races.  
❌ **No symlinks!** Another source of races.

---

## Proposed Test Enhancements

### Enhancement 1: Add Rename Operations

**New writer pattern:**

```c
static void *writer_thread(void *arg) {
    // ...
    while (!atomic_load(&state->stop)) {
        int operation = rand() % 10;
        
        if (operation < 4) {
            // 40%: Create
            fd = open(filepath, O_CREAT | O_WRONLY, 0644);
            close(fd);
            tracker_record_create(tracker, filename);
        }
        else if (operation < 7) {
            // 30%: Delete
            unlink(filepath);
            tracker_record_delete(tracker, filename);
        }
        else {
            // 30%: RENAME ← NEW!
            char old_path[512], new_path[512];
            snprintf(old_path, sizeof(old_path), "%s/file_%d", dir, old_num);
            snprintf(new_path, sizeof(new_path), "%s/file_%d", dir, new_num);
            
            rename(old_path, new_path);
            tracker_record_rename(tracker, old_name, new_name);
        }
    }
}
```

**Expected Bugs:**
- **Missing files**: File invisible during rename (Window 1)
- **Duplicate files**: File visible at both old and new location
- **Lost renames**: Rename completes but file still at old location

### Enhancement 2: Add Link Operations

**New link writer:**

```c
static void *link_writer_thread(void *arg) {
    while (!stop) {
        // Create hard link
        char source[512], link_path[512];
        snprintf(source, sizeof(source), "%s/file_%d", dir, src_num);
        snprintf(link_path, sizeof(link_path), "%s/link_%d", dir, link_num);
        
        link(source, link_path);
        tracker_record_link(tracker, source, link_path);
        
        // Later: unlink one of them
        unlink(link_path);
        tracker_record_unlink(tracker, link_path);
    }
}
```

**Expected Bugs:**
- **Lost links**: Link created but inode ref count not incremented (Window 3)
- **Dangling references**: Inode ref exists but no directory entry
- **Use-after-free**: Inode freed but directory entry still exists

---

## eBPF Injection Strategy

### Strategy 1: Hook at Rename Window

**Target:** The window between unlink and add_link in rename

```c
// File: chaos/rename_race_injector.bpf.c

SEC("kretprobe/__btrfs_unlink_inode")
int inject_rename_delay(struct pt_regs *ctx) {
    int ret = PT_REGS_RC(ctx);
    
    if (ret == 0) {  // Success
        // Check if we're in a rename operation
        // (Would need to track context via map)
        
        if (should_inject_delay()) {
            bpf_busy_wait(15000);  // 15μs delay
            
            // Update stats
            __u32 key = STAT_RENAME_DELAYS;
            __u64 *count = bpf_map_lookup_elem(&stats, &key);
            if (count)
                __sync_fetch_and_add(count, 1);
        }
    }
    
    return 0;
}
```

### Strategy 2: Hook at Path Free Window

**Target:** The window between btrfs_free_path and btrfs_del_inode_ref

```c
SEC("kprobe/btrfs_free_path")
int inject_unlink_delay(struct pt_regs *ctx) {
    // Detect if we're in __btrfs_unlink_inode by checking call stack
    // (Can use bpf_get_stackid for this)
    
    if (in_unlink_path()) {
        if (should_inject_delay()) {
            bpf_busy_wait(10000);  // 10μs delay
        }
    }
    
    return 0;
}
```

### Strategy 3: Hook at Add Link Window

**Target:** The window between inode_ref insert and dir_item insert

```c
SEC("kretprobe/btrfs_insert_inode_ref")
int inject_addlink_delay(struct pt_regs *ctx) {
    int ret = PT_REGS_RC(ctx);
    
    if (ret == 0) {  // Success
        if (should_inject_delay()) {
            bpf_busy_wait(12000);  // 12μs delay
        }
    }
    
    return 0;
}
```

---

## Validation Strategy

### What to Check For

1. **Missing Files** (POSIX violation if not in rename):
   ```
   Thread A: rename("foo", "bar")
   Thread B: getdents64() sees NEITHER "foo" NOR "bar"
   ```

2. **Duplicate Files** (POSIX violation):
   ```
   Thread A: rename("foo", "bar")
   Thread B: getdents64() sees BOTH "foo" AND "bar"
   ```

3. **Lost Renames** (Consistency violation):
   ```
   Before: "foo" exists, "bar" doesn't
   Thread A: rename("foo", "bar") returns success
   After: "foo" still exists, "bar" doesn't
   Result: Rename lost!
   ```

4. **Reference Count Corruption** (Hard links):
   ```
   Before: inode ref count = 1 (one link)
   Thread A: link("foo", "bar")
   Thread B: unlink("foo")
   After: inode ref count = 1, but only "bar" exists
   Expected: ref count = 1, "bar" exists (CORRECT)
   
   BUT if race:
   After: inode ref count = 0 (freed!), but "bar" still exists
   Result: Use-after-free!
   ```

---

## Recommendations

### Immediate Actions

1. **Add rename operations to `simple_chaos_test.c`**
   - 30% of writer operations should be renames
   - Track renames in state_tracker

2. **Implement eBPF hooks at the 3 critical windows**
   - Window 1: After `__btrfs_unlink_inode` in rename
   - Window 2: After `btrfs_free_path` in unlink
   - Window 3: After `btrfs_insert_inode_ref` in add_link

3. **Add hard link testing**
   - Separate link_writer thread
   - Validate reference counts match directory entries

### Long-Term Actions

1. **Implement rename tracking in state_tracker.c**
   - Track rename operations with timestamps
   - Validate that renamed files appear in exactly one location

2. **Add reference count validation**
   - Track number of hard links to each inode
   - Verify ref count matches expected count

3. **Create btrfs-specific test**
   - Test btrfs transaction abort scenarios
   - Test concurrent operations on different subvolumes
   - Test snapshot creation during directory modifications

---

## Expected Results

### Without eBPF Delays

**Prediction:** 0-5 bugs per 100,000 operations

**Why:** Race windows are nanoseconds wide. Natural timing makes them rare.

### With eBPF Delays (5-15μs)

**Prediction:** 50-500 bugs per 100,000 operations

**Why:** Race windows expanded by 10,000x. What was nanoseconds is now microseconds.

**Specific Bugs Expected:**

| Bug Type | Window | Expected Rate |
|----------|--------|---------------|
| Missing file during rename | Window 1 | 5-10% of renames |
| Lost rename | Window 1 | 1-2% of renames |
| Ref count corruption | Window 3 | 10-20% of links |
| Dangling inode ref | Window 2 | 5-10% of unlinks |

---

## Conclusion

**The real bugs are in the modification paths, not the read paths!**

**Key Insights:**

1. **Transactions don't prevent concurrency bugs** - they prevent crash inconsistency
2. **Btrfs has multiple lock-free windows** between compound operation steps
3. **Rename is the most vulnerable** - file temporarily doesn't exist anywhere
4. **Our current tests don't exercise rename** - we need to add it!

**Next Steps:**

1. Enhance `simple_chaos_test.c` with rename operations
2. Implement eBPF hooks at the 3 critical windows
3. Run tests and document bug rates
4. If successful: Report to btrfs maintainers!

---

*Analysis based on Linux kernel 6.14 btrfs code*  
*Created: 2025-10-13*

