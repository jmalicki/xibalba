# Linux Filesystem Corruption Bugs Catalog

## Executive Summary

**Purpose:** Catalog real-world filesystem corruption bugs to evaluate Xibalba's detection potential.

**Key Findings:**
- **16 significant bugs** documented from 2018-2025
- **Multiple bugs** went undetected for **years** (OpenZFS bug present since ~2006)
- **6 bugs** are high-priority targets for Xibalba testing (marked with ✓ below)
- **Race conditions and crash consistency issues** dominate the bug catalog
- Academic research (CrashMonkey) found **10 NEW bugs** using similar fault injection methodology

**Xibalba's Sweet Spot:**
1. **Race condition bugs** (OCFS2 hole punching, OpenZFS block cloning)
2. **Crash consistency bugs** (ext4 delayed allocation, rename atomicity)
3. **Multi-step operation bugs** (F2FS garbage collection, Btrfs RAID5/6)
4. **Timing-dependent corruption** (ext4 iomap direct I/O)

**Bottom Line:** Many of these bugs could likely have been caught by Xibalba's pause injection and crash simulation. The CrashMonkey research validates this approach—they found 10 new bugs using bounded crash testing with small workloads (≤3 operations).

---

## Overview

This document catalogs notable subtle Linux filesystem corruption bugs from recent years (2018-2025) that evaded detection for extended periods. These bugs are particularly interesting for evaluating whether Xibalba's fault injection and testing framework could have detected them.

Each bug is analyzed for:
- Technical details and root causes
- How long it went undetected
- Links to LKML discussions, CVEs, and bug reports
- **Xibalba detection potential**

---

## Recent High-Profile Bugs (2023-2025)

### 1. OpenZFS Block Cloning Data Corruption (2023)
**Discovered:** December 2023  
**Affected Versions:** OpenZFS versions prior to 2.2.2/2.1.14  
**Duration:** Present for years, possibly since 2006  

**Description:**
A longstanding bug in OpenZFS occasionally corrupted data during file copy operations. The bug existed for years but became more apparent with OpenZFS 2.2.0's introduction of a faster copy function (block cloning), which increased the likelihood of triggering the corruption.

**Key Details:**
- Silent data corruption during file copies
- Exposed by performance optimization (block cloning feature)
- Affected both Linux and FreeBSD systems
- Fixed in versions 2.2.2 and 2.1.14

**Links:**
- The Register article: https://www.theregister.com/2023/12/04/two_new_versions_of_openzfs
- Related to `dmu_buf_will_clone` functionality
- Potentially related GitHub issues: #15526, #15571 (needs verification)

**Xibalba Relevance:**
Could Xibalba detect this? This involves concurrent file operations and data verification. The bug was timing-dependent, which is exactly what Xibalba's pause injection is designed to expose.

---

### 2. ext4 iomap Direct I/O Corruption (December 2023)
**CVE:** None assigned  
**Affected Kernel:** 6.1 stable series  
**LKML Discussion:** https://lkml.org/lkml/2023/12/5/646  

**Description:**
A subtle interaction between the iomap code and ext4 caused data corruption during direct I/O writes. The issue arose because commit 936e114a245b6 ("iomap: update ki_pos a little later in iomap_dio_complete") was missing from the stable kernel.

**Key Details:**
- File position not updated correctly after direct I/O writes
- Data written to incorrect locations
- Affected stable kernels before 6.5
- Required removal of partial backport from stable trees

**Technical Details:**
```
Missing commit: 936e114a245b6
Issue: File position update timing in iomap_dio_complete()
Impact: Silent data corruption during direct I/O operations
```

**Xibalba Relevance:**
This is a crash consistency issue that involves precise ordering of operations. Xibalba's pause injection could expose race conditions in the iomap/ext4 interaction.

---

### 3. F2FS Garbage Collection Corruption (July 2025)
**CVE:** CVE-2025-38164  
**Discovered:** July 3, 2025  
**Links:**
- Wiz vulnerability database: https://www.wiz.io/vulnerability-database/cve/cve-2025-38164

**Description:**
Inconsistencies between the Segment Information Table (SIT) and Segment Summary Area (SSA) during garbage collection operations led to filesystem corruption.

**Key Details:**
- Bug in `f2fs_gc_range()` function
- SSA blocks not updated due to stale data in `curseg` cache
- Caused mismatches between SIT and SSA
- Potential data loss

**Technical Details:**
- Function: `f2fs_gc_range()`
- Issue: Attempts to migrate blocks in curseg without updating SSA
- Root cause: Stale summary block data in curseg cache

**Xibalba Relevance:**
Garbage collection is a complex multi-step operation. Xibalba could inject pauses between GC steps to expose state inconsistencies.

---

## 2024 Notable Bugs

### 4. ext4 Online Resize Corruption (May 2024)
**CVE:** CVE-2024-35807  
**Discovered:** May 17, 2024  
**Links:**
- Ubuntu Security: https://ubuntu.com/security/CVE-2024-35807

**Description:**
Resizing ext4 filesystems larger than 16 TiB with 4k block size could lead to corruption when crossing an 8 GiB boundary.

**Key Details:**
- Only affects very large filesystems (>16 TiB)
- Issue with `resize_inode` feature disabled (default for such sizes)
- Problem with meta block group handling
- `ext4_flex_group_add` adds block groups incorrectly

**Technical Details:**
- Filesystem converted to `metabg` starting at `s_first_meta_bg`
- `ext4_flex_group_add` might add groups not yet part of first meta block group
- Results in corrupted block descriptors

**Xibalba Relevance:**
This is a metadata consistency issue. Xibalba could potentially detect this by testing filesystem resize operations under different conditions.

---

### 5. Btrfs Scrub Use-After-Free (2024)
**CVE:** CVE-2024-26616  
**Links:**
- Radar OffSeq: https://radar.offseq.com/threat/cve-2024-26616

**Description:**
When scrubbing a Btrfs filesystem converted from ext4, misaligned data chunks (not aligned to 64KB boundary) triggered a use-after-free condition.

**Key Details:**
- Only affects filesystems converted from ext4
- Data chunks not aligned to 64KB boundary
- Bio splits into multiple parts with multiple endio callbacks
- Scrub code assumes single endio per bio
- Leads to kernel memory corruption and crashes

**Technical Details:**
- Issue in scrub functionality: `fs/btrfs/scrub.c`
- Root cause: Incorrect assumption about bio endio callbacks
- Trigger: Data chunks with alignment < 64KB

**Xibalba Relevance:**
While this is a kernel memory corruption issue, it's triggered by filesystem metadata state. Xibalba's ability to create unusual filesystem states could potentially expose this.

---

### 6. OCFS2 Hole Punching Race (July 2024)
**CVE:** CVE-2024-40943  
**Discovered:** July 12, 2024  
**Links:**
- Rapid7 VulnDB: https://www.rapid7.com/db/vulnerabilities/oracle_linux-cve-2024-40943/

**Description:**
Race condition between hole punching and asynchronous direct I/O (AIO+DIO) operations led to on-disk corruption.

**Key Details:**
- Race between `fallocate(PUNCH_HOLE)` and AIO+DIO
- Results in on-disk metadata corruption
- Error message: "Owner 5668 has an extent at cpos 78723 which can no longer be found"
- Timing-dependent corruption

**Xibalba Relevance:**
This is exactly the type of race condition Xibalba is designed to find! Pausing between concurrent operations (hole punch vs. AIO writes) could expose this consistently.

---

### 7. XFS Quota Lock Hang and Corruption (2024/2025)
**CVE:** CVE-2024-55641  
**Published:** January 11, 2025  
**Links:**
- Ubuntu Security: https://ubuntu.com/security/CVE-2024-55641

**Description:**
`link()` system call could cause filesystem hang and potential corruption during quota reservation failures.

**Key Details:**
- Triggered when `link()` attempts to set up transaction
- Quota reservation failure leads to inode locking issues
- Can cause filesystem hang
- Potential for corruption during recovery

**Xibalba Relevance:**
Transaction handling and quota operations are areas where Xibalba's pause injection could expose race conditions.

---

## 2020-2023 Notable Bugs

### 8. XFS Metadata Corruption (March 2020)
**LKML Discussion:** https://lkml.org/lkml/2020/3/31/1555

**Description:**
Metadata corruption in XFS where file data was written in place of inode metadata.

**Key Details:**
- Severe metadata corruption
- File data overwriting inode metadata
- Required `xfs_repair` to fix
- Traced to specific kernel changes

**Error Messages:**
```
Unmount and run xfs_repair
Metadata corruption detected at ...
```

**Xibalba Relevance:**
Metadata/data confusion suggests ordering issues that Xibalba's pause injection might expose.

---

### 9. JFS Inode Eviction Bug (August 2025)
**LKML Discussion:** https://lkml.org/lkml/2025/8/8/92

**Description:**
Invalid inode state handling during cleanup phase led to kernel bugs during inode eviction.

**Key Details:**
- Function: `jfs_evict_inode` in `fs/jfs/inode.c`
- Triggered with specific non-default mount parameters
- Discovered using Syzkaller (fuzzer)
- Invalid inode state transitions

**Xibalba Relevance:**
Inode state transitions during cleanup are complex. Xibalba could test various mount options and operation sequences.

---

## Longstanding Issues

### 10. ext4 Delayed Allocation Data Loss (2008-2009, ongoing discussion)
**Context:** Introduction of delayed allocation in ext4  
**Kernel versions:** Fixed/mitigated in 2.6.30+  

**Description:**
ext4's delayed allocation feature changed behavior from ext3, creating risks of data loss when systems crash before data is written to disk.

**Key Details:**
- Performance optimization that changed crash semantics
- Applications that relied on ext3 behavior affected
- Data might be lost if system crashes before write
- Applications should use `fsync()` but many don't
- Auto-fsync patches added in 2.6.30

**Classic Scenario:**
```
1. Application writes to file
2. Application closes file (no fsync)
3. ext4 delays allocation
4. System crashes
5. File contains zeros or old data
```

**Xibalba Relevance:**
This is a crash consistency issue! Xibalba's crash injection could systematically test the window between write and allocation.

---

## Academic Research: CrashMonkey & ACE

### CrashMonkey: Finding Crash-Consistency Bugs (2018)
**Paper:** "Finding Crash-Consistency Bugs with Bounded Black-Box Crash Testing"  
**Authors:** Jayashree Mohan, Ashlie Martinez, Soujanya Ponnapalli, Pandian Raju, Vijay Chidambaram  
**Published:** OSDI 2018  
**arXiv:** https://arxiv.org/abs/1810.02904

**Key Findings:**
- Analyzed crash-consistency bugs in Linux filesystems (5 years of history)
- Found that most bugs triggered by small workloads (≤3 filesystem operations)
- Tool identified 24 out of 26 known bugs
- Discovered 10 NEW previously unknown bugs
- Bugs found in: ext4, XFS, btrfs, F2FS

**Quote from research:**
> "We found that most crash-consistency bugs can be demonstrated with small workloads, involving three or fewer file-system operations."

**Notable Bugs Found:**
- ext4: rename followed by fsync of parent directory doesn't persist rename
- btrfs: incorrect handling of ordered operations during crash
- F2FS: metadata inconsistencies after crash
- XFS: directory entry persistence issues

**Xibalba Relevance:**
This research validates Xibalba's approach! Small workloads with fault injection can find real bugs. CrashMonkey found 10 new bugs using similar methodology.

---

## Btrfs RAID5/6 Write Hole (Ongoing)

### Longstanding Warning: Btrfs RAID5/6 Corruption Risk
**Status:** Still present (as of 2024)  
**Official Warning:** "RAID5 and RAID6 are not recommended for production use"

**Description:**
Btrfs RAID5/6 implementations have a known "write hole" issue that can lead to data corruption after a crash during a stripe write.

**Key Details:**
- Write hole during parity updates
- Can corrupt data after system crash
- Power loss during stripe write leaves inconsistent state
- Scrub cannot detect/fix all corruption cases
- Official btrfs wiki warns against production use

**The Write Hole Problem:**
```
1. Start writing stripe (data + parity)
2. Some writes complete, others don't
3. System crashes
4. Parity doesn't match data
5. Corruption during reconstruction
```

**Xibalba Relevance:**
Classic crash consistency issue! Xibalba could test RAID5/6 with crash injection during stripe writes.

---

## Historical Context: ReiserFS Issues (1997-2025)

### ReiserFS Deprecation Due to Corruption Concerns
**Timeline:**
- Deprecated in Linux 5.18 (2022)
- Marked obsolete in Linux 6.6 (2023)
- Removed from mainline in Linux 6.13 (2025)

**Known Issues:**
1. **Non-synchronous rename():** Can cause corruption if system halts
2. **fsck can cause further corruption:** Rebuilding tree is destructive
3. **Year 2038 problem:** No fixes forthcoming due to lack of maintenance

**Xibalba Relevance:**
The rename() synchronization issue is exactly what Xibalba could test with pause injection!

---

## Debian/Ubuntu Specific Reports

### Debian Kernel ext4 Corruption (2018)
**Bug Report:** https://bugs.launchpad.net/bugs/1806755  
**Kernel:** 4.19 (before 4.19.8.9)  
**Date:** December 2018  

**Description:**
ext4 filesystem corruption under memory pressure in Debian kernel 4.19.

**Key Details:**
- Triggered by memory pressure
- Kernel 4.19 specific
- Fixed in 4.19.8.9 and later

---

### Ubuntu 24.04 LTS Filesystem Corruption (2024)
**Bug Report:** https://bugs.launchpad.net/bugs/2078283  
**Date:** August 2024  

**Description:**
Users reported filesystem corruption in Ubuntu 24.04 LTS leading to severe data loss.

**Symptoms:**
- File manager cannot create new folders
- Cannot transfer files
- Requires manual filesystem repairs
- Persisted despite regular updates

---

## Linux VFS Bug (5-Year Latency)

### VFS Non-Initial User Namespace Mount Bug (2019-2024)
**Introduced:** 2018  
**Present in mainline:** February 2019  
**Fixed:** 2024 (Linux 6.11)  
**Duration:** ~5 years  
**Source:** https://www.phoronix.com/news/Linux-6.11-VFS-Fix-5-Year-Bug

**Description:**
VFS bug allowed privileged users to mount filesystems with non-initial user namespace, leading to potential security issues, crashes, or on-disk corruption.

**Key Details:**
- Went undetected for 5 years
- Affected security boundaries
- Could cause system crashes
- Potential for on-disk corruption

**Xibalba Relevance:**
Namespace and permission edge cases are testable with Xibalba's framework.

---

## Summary: Categories of Bugs

### 1. Race Conditions & Timing Issues
- OCFS2 hole punching race (CVE-2024-40943) ✓ **High Xibalba relevance**
- OpenZFS block cloning corruption ✓ **High Xibalba relevance**
- F2FS garbage collection (CVE-2025-38164)

### 2. Crash Consistency Issues
- ext4 delayed allocation data loss ✓ **High Xibalba relevance**
- CrashMonkey bugs (10 new bugs found)
- ReiserFS rename() non-synchronous operations

### 3. Metadata Inconsistencies
- ext4 online resize (CVE-2024-35807)
- XFS metadata corruption (2020)
- F2FS SIT/SSA inconsistencies

### 4. Direct I/O and iomap Issues
- ext4 iomap corruption (2023) ✓ **High Xibalba relevance**

### 5. Edge Cases in Feature Interactions
- Btrfs scrub on converted filesystems (CVE-2024-26616)
- XFS quota + link() operations (CVE-2024-55641)

---

## Xibalba Detection Potential: High-Priority Targets

### Bugs Xibalba Could Likely Detect:

1. **OCFS2 Hole Punching Race** - Race between concurrent operations
2. **OpenZFS Block Cloning** - Timing-dependent data corruption
3. **ext4 Delayed Allocation** - Crash consistency during delayed writes
4. **ext4 iomap Direct I/O** - Ordering of position updates
5. **ReiserFS rename()** - Non-synchronous directory operations
6. **Btrfs RAID5/6 Write Hole** - Crash during multi-step operations

### Testing Strategies:

**For Race Conditions:**
- Inject pauses between `fallocate()` and concurrent writes
- Pause between metadata updates
- Test concurrent operations on same file

**For Crash Consistency:**
- Inject crashes after each syscall in workload
- Test small workloads (2-3 operations)
- Verify state after "recovery"

**For Metadata Issues:**
- Pause between related metadata updates
- Test operation sequences that span multiple transactions
- Verify filesystem consistency with fsck

---

## Quick Reference Table

| Bug Name | Filesystem | Year | Detection Latency | Bug Type | Xibalba Potential |
|----------|-----------|------|-------------------|----------|-------------------|
| OpenZFS Block Cloning | ZFS | 2023 | ~17 years | Race condition | ✓ HIGH |
| ext4 iomap Direct I/O | ext4 | 2023 | Months | Ordering/Timing | ✓ HIGH |
| F2FS Garbage Collection | F2FS | 2025 | Unknown | Metadata consistency | MEDIUM |
| ext4 Online Resize | ext4 | 2024 | Unknown | Metadata corruption | MEDIUM |
| Btrfs Scrub UAF | Btrfs | 2024 | Unknown | Memory/alignment | LOW |
| OCFS2 Hole Punching Race | OCFS2 | 2024 | Unknown | Race condition | ✓ HIGH |
| XFS Quota Lock | XFS | 2024 | Unknown | Transaction/locking | MEDIUM |
| XFS Metadata Corruption | XFS | 2020 | Unknown | Metadata ordering | MEDIUM |
| JFS Inode Eviction | JFS | 2025 | Unknown | State machine | LOW |
| ext4 Delayed Allocation | ext4 | 2008-09 | Immediate | Crash consistency | ✓ HIGH |
| Btrfs RAID5/6 Write Hole | Btrfs | Ongoing | Years | Crash consistency | ✓ HIGH |
| ReiserFS rename() | ReiserFS | Longstanding | Years | Atomicity | ✓ HIGH |
| VFS Namespace Bug | VFS | 2019-24 | 5 years | Security/corruption | LOW |

**Legend:**
- ✓ HIGH: Very likely Xibalba could detect this
- MEDIUM: Xibalba might detect this with right test configuration
- LOW: Xibalba unlikely to detect (memory corruption, specific edge case)

---

## Next Steps for Research

### Need More Details On (Requires Manual Lookup):
1. **OpenZFS Block Cloning Bug:**
   - GitHub issues: Likely #15526 or #15571
   - Need to manually browse: https://github.com/openzfs/zfs/issues
   - Search for "block cloning", "dmu_buf_will_clone", "corruption" in Nov-Dec 2023
   
2. **Kernel Git Commits:**
   - ext4 iomap fix: 936e114a245b6 ("iomap: update ki_pos a little later in iomap_dio_complete")
   - Need to find commits for CVE-2024-35807, CVE-2024-26616, etc.
   - Browse: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
   
3. **Btrfs RAID5/6 Information:**
   - Check https://btrfs.wiki.kernel.org/index.php/RAID56
   - Look for write hole documentation
   
4. **CrashMonkey Paper:**
   - Full citation needed from OSDI 2018 proceedings
   - arXiv paper: https://arxiv.org/abs/1810.02904
   - Authors: Jayashree Mohan, Ashlie Martinez, Soujanya Ponnapalli, Pandian Raju, Vijay Chidambaram
   - Check https://www.cs.utexas.edu/~vijay/ for full paper PDF

### Additional Resources to Check:
- **LKML Archives:** https://lkml.org (search by CVE number or filesystem name)
- **Kernel Git:** https://git.kernel.org
- **OpenZFS GitHub:** https://github.com/openzfs/zfs/issues
- **Btrfs Wiki:** https://btrfs.wiki.kernel.org
- **CVE Database:** https://cve.mitre.org
- **Linux Weekly News:** https://lwn.net (search for filesystem corruption articles)
- **Phoronix:** https://www.phoronix.com (filesystem bug reports)

### Tools for Finding Bugs:
- **Syzkaller:** https://github.com/google/syzkaller - Kernel fuzzer that found many of these bugs
- **CrashMonkey:** https://github.com/utsaslab/crashmonkey - Crash consistency testing
- **xfstests:** Standard filesystem test suite
- **Trinity:** System call fuzzer

---

## References

### LKML Discussions:
- ext4 iomap corruption: https://lkml.org/lkml/2023/12/5/646
- XFS metadata corruption: https://lkml.org/lkml/2020/3/31/1555
- JFS inode eviction: https://lkml.org/lkml/2025/8/8/92

### CVE Databases:
- CVE-2025-38164 (F2FS): https://www.wiz.io/vulnerability-database/cve/cve-2025-38164
- CVE-2024-35807 (ext4): https://ubuntu.com/security/CVE-2024-35807
- CVE-2024-26616 (Btrfs): https://radar.offseq.com/threat/cve-2024-26616
- CVE-2024-40943 (OCFS2): https://www.rapid7.com/db/vulnerabilities/oracle_linux-cve-2024-40943/
- CVE-2024-55641 (XFS): https://ubuntu.com/security/CVE-2024-55641

### Academic Research:
- CrashMonkey: https://arxiv.org/abs/1810.02904

### News/Articles:
- OpenZFS corruption: https://www.theregister.com/2023/12/04/two_new_versions_of_openzfs
- Linux 6.11 VFS fix: https://www.phoronix.com/news/Linux-6.11-VFS-Fix-5-Year-Bug

### Bug Trackers:
- Debian #1055005 (ext4 iomap): https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=1055005
- Ubuntu #1806755 (ext4 4.19): https://bugs.launchpad.net/bugs/1806755
- Ubuntu #2078283 (24.04 corruption): https://bugs.launchpad.net/bugs/2078283
- Ubuntu #1796542 (silent corruption): https://bugs.launchpad.net/bugs/1796542
- Kernel Bugzilla #201685 (block layer): https://bugzilla.kernel.org/show_bug.cgi?id=201685
- Arch Linux Forums (ext4 3.6): https://bbs.archlinux.org/viewtopic.php?id=151341

### LWN.net Articles (Highly Recommended Reading):
- ext4 data corruption in stable kernels: https://lwn.net/Articles/954770/
- Another ext4 stable discussion: https://lwn.net/Articles/954285/
- Block layer corruption bug (2018): https://lwn.net/Articles/774440/
- RAID 0 corruption (2015): https://lwn.net/Articles/645720/
- Trust in and maintenance of filesystems (2023): https://lwn.net/Articles/951846/

### Community Discussions:
- Hacker News - ext4 corruption: https://news.ycombinator.com/item?id=38589389
- Phoronix Forums - ext4 bug: https://www.phoronix.com/forums/forum/software/general-linux-open-source/32852-ext4-data-corruption-bug-hits-stable-linux-kernels

### Wikipedia:
- ext4 delayed allocation: https://en.wikipedia.org/wiki/Ext4#Delayed_allocation_and_potential_data_loss
- ReiserFS: https://en.wikipedia.org/wiki/ReiserFS
- Bcachefs: https://en.wikipedia.org/wiki/Bcachefs
- ZFS: https://en.wikipedia.org/wiki/ZFS

### Filesystem-Specific Resources:
- Btrfs Wiki: https://btrfs.wiki.kernel.org
- Btrfs RAID56: https://btrfs.wiki.kernel.org/index.php/RAID56
- OpenZFS GitHub: https://github.com/openzfs/zfs

### Research & Tools:
- CrashMonkey GitHub: https://github.com/utsaslab/crashmonkey
- Syzkaller: https://github.com/google/syzkaller
- xfstests: https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git
- Vijay Chidambaram's page: https://www.cs.utexas.edu/~vijay/

---

*Last Updated: October 12, 2025*
*Compiled for Xibalba Filesystem Testing Framework*

**Navigation:**
- For test implementation details, see: [`PRIORITY-BUGS-FOR-TESTING.md`](PRIORITY-BUGS-FOR-TESTING.md)
- For gap analysis & roadmap, see: [`XIBALBA-CURRENT-CAPABILITIES-VS-BUGS.md`](XIBALBA-CURRENT-CAPABILITIES-VS-BUGS.md)
- For Xibalba architecture, see: [`../design/TESTING-FRAMEWORK.md`](../design/TESTING-FRAMEWORK.md)
- For eBPF implementation, see: [`../design/EBPF-FAULT-INJECTION-FOR-POSIX-BUGS.md`](../design/EBPF-FAULT-INJECTION-FOR-POSIX-BUGS.md)

