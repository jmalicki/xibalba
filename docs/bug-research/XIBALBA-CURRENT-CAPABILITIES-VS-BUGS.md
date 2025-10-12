# Xibalba Current Capabilities vs. Priority Bugs

## What Xibalba Can Do RIGHT NOW

Based on analysis of the codebase:

### ✅ Current Capabilities:

1. **eBPF Pause Injection** (`chaos/pause_injector.bpf.c`, `chaos/pause_controller.c`)
   - Injects delays into `getdents64` syscalls
   - Configurable probability (0-100%)
   - Configurable delay iterations (busy-wait loop)
   - Can widen race windows by 10,000x!

2. **Concurrent Operations Testing** (`chaos/simple_chaos_test.c`)
   - Multiple reader threads (directory scans)
   - Multiple writer threads (create/delete files)
   - Tests concurrent access to same directory

3. **State Tracking & Validation** (`common/state_tracker.c`)
   - Ground truth tracking of all operations
   - Detects: missing entries, duplicates, phantom entries
   - Vector clock-based causality tracking

4. **Multiple Consistency Models**
   - POSIX compliance
   - Weak POSIX (causality-based)
   - Strict/Linearizable
   - Eventual consistency

5. **Bug Detection & Reporting**
   - Real-time bug detection
   - JSONL output for analysis
   - Progress tracking

### ❌ What Xibalba CANNOT Do Yet:

1. **Crash Simulation** - No crash injection capability
2. **Post-Crash Verification** - Can't validate state after "reboot"
3. **Direct I/O Testing** - No O_DIRECT workloads
4. **Data Checksumming** - No file content verification
5. **fsync Testing** - No explicit fsync testing
6. **rename() Atomicity** - No rename testing
7. **Hole Punching** - No fallocate(PUNCH_HOLE) testing
8. **AIO/DIO** - No async I/O testing
9. **File Content Verification** - Only tests directory entries, not data

---

## Gap Analysis: Priority Bugs vs. Current Capabilities

### 🟢 CLOSEST MATCH: ext4 iomap Direct I/O Corruption

**Bug Requirements:**
- ✅ Pause injection (HAVE THIS!)
- ❌ Direct I/O workload (NEED THIS)
- ❌ Data verification (NEED THIS)

**Gap Size:** **SMALL** - Only missing test workload

**What to Add:**
1. Simple test program that does Direct I/O writes:
   ```c
   fd = open("testfile", O_RDWR | O_DIRECT);
   pwrite(fd, buf1, 4096, 0);      // Write at offset 0
   pwrite(fd, buf2, 4096, 4096);   // Write at offset 4096
   ```

2. Pause injection between writes (ALREADY HAVE THIS!)

3. Verify file contents match expected layout:
   ```c
   read(fd, verify_buf, 8192);
   // Check: first 4096 bytes == buf1, next 4096 == buf2
   ```

**Estimated Effort:** 2-3 hours
- Write simple Direct I/O test program
- Add data pattern generation (e.g., fill with known values)
- Add verification logic
- Hook up to existing pause controller

**Detection Confidence:** HIGH
- The bug is a timing issue with position updates
- Pause injection should expose it consistently
- Would reproduce the Dec 2023 ext4 bug!

---

### 🟡 MEDIUM MATCH: ext4 Delayed Allocation Data Loss

**Bug Requirements:**
- ✅ File create/write operations (HAVE THIS!)
- ❌ Crash injection (DON'T HAVE)
- ❌ Post-crash verification (DON'T HAVE)
- ❌ fsync testing (DON'T HAVE)

**Gap Size:** **MEDIUM** - Need crash simulation framework

**What to Add:**
1. Crash injection mechanism:
   - Option 1: VM snapshot/restore
   - Option 2: SIGKILL + remount
   - Option 3: Power-loss simulation

2. Test workload:
   ```c
   fd = open("file", O_CREAT | O_WRONLY);
   write(fd, data, size);
   close(fd);  // NO fsync!
   // <-- INJECT CRASH HERE
   ```

3. Post-crash verification:
   - Remount filesystem
   - Check if file exists
   - Check if file has data or is zero-length

**Estimated Effort:** 1-2 days
- Design crash injection mechanism
- Implement filesystem remount
- Add pre/post crash state comparison
- Test with/without fsync

**Detection Confidence:** HIGH
- Well-understood bug
- Easy to reproduce with crash timing
- Would demonstrate crash consistency bugs

---

### 🟡 MEDIUM MATCH: ReiserFS rename() Non-Atomicity

**Bug Requirements:**
- ❌ rename() operations (DON'T HAVE)
- ❌ Crash injection (DON'T HAVE)
- ❌ Atomicity verification (DON'T HAVE)

**Gap Size:** **MEDIUM** - Need crash + rename testing

**What to Add:**
- Same crash mechanism as #2 above
- rename() test pattern:
  ```c
  write_file("temp.txt", data);
  fsync_file("temp.txt");
  rename("temp.txt", "important.txt");
  // <-- INJECT CRASH HERE
  // Verify: either old or new file exists, never neither
  ```

**Estimated Effort:** 1-2 days (similar to #2)

**Detection Confidence:** HIGH
- Classic atomicity violation
- ReiserFS is known to fail this

---

### 🔴 FAR MATCH: OCFS2 Hole Punching Race

**Bug Requirements:**
- ❌ fallocate(PUNCH_HOLE) (DON'T HAVE)
- ❌ Concurrent AIO+DIO writes (DON'T HAVE)
- ✅ Pause injection (HAVE THIS!)
- ❌ Data verification (DON'T HAVE)

**Gap Size:** **LARGE** - Need new syscall testing + AIO

**What to Add:**
1. Hole punching support
2. AIO/DIO infrastructure
3. Concurrent thread coordination
4. Data integrity checking

**Estimated Effort:** 3-5 days

**Detection Confidence:** MEDIUM-HIGH
- Requires getting AIO+DIO+hole-punch timing right
- Pause injection should help

---

### 🔴 FAR MATCH: OpenZFS Block Cloning

**Bug Requirements:**
- ❌ Block cloning/reflink support (DON'T HAVE)
- ❌ File copy testing (DON'T HAVE)
- ✅ Pause injection (HAVE THIS!)
- ❌ Data verification (DON'T HAVE)

**Gap Size:** **LARGE** - Need OpenZFS + reflink testing

**What to Add:**
1. ZFS filesystem support
2. cp --reflink testing
3. Data checksumming
4. Concurrent copy operations

**Estimated Effort:** 2-4 days

**Detection Confidence:** MEDIUM
- Bug is timing-dependent
- Pause injection might help but bug is subtle

---

### 🔴 VERY FAR: Btrfs RAID5/6 Write Hole

**Gap Size:** **VERY LARGE** - Need complete RAID infrastructure

**Estimated Effort:** 1-2 weeks

**Detection Confidence:** MEDIUM
- Complex setup required
- Would need multi-device testing

---

## Recommended Implementation Order

### Phase 1: Quick Win (2-3 hours)
**Target: ext4 iomap Direct I/O Bug**

Why:
- Uses existing pause injection
- Minimal new code needed
- High detection confidence
- Recent real-world bug (Dec 2023)

Steps:
1. Create `chaos/direct_io_test.c`
2. Implement Direct I/O write workload
3. Add data pattern verification
4. Test with pause controller
5. Verify bug detection

**Success Metric:** Detect data corruption when writes overlap due to position tracking bug

---

### Phase 2: Crash Consistency (1-2 days)
**Target: ext4 Delayed Allocation + ReiserFS rename()**

Why:
- Fundamental capability (crash testing)
- Enables testing many bugs
- Well-understood test patterns
- High value for future tests

Steps:
1. Design crash injection mechanism (VM or SIGKILL)
2. Implement filesystem remount logic
3. Add pre/post-crash state comparison
4. Test delayed allocation bug
5. Test rename atomicity bug

**Success Metric:** Detect data loss without fsync; detect rename non-atomicity

---

### Phase 3: Advanced Race Conditions (3-5 days)
**Target: OCFS2 Hole Punching, others**

Why:
- More complex bugs
- Requires AIO infrastructure
- Lower ROI than Phases 1-2

---

## Implementation Recommendation

### START HERE: Direct I/O Bug Test

Create `chaos/direct_io_race_test.c`:

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BLOCK_SIZE 4096

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <test_file>\n", argv[0]);
        return 1;
    }
    
    const char *filename = argv[1];
    
    // Allocate aligned buffers for O_DIRECT
    void *buf1, *buf2;
    posix_memalign(&buf1, 4096, BLOCK_SIZE);
    posix_memalign(&buf2, 4096, BLOCK_SIZE);
    
    // Fill with known patterns
    memset(buf1, 0xAA, BLOCK_SIZE);  // Pattern A
    memset(buf2, 0xBB, BLOCK_SIZE);  // Pattern B
    
    // Open with O_DIRECT
    int fd = open(filename, O_RDWR | O_CREAT | O_DIRECT, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    // Sequential Direct I/O writes
    printf("Writing pattern A at offset 0...\n");
    pwrite(fd, buf1, BLOCK_SIZE, 0);
    
    // THIS IS WHERE PAUSE INJECTION SHOULD EXPOSE THE BUG!
    // If position not updated, next write might go to wrong location
    
    printf("Writing pattern B at offset 4096...\n");
    pwrite(fd, buf2, BLOCK_SIZE, BLOCK_SIZE);
    
    close(fd);
    
    // Verify file contents
    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open for verify");
        return 1;
    }
    
    void *verify_buf;
    posix_memalign(&verify_buf, 4096, BLOCK_SIZE * 2);
    
    ssize_t bytes = read(fd, verify_buf, BLOCK_SIZE * 2);
    if (bytes != BLOCK_SIZE * 2) {
        fprintf(stderr, "Failed to read back data\n");
        return 1;
    }
    
    // Check pattern A at offset 0
    if (memcmp(verify_buf, buf1, BLOCK_SIZE) != 0) {
        fprintf(stderr, "❌ BUG DETECTED: Pattern A corrupted at offset 0!\n");
        return 1;
    }
    
    // Check pattern B at offset 4096
    if (memcmp(verify_buf + BLOCK_SIZE, buf2, BLOCK_SIZE) != 0) {
        fprintf(stderr, "❌ BUG DETECTED: Pattern B corrupted at offset 4096!\n");
        fprintf(stderr, "Likely cause: File position not updated between writes\n");
        fprintf(stderr, "This is the ext4 iomap Direct I/O bug (Dec 2023)!\n");
        return 1;
    }
    
    printf("✅ Data verified correctly - no corruption detected\n");
    
    close(fd);
    free(buf1);
    free(buf2);
    free(verify_buf);
    
    return 0;
}
```

**Usage:**
```bash
# Terminal 1: Start pause injection
bazel run //chaos:pause_controller -- 50 500

# Terminal 2: Run test
bazel run //chaos:direct_io_race_test -- /tmp/testfile

# Expected with bug: Corruption detected
# Expected without bug or without pauses: Success
```

**This test would have caught the Dec 2023 ext4 bug!**

---

## Summary

**Immediate Action:** Implement Direct I/O race test
- **Time:** 2-3 hours
- **Value:** HIGH (detects real 2023 bug)
- **Uses existing:** Pause injection
- **Adds minimal:** Direct I/O workload + verification

**Next Action:** Add crash injection framework
- **Time:** 1-2 days
- **Value:** VERY HIGH (enables many tests)
- **Unlocks:** Delayed allocation, rename atomicity, and more

**Future:** Advanced race conditions requiring AIO/hole-punch

---

*Created: October 12, 2025*
*Analysis for Xibalba Bug Detection Roadmap*

