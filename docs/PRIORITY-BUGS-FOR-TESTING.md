# Priority Bugs for Xibalba Testing

## Purpose
This document lists specific real-world filesystem bugs that Xibalba should be tested against to validate its bug detection capabilities. These are bugs that **actually happened** and evaded detection for extended periods.

---

## HIGH PRIORITY: Race Condition Bugs

### 1. OCFS2 Hole Punching Race (CVE-2024-40943)
**Why test this:** Perfect test case for Xibalba's pause injection!

**The Bug:**
- Race between `fallocate(PUNCH_HOLE)` and asynchronous direct I/O writes
- Caused on-disk corruption
- Timing-dependent

**Links & References:**
- **CVE:** CVE-2024-40943
- **CVE Details:** https://www.rapid7.com/db/vulnerabilities/oracle_linux-cve-2024-40943/
- **Discovered:** July 12, 2024
- **Error Message:** "Owner 5668 has an extent at cpos 78723 which can no longer be found"
- **Kernel Commit:** Search git.kernel.org for CVE-2024-40943
- **Affected:** Oracle Cluster File System 2 (OCFS2)

**Test Scenario:**
```c
// Thread 1:
fd = open("testfile", O_RDWR);
pwrite(fd, buf, size, offset);  // Write data

// Thread 2 (concurrent):
fallocate(fd, FALLOC_FL_PUNCH_HOLE, offset, size);  // Punch hole

// Xibalba: Inject pauses between operations
// Expected: Detect corruption or inconsistency
```

**Xibalba Strategy:**
1. Create file with data
2. Launch concurrent threads: one punching holes, one doing AIO+DIO writes
3. Inject pauses at various points in each thread
4. Run fsck after each test
5. Verify file contents

**Expected Result:** Should expose the race condition consistently.

---

### 2. OpenZFS Block Cloning Corruption (2023)
**Why test this:** Bug was present for ~17 years!

**The Bug:**
- Silent data corruption during file copy operations
- Exposed by block cloning feature (fast copy)
- Timing-dependent corruption

**Links & References:**
- **Discovered:** December 2023
- **Fixed in:** OpenZFS 2.2.2 and 2.1.14
- **Article:** https://www.theregister.com/2023/12/04/two_new_versions_of_openzfs
- **GitHub Issues:** Likely #15526 or #15571 (search https://github.com/openzfs/zfs/issues)
- **Related Function:** `dmu_buf_will_clone` in block cloning code
- **Duration:** Present since approximately 2006 (~17 years!)
- **Platforms:** Linux and FreeBSD

**Test Scenario:**
```bash
# Create source file with known data
dd if=/dev/urandom of=source bs=1M count=100

# Rapid concurrent copies (triggers block cloning)
for i in {1..10}; do
    cp --reflink=auto source dest_$i &
done
wait

# Xibalba: Inject pauses during copy operations
# Expected: Data verification should fail
```

**Xibalba Strategy:**
1. Create files with known checksums
2. Perform concurrent `cp --reflink=auto` operations
3. Inject pauses during copy system calls
4. Checksum all copies
5. Look for mismatches

**Expected Result:** Should catch data corruption during cloning.

---

## HIGH PRIORITY: Crash Consistency Bugs

### 3. ext4 Delayed Allocation Data Loss
**Why test this:** Classic crash consistency bug that affected many users.

**The Bug:**
- ext4 delays block allocation for performance
- If system crashes before allocation, data lost
- Application writes don't immediately persist

**Links & References:**
- **Timeline:** 2008-2009 (ext4 introduction)
- **Mitigation:** Kernel 2.6.30+ (auto-fsync heuristics added)
- **Wikipedia:** https://en.wikipedia.org/wiki/Ext4#Delayed_allocation_and_potential_data_loss
- **Discussion:** Theodore Ts'o (ext4 maintainer) blog posts and LKML discussions
- **Behavior Change:** ext3 wrote data before metadata; ext4 delays both
- **Impact:** Applications using rename() for atomic updates without fsync() lose data

**Test Scenario:**
```c
// Application code (typical pattern that loses data):
fd = open("important.txt", O_WRONLY | O_TRUNC | O_CREAT, 0644);
write(fd, data, size);
close(fd);  // NO fsync()!
// <-- CRASH HERE = data loss

// After reboot: file is zero bytes or has old contents
```

**Xibalba Strategy:**
1. Create test workload: write to file, close without fsync
2. Inject "crash" after close() but before delayed allocation completes
3. "Reboot" and check file contents
4. Verify data persistence

**Expected Result:** Should demonstrate data loss without fsync.

**How to Fix (for validation):**
- Add `fsync()` before close
- Use `O_SYNC` flag
- Test that fixes work

---

### 4. ReiserFS rename() Non-Atomicity
**Why test this:** Demonstrates non-synchronous directory operations.

**The Bug:**
- `rename()` is not synchronous on ReiserFS
- System crash during rename can lose or corrupt files
- Particularly bad for applications using file locks

**Links & References:**
- **Status:** Longstanding known issue
- **Wikipedia:** https://en.wikipedia.org/wiki/ReiserFS#Criticism
- **Deprecated:** Linux 5.18 (2022)
- **Marked Obsolete:** Linux 6.6 (2023)
- **Removed:** Linux 6.13 (2025)
- **Impact:** Mail transfer agents (qmail, Postfix) particularly affected
- **Filesystem Status:** No longer maintained, removed from kernel

**Test Scenario:**
```c
// Safe file replacement pattern (atomic on most filesystems):
write_file("temp.txt", new_data);
fsync_file("temp.txt");
rename("temp.txt", "important.txt");  // Should be atomic!
// <-- CRASH HERE

// Expected: Either old or new important.txt exists
// ReiserFS: Might have neither, or corrupted file
```

**Xibalba Strategy:**
1. Implement safe file replacement pattern
2. Inject crash immediately after rename()
3. Verify atomic behavior (old or new, never neither)
4. Test on ext4 (should pass) vs ReiserFS (should fail)

**Expected Result:** ReiserFS should violate atomicity guarantees.

---

### 5. Btrfs RAID5/6 Write Hole
**Why test this:** Longstanding known issue, good validation target.

**The Bug:**
- During stripe write (data + parity update), crash can leave inconsistent state
- Parity doesn't match data
- Corruption during reconstruction

**Links & References:**
- **Status:** Still present (as of 2024)
- **Official Warning:** "RAID5 and RAID6 are not recommended for production use"
- **Btrfs Wiki:** https://btrfs.wiki.kernel.org/index.php/RAID56
- **Issue:** Write hole during parity updates
- **Problem:** Scrub cannot detect/fix all corruption cases
- **Duration:** Known for years, still unresolved
- **Alternative:** Use btrfs RAID1/10 instead

**Test Scenario:**
```bash
# Setup Btrfs RAID5
mkfs.btrfs -d raid5 /dev/sd{a,b,c}
mount /dev/sda /mnt/test

# Write data that spans stripes
dd if=/dev/urandom of=/mnt/test/bigfile bs=1M count=1000

# Xibalba: Inject crash during write
# Simulate disk failure and rebuild
# Expected: Data corruption after rebuild
```

**Xibalba Strategy:**
1. Create Btrfs RAID5 filesystem
2. Write data that spans multiple stripes
3. Inject crash during write operation
4. Simulate rebuild scenario
5. Verify data integrity (expect failures)

**Expected Result:** Should demonstrate write hole corruption.

---

## HIGH PRIORITY: Timing/Ordering Bugs

### 6. ext4 iomap Direct I/O Corruption (2023)
**Why test this:** Recent bug, shows interaction between subsystems.

**The Bug:**
- File position not updated after direct I/O writes
- Subsequent writes go to wrong location
- Silent data corruption

**Links & References:**
- **Discovered:** December 2023
- **Affected Kernel:** 6.1.64 stable
- **LKML Discussion:** https://lkml.org/lkml/2023/12/5/646
- **LWN Article:** https://lwn.net/Articles/954770/
- **Debian Bug:** https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=1055005
- **Hacker News:** https://news.ycombinator.com/item?id=38589389
- **Missing Commit:** 936e114a245b6 ("iomap: update ki_pos a little later in iomap_dio_complete")
- **Root Cause:** Backport without prerequisite dependency
- **Fixed in:** Stable kernels 6.5+

**Test Scenario:**
```c
// Direct I/O writes
fd = open("testfile", O_RDWR | O_DIRECT);

// Write 1
pwrite(fd, buf1, 4096, 0);

// Write 2 (should go to offset 4096, but might go to offset 0)
pwrite(fd, buf2, 4096, 4096);

// Xibalba: Inject pause between position update and next write
// Expected: buf2 might overwrite buf1
```

**Xibalba Strategy:**
1. Perform sequential direct I/O writes
2. Inject pauses between writes
3. Verify file contents match expected layout
4. Check for overwrites or corruption

**Expected Result:** Should catch position tracking bugs.

---

## MEDIUM PRIORITY: Metadata Consistency Bugs

### 7. F2FS Garbage Collection Inconsistency (CVE-2025-38164)
**Why test this:** Shows complex state machine bugs.

**The Bug:**
- Garbage collection migrates blocks
- Segment Information Table (SIT) and Segment Summary Area (SSA) become inconsistent
- Stale data in cache causes corruption

**Links & References:**
- **CVE:** CVE-2025-38164
- **Discovered:** July 3, 2025
- **CVE Details:** https://www.wiz.io/vulnerability-database/cve/cve-2025-38164
- **Function:** `f2fs_gc_range()` 
- **Issue:** SSA blocks not updated due to stale `curseg` cache data
- **Impact:** Filesystem corruption during garbage collection
- **Filesystem:** Flash-Friendly File System (F2FS)

**Test Scenario:**
```bash
# Fill filesystem to trigger GC
dd if=/dev/urandom of=/mnt/test/fill bs=1M count=1000

# Delete some files to create fragmentation
rm /mnt/test/fill

# Write more data to trigger GC
dd if=/dev/urandom of=/mnt/test/data bs=1M count=500

# Xibalba: Inject pauses during GC
# Expected: SIT/SSA inconsistency
```

**Xibalba Strategy:**
1. Fill F2FS filesystem
2. Create fragmentation
3. Trigger garbage collection
4. Inject pauses during GC operations
5. Run fsck to detect SIT/SSA mismatches

**Expected Result:** Should expose metadata inconsistencies.

---

### 8. XFS Metadata Corruption (2020)
**Why test this:** Shows data/metadata confusion.

**The Bug:**
- File data written where inode metadata should be
- Severe filesystem corruption

**Links & References:**
- **Discovered:** March 2020
- **LKML Discussion:** https://lkml.org/lkml/2020/3/31/1555
- **Error Message:** "Unmount and run xfs_repair"
- **Symptom:** File data overwriting inode metadata
- **Cause:** Metadata buffer corruption
- **Fix:** Required `xfs_repair` to recover
- **Impact:** Severe data structure corruption

**Test Scenario:**
```bash
# Concurrent metadata and data operations
# (specific trigger unknown, but general pattern)

# Heavy mixed workload:
while true; do
    # Metadata operations
    mkdir -p dir_$RANDOM
    touch dir_$RANDOM/file_$RANDOM
    
    # Data operations
    dd if=/dev/urandom of=bigfile_$RANDOM bs=1M count=10
done

# Xibalba: Inject pauses between metadata updates
# Expected: fsck detects corruption
```

**Xibalba Strategy:**
1. Run heavy mixed metadata/data workload
2. Inject pauses to expose races
3. Run xfs_repair to check for corruption
4. Look for "data in metadata" errors

---

## Test Implementation Strategy

### Phase 1: Crash Consistency Tests (Easiest)
1. ext4 delayed allocation data loss
2. ReiserFS rename atomicity
3. Btrfs RAID5/6 write hole

**Approach:** Use existing Xibalba crash injection

### Phase 2: Race Condition Tests
1. OCFS2 hole punching race
2. OpenZFS block cloning
3. ext4 iomap direct I/O

**Approach:** Use Xibalba's pause injection with concurrent workloads

### Phase 3: Metadata Consistency Tests
1. F2FS garbage collection
2. XFS metadata corruption

**Approach:** Heavy workloads + pause injection + fsck verification

---

## Test Validation Checklist

For each bug test:

- [ ] Implement minimal reproducer workload
- [ ] Confirm workload runs without Xibalba (baseline)
- [ ] Run with Xibalba pause injection
- [ ] Verify bug detection (corruption, inconsistency, etc.)
- [ ] Test on filesystem version that HAD the bug (if possible)
- [ ] Test on filesystem version with fix (should not detect)
- [ ] Document detection rate and configuration

---

## Success Criteria

**Xibalba is successful if it can:**

1. **Consistently reproduce** at least 4 out of 6 HIGH priority bugs
2. **Detect corruption** that would have been missed by normal testing
3. **Find bugs faster** than the actual detection latency (e.g., minutes vs years)
4. **Generate actionable** bug reports with reproduction steps

---

## CrashMonkey Comparison

**CrashMonkey's Results:**
- Found 24 out of 26 known crash-consistency bugs
- Discovered 10 NEW previously unknown bugs
- Used workloads of ≤3 filesystem operations
- Tested: ext4, btrfs, F2FS, XFS

**Xibalba's Advantage:**
- Also tests race conditions (not just crash consistency)
- Pause injection for timing bugs
- Can test networked/distributed filesystems (future)

**Xibalba's Goal:**
- Match or exceed CrashMonkey's detection rate
- Cover more bug categories (races + crashes)

**CrashMonkey Paper & Resources:**
- **Title:** "Finding Crash-Consistency Bugs with Bounded Black-Box Crash Testing"
- **Authors:** Jayashree Mohan, Ashlie Martinez, Soujanya Ponnapalli, Pandian Raju, Vijay Chidambaram
- **Published:** OSDI 2018
- **arXiv:** https://arxiv.org/abs/1810.02904
- **GitHub:** https://github.com/utsaslab/crashmonkey
- **Author Website:** https://www.cs.utexas.edu/~vijay/
- **Key Finding:** Most bugs reproducible with ≤3 filesystem operations

---

## Resources for Testing

### Primary Sources for Bug Research:

**Linux Kernel Mailing List (LKML):**
- Main archive: https://lkml.org
- Search by CVE, filesystem name, or keywords
- Contains original bug reports and discussions

**Kernel Git Repository:**
- Main repo: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
- Search commits by hash, CVE, or message
- Browse fs/ directory for filesystem-specific changes

**Bug Trackers:**
- Kernel Bugzilla: https://bugzilla.kernel.org
- Debian Bugs: https://bugs.debian.org
- Ubuntu Launchpad: https://bugs.launchpad.net
- Red Hat Bugzilla: https://bugzilla.redhat.com

**OpenZFS Resources:**
- GitHub Issues: https://github.com/openzfs/zfs/issues
- GitHub Commits: https://github.com/openzfs/zfs/commits
- Mailing list archives

**Btrfs Resources:**
- Btrfs Wiki: https://btrfs.wiki.kernel.org
- RAID56 Status: https://btrfs.wiki.kernel.org/index.php/RAID56
- Mailing list: linux-btrfs@vger.kernel.org

**CVE Databases:**
- MITRE CVE: https://cve.mitre.org
- NVD: https://nvd.nist.gov
- Ubuntu Security: https://ubuntu.com/security/cves
- Rapid7 VulnDB: https://www.rapid7.com/db/vulnerabilities/

**News & Analysis:**
- LWN.net: https://lwn.net (excellent technical analysis)
- Phoronix: https://www.phoronix.com (Linux news)
- The Register: https://www.theregister.com (tech news)

### Get Old Kernel Versions (with bugs):
```bash
# For testing bugs that have been fixed
# Use VMs with specific kernel versions

# ext4 iomap bug: kernels 6.1.64 (has bug), 6.5+ (fixed)
# F2FS GC bug: kernels before CVE-2025-38164 fix
# Btrfs scrub: kernels before CVE-2024-26616 fix

# Download old kernels:
# https://kernel.org/pub/linux/kernel/
```

### Filesystem Tools Needed:
- `mkfs.*` for each filesystem
- `fsck.*` / `xfs_repair` / `btrfs check` for verification
- `xfstests` for standard test suite (https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git)
- `blktrace` for I/O tracing
- `strace` for system call tracing
- `perf` for kernel profiling

### Test Environments:
- VMs with different kernel versions (QEMU/KVM)
- Multiple filesystem types
- QEMU with snapshot/restore for crash testing
- Container images with specific kernel versions

### Academic Papers on Filesystem Testing:
- **CrashMonkey (OSDI 2018):** https://arxiv.org/abs/1810.02904
- **ACE (USENIX ATC 2020):** "Finding Semantic Bugs in File Systems with an Extensible Fuzzing Framework"
- **B3 (ASPLOS 2019):** "Finding Crash-Consistency Bugs with Bounded Black-Box Crash Testing"

### Other Testing Tools:
- **Syzkaller:** https://github.com/google/syzkaller (kernel fuzzer that found many bugs)
- **Trinity:** System call fuzzer
- **fsstress:** Filesystem stress tester (part of xfstests)
- **fsx:** File system exerciser from Apple

---

## Documentation Template

For each bug test, document:

```markdown
## Bug: [Name] (CVE-XXXX-XXXXX)

### Original Bug Report
- Link: [LKML/GitHub/etc]
- Discovery date: [date]
- Fixed in: [version]

### Test Implementation
- Workload: [description]
- Xibalba configuration: [pause points, etc.]
- Expected behavior: [what corruption to expect]

### Results
- Detection rate: [X/Y tests]
- Time to detect: [average]
- False positives: [count]
- Notes: [any issues]

### Conclusion
- [ ] Bug successfully detected
- [ ] Xibalba configuration effective
- [ ] Would have found bug earlier than actual discovery
```

---

## Appendix: Direct Bug Links

### LKML Discussions:
- **ext4 iomap corruption (Dec 2023):** https://lkml.org/lkml/2023/12/5/646
- **XFS metadata corruption (Mar 2020):** https://lkml.org/lkml/2020/3/31/1555
- **JFS inode eviction (Aug 2025):** https://lkml.org/lkml/2025/8/8/92

### LWN Articles:
- **ext4 data corruption in stable kernels:** https://lwn.net/Articles/954770/
- **Block layer corruption bug (2018):** https://lwn.net/Articles/774440/
- **RAID 0 corruption (2015):** https://lwn.net/Articles/645720/
- **Trust in filesystems (2023):** https://lwn.net/Articles/951846/

### CVE Details:
- **CVE-2024-40943 (OCFS2):** https://www.rapid7.com/db/vulnerabilities/oracle_linux-cve-2024-40943/
- **CVE-2025-38164 (F2FS):** https://www.wiz.io/vulnerability-database/cve/cve-2025-38164
- **CVE-2024-35807 (ext4):** https://ubuntu.com/security/CVE-2024-35807
- **CVE-2024-26616 (Btrfs):** Search on https://nvd.nist.gov

### Bug Tracker Links:
- **Debian #1055005 (ext4 iomap):** https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=1055005
- **Ubuntu #1796542 (silent corruption):** https://bugs.launchpad.net/bugs/1796542
- **Ubuntu #1806755 (ext4 4.19):** https://bugs.launchpad.net/bugs/1806755
- **Arch Linux ext4 (2012):** https://bbs.archlinux.org/viewtopic.php?id=151341

### News Articles:
- **OpenZFS corruption:** https://www.theregister.com/2023/12/04/two_new_versions_of_openzfs
- **ext4 corruption hits Debian:** https://www.theregister.com/2023/12/12/kernel_6_1_ext4_corruption/

### Wikipedia References:
- **ext4 delayed allocation:** https://en.wikipedia.org/wiki/Ext4#Delayed_allocation_and_potential_data_loss
- **ReiserFS criticism:** https://en.wikipedia.org/wiki/ReiserFS#Criticism

### Filesystem-Specific Wikis:
- **Btrfs RAID56:** https://btrfs.wiki.kernel.org/index.php/RAID56
- **Btrfs Status:** https://btrfs.wiki.kernel.org/index.php/Status

### Research Papers & Tools:
- **CrashMonkey paper:** https://arxiv.org/abs/1810.02904
- **CrashMonkey code:** https://github.com/utsaslab/crashmonkey
- **Syzkaller:** https://github.com/google/syzkaller
- **xfstests:** https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git

### Community Discussions:
- **Hacker News - ext4 corruption:** https://news.ycombinator.com/item?id=38589389
- **Phoronix Forums - ext4 bug:** https://www.phoronix.com/forums/forum/software/general-linux-open-source/32852-ext4-data-corruption-bug-hits-stable-linux-kernels

---

*Created: October 12, 2025*
*Last Updated: October 12, 2025*
*For Xibalba Filesystem Testing Framework*

**Note:** Some links (especially kernel git commits and OpenZFS GitHub issues) require manual lookup by commit hash or issue number. See the bug-specific "Links & References" sections above for search hints.

