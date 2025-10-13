# Up-and-Coming Filesystems Not Yet Tested by Xibalba

## Current Testing Coverage

**Currently Tested:**
- ✅ **ext4** - Default Linux filesystem
- ✅ **XFS** - High-performance filesystem
- ✅ **btrfs** - Copy-on-write filesystem
- ⚠️ **ZFS** - Partial testing (some limitations noted)

**Not Tested:** Everything else!

---

## Priority 1: Newly Mainlined Filesystems

### 1. **bcachefs** 🔥 (Highest Priority!)

**Status:** Merged into Linux 6.7 (January 2024)  
**Maturity:** NEW - First stable release, under heavy development  
**Complexity:** Very high  

**Why Test:**
- **Brand new!** Most likely to have bugs
- Complex copy-on-write implementation
- Native RAID support (potential for write hole bugs)
- Compression, checksumming (complex interactions)
- Competes with ZFS and btrfs (bold claims need validation)
- Heavy development means bugs are being found and fixed rapidly

**Features to Stress:**
- Copy-on-write operations under concurrent load
- RAID configurations (especially RAID5/6)
- Compression + concurrent writes
- Checksumming validation
- Snapshot operations
- Online fsck

**Xibalba Advantages:**
- ✅ Race conditions in new code are common
- ✅ COW operations are multi-step (pause injection perfect)
- ✅ RAID operations have timing windows
- ✅ Compression + concurrency = bugs waiting to happen

**Known Issues:**
- Development disputes (ejected then re-added to kernel in 2024/2025)
- Marked as "Externally maintained" as of August 2025
- Still stabilizing - perfect time to find bugs!

**Testing Priority:** ⭐⭐⭐⭐⭐ (HIGHEST)  
**Expected Bug Yield:** Very High (new + complex)

**Links:**
- Wikipedia: https://en.wikipedia.org/wiki/Bcachefs
- Linux kernel: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/fs
- Controversies: https://lwn.net/Articles/934623/

---

### 2. **F2FS** (Flash-Friendly File System) 🔥

**Status:** Mainlined in Linux 3.8 (2013), actively developed  
**Maturity:** Mature but evolving  
**Complexity:** Medium-High  

**Why Test:**
- **Flash-optimized** - Different assumptions than traditional FS
- Used in Android and embedded systems (high impact)
- Complex garbage collection (multi-step, race-prone)
- **We already cataloged a bug:** CVE-2025-38164 (GC corruption)
- Log-structured design (different concurrency patterns)

**Features to Stress:**
- **Garbage collection** - Complex multi-step operation
- Segment cleaning under concurrent load
- Wear leveling interactions
- Atomic writes
- Concurrent trim operations
- SIT/SSA consistency (known bug area!)

**Xibalba Advantages:**
- ✅ GC is multi-step (pause injection exposes races)
- ✅ Already found bugs here (CVE-2025-38164)
- ✅ Flash-specific operations are less tested
- ✅ Segment management is complex state machine

**Known Issues (from our research):**
- CVE-2025-38164: SIT/SSA inconsistency during GC
- Garbage collection timing bugs
- Metadata consistency issues

**Testing Priority:** ⭐⭐⭐⭐ (HIGH)  
**Expected Bug Yield:** High (complex GC, known issues)

**Use Cases:**
- Android devices
- Embedded systems
- SSD-optimized deployments
- Flash storage

**Links:**
- Kernel docs: https://www.kernel.org/doc/html/latest/filesystems/f2fs.html
- CVE-2025-38164: https://www.wiz.io/vulnerability-database/cve/cve-2025-38164

---

## Priority 2: Production Filesystems (Enterprise Focus)

### 3. **OCFS2** (Oracle Cluster File System 2)

**Status:** Mainlined in Linux 2.6.16 (2006)  
**Maturity:** Mature but niche  
**Complexity:** Very High (cluster/distributed)  

**Why Test:**
- **We already cataloged a bug:** CVE-2024-40943 (hole punching race)
- Cluster filesystem (complex distributed consistency)
- Oracle databases use it (high-value target)
- Less tested than ext4/XFS
- Concurrent access from multiple nodes

**Features to Stress:**
- **Hole punching + concurrent I/O** (known bug: CVE-2024-40943)
- Distributed lock management (DLM)
- Cluster-wide consistency
- Node failure scenarios
- Concurrent writes from multiple "nodes" (threads)
- Directory operations across nodes

**Xibalba Advantages:**
- ✅ Already documented race condition (CVE-2024-40943)
- ✅ Distributed FS = more race conditions
- ✅ Perfect test case for validating Xibalba
- ✅ Could expose more races in DLM

**Known Issues:**
- CVE-2024-40943: Race between hole punching and AIO+DIO
- Less mainstream = less testing

**Testing Priority:** ⭐⭐⭐⭐ (HIGH)  
**Expected Bug Yield:** Medium-High (distributed complexity)

**Validation Opportunity:** Test the CVE-2024-40943 fix!

**Links:**
- CVE-2024-40943: https://www.rapid7.com/db/vulnerabilities/oracle_linux-cve-2024-40943/

---

### 4. **GFS2** (Global File System 2)

**Status:** Mainlined in Linux 2.6.19 (2006)  
**Maturity:** Mature  
**Complexity:** Very High (cluster/distributed)  

**Why Test:**
- Red Hat's cluster filesystem
- Distributed lock manager (complex)
- Multi-node concurrent access
- Enterprise deployments (high impact)
- Similar to OCFS2 but different implementation

**Features to Stress:**
- Distributed locking under load
- Multi-node directory operations
- Cluster-wide cache coherency
- Node failure/recovery
- Lock contention

**Xibalba Advantages:**
- ✅ Cluster FS assumptions are harder to validate
- ✅ More nodes = more races
- ✅ DLM is complex state machine

**Testing Priority:** ⭐⭐⭐ (MEDIUM-HIGH)  
**Expected Bug Yield:** Medium

---

## Priority 3: Specialized/Emerging Filesystems

### 5. **ngnfs** 🆕 (Next-Generation Network File System)

**Status:** Announced at 2025 LSFMMBPF Summit  
**Maturity:** VERY NEW - Probably not in mainline yet  
**Complexity:** Very High (distributed, archival)  

**Why Test:**
- **Brand new!** (2025)
- Distributed filesystem for archival
- Large offline datasets
- Novel design
- Likely has undiscovered bugs

**Features to Stress:**
- Archival data retrieval
- Distributed operations
- Large file handling
- Offline/online transitions
- Network partition handling

**Xibalba Advantages:**
- ✅ New = bugs
- ✅ Distributed = race conditions
- ✅ First to test = high impact

**Testing Priority:** ⭐⭐⭐⭐ (HIGH if available)  
**Expected Bug Yield:** Very High (new + distributed)

**Status:** Wait for mainline kernel inclusion

**Links:**
- LWN: https://lwn.net/Articles/lsfmmbpf2025/

---

### 6. **famfs** 🆕 (CXL/Disaggregated Memory Filesystem)

**Status:** Announced at 2025 LSFMMBPF Summit, FUSE implementation  
**Maturity:** VERY NEW  
**Complexity:** High (new hardware model)  

**Why Test:**
- **Cutting edge!** CXL memory
- FUSE implementation (userspace bugs different from kernel)
- Disaggregated memory model (new assumptions)
- Future hardware paradigm

**Features to Stress:**
- CXL memory access patterns
- FUSE vs kernel consistency
- Disaggregated memory coherency
- Concurrent access to remote memory

**Xibalba Advantages:**
- ✅ New hardware model = untested assumptions
- ✅ FUSE = different bug patterns
- ✅ Could pioneer testing methodology

**Testing Priority:** ⭐⭐⭐ (MEDIUM - cutting edge)  
**Expected Bug Yield:** High (very new)

**Challenges:**
- Need CXL hardware or emulation
- FUSE testing might need different approach

**Links:**
- LWN: https://lwn.net/Articles/lsfmmbpf2025/

---

### 7. **NILFS2** (New Implementation of Log-structured File System)

**Status:** Mainlined in Linux 2.6.30 (2009)  
**Maturity:** Mature but niche  
**Complexity:** High (log-structured)  

**Why Test:**
- Log-structured design (different from traditional)
- Continuous snapshotting
- Garbage collection (like F2FS)
- Less tested than mainstream FS

**Features to Stress:**
- Continuous snapshotting under load
- Garbage collection + concurrent writes
- Checkpoint operations
- Log segment management

**Xibalba Advantages:**
- ✅ GC is multi-step
- ✅ Log-structured = different timing
- ✅ Less mainstream = fewer tests

**Testing Priority:** ⭐⭐ (MEDIUM)  
**Expected Bug Yield:** Medium

---

### 8. **EROFS** (Enhanced Read-Only File System)

**Status:** Mainlined in Linux 4.19 (2018)  
**Maturity:** Mature, active development  
**Complexity:** Low-Medium (read-only)  

**Why Test:**
- Used in Android
- Read-only (simpler but still has races)
- Compression support
- High performance focus

**Features to Stress:**
- Concurrent reads (main use case)
- Compression decompression races
- Mount/unmount under load
- Cache coherency

**Xibalba Advantages:**
- ⚠️ Read-only limits race potential
- ✅ Android use = high impact
- ✅ Compression = complexity

**Testing Priority:** ⭐⭐ (LOW-MEDIUM)  
**Expected Bug Yield:** Low (read-only)

**Use Cases:**
- Android system partitions
- Container images
- Read-only distributions

---

### 9. **SquashFS**

**Status:** Mainlined in Linux 2.6.29 (2009)  
**Maturity:** Very mature  
**Complexity:** Low (read-only, compressed)  

**Why Test:**
- Widely used (Live CDs, containers)
- Read-only compressed
- High decompression concurrency

**Features to Stress:**
- Concurrent decompression
- Many simultaneous readers
- Different compression algorithms
- Mount racing

**Xibalba Advantages:**
- ✅ Used everywhere (high impact)
- ⚠️ Read-only limits bugs
- ✅ Compression = some complexity

**Testing Priority:** ⭐ (LOW)  
**Expected Bug Yield:** Low

---

## Priority 4: Network Filesystems

### 10. **NFS** (Network File System)

**Status:** Ancient, constantly evolving  
**Maturity:** Very mature  
**Complexity:** Very High (network + distributed)  

**Why Test:**
- Ubiquitous in enterprise
- Network timing issues
- Cache coherency nightmares
- Client/server races
- Multiple protocol versions (v3, v4, v4.1, v4.2)

**Features to Stress:**
- Network partition simulation
- Concurrent access from multiple clients
- Cache coherency under load
- Lock manager (NLM) races
- Close-to-open consistency

**Xibalba Advantages:**
- ✅ Network = more timing windows
- ✅ Distributed = complex races
- ✅ Cache coherency is subtle

**Testing Priority:** ⭐⭐⭐ (MEDIUM-HIGH)  
**Expected Bug Yield:** Medium

**Challenges:**
- Need client/server setup
- Network simulation

---

### 11. **SMB/CIFS** (Windows File Sharing)

**Status:** Mature, CIFS module in kernel  
**Maturity:** Mature  
**Complexity:** Very High (protocol complexity)  

**Why Test:**
- Windows interoperability (huge use case)
- Complex protocol
- Opportunistic locking (OpLocks)
- Many protocol versions

**Features to Stress:**
- OpLock breaking
- Concurrent access
- Directory change notification
- Protocol negotiation races

**Testing Priority:** ⭐⭐⭐ (MEDIUM)  
**Expected Bug Yield:** Medium

---

### 12. **Ceph FS**

**Status:** Mature distributed filesystem  
**Maturity:** Mature  
**Complexity:** VERY HIGH (distributed, object-based)  

**Why Test:**
- Modern distributed storage
- Object-based architecture
- Strong consistency guarantees
- Cloud-native

**Features to Stress:**
- Distributed consistency
- OSD failure scenarios
- Concurrent multi-client access
- Metadata server (MDS) races
- CRUSH algorithm behavior

**Xibalba Advantages:**
- ✅ Distributed = maximum complexity
- ✅ Many components = many races
- ✅ High-value target

**Testing Priority:** ⭐⭐⭐⭐ (HIGH)  
**Expected Bug Yield:** High (very complex)

**Challenges:**
- Need full Ceph cluster
- Complex setup

---

## Priority 5: Experimental/Research

### 13. **Stratis**

**Status:** Red Hat's storage management (not a filesystem itself)  
**Maturity:** Production-ready  
**Complexity:** High (volume management + XFS)  

**Why Test:**
- Red Hat backing
- Combines volume management + filesystem
- Snapshots, thin provisioning
- Enterprise target

**Features to Stress:**
- Snapshot operations
- Thin provisioning edge cases
- Volume resize under load
- XFS interactions

**Testing Priority:** ⭐⭐ (LOW-MEDIUM)  
**Expected Bug Yield:** Medium

**Note:** Tests the management layer more than filesystem itself

---

### 14. **FUSE Filesystems** (General Category)

**Examples:**
- sshfs
- s3fs
- glusterfs
- bindfs

**Why Test:**
- Userspace = different bug patterns
- FUSE layer itself has races
- Each implementation is different

**Xibalba Advantages:**
- ✅ Userspace = less tested for races
- ✅ FUSE layer adds complexity
- ✅ Many implementations to test

**Testing Priority:** ⭐⭐⭐ (MEDIUM)  
**Expected Bug Yield:** Medium-High

---

## Testing Roadmap Recommendation

### Phase 1: Maximum Impact (Immediate)

1. **bcachefs** ⭐⭐⭐⭐⭐
   - NEW, complex, high bug potential
   - COW + RAID = perfect for Xibalba
   - First-to-market advantage

2. **F2FS** ⭐⭐⭐⭐
   - We already found bugs here
   - Android impact
   - Validate CVE-2025-38164

3. **OCFS2** ⭐⭐⭐⭐
   - Validate CVE-2024-40943 fix
   - Prove Xibalba works on known bug

### Phase 2: Enterprise Coverage (Next)

4. **Ceph FS** ⭐⭐⭐⭐
   - Distributed, complex
   - Cloud-native use case

5. **NFS** ⭐⭐⭐
   - Ubiquitous
   - Network races

6. **GFS2** ⭐⭐⭐
   - Red Hat ecosystem
   - Cluster complexity

### Phase 3: Emerging Tech (Future)

7. **ngnfs** ⭐⭐⭐⭐ (when available)
   - Brand new
   - First to test

8. **famfs** ⭐⭐⭐ (when mature)
   - CXL future
   - Novel hardware model

### Phase 4: Specialized (Lower Priority)

9. **NILFS2** ⭐⭐
10. **Stratis** ⭐⭐
11. **EROFS** ⭐⭐
12. **SquashFS** ⭐

---

## Implementation Considerations

### Easy to Add (Similar to Current):
- ✅ **bcachefs** - Standard block device, mkfs available
- ✅ **F2FS** - Standard block device, mkfs available
- ✅ **NILFS2** - Standard block device, mkfs available
- ✅ **EROFS** - Standard block device (read-only)

### Moderate Complexity:
- ⚠️ **OCFS2** - Needs DLM setup (can simulate with single node)
- ⚠️ **GFS2** - Needs DLM setup
- ⚠️ **Stratis** - Needs Stratis daemon

### Complex Setup:
- ❌ **NFS** - Need client/server, network
- ❌ **Ceph FS** - Need full Ceph cluster (OSDs, MON, MDS)
- ❌ **famfs** - Need CXL hardware or emulation
- ❌ **ngnfs** - Probably distributed setup

### Not Yet Available:
- ❌ **ngnfs** - Wait for kernel merge
- ❌ **famfs** - Still experimental

---

## Comparison: Why These > Current Testing

### Currently Tested:
| Filesystem | Maturity | Complexity | Bug Potential |
|------------|----------|------------|---------------|
| ext4 | Very High | Low | Low (well-tested) |
| XFS | Very High | Medium | Low-Medium |
| btrfs | High | High | Medium |

### High-Priority Untested:
| Filesystem | Maturity | Complexity | Bug Potential |
|------------|----------|------------|---------------|
| **bcachefs** | **LOW** | **Very High** | **VERY HIGH** 🔥 |
| **F2FS** | Medium | High | **HIGH** 🔥 |
| **OCFS2** | High | Very High | **HIGH** 🔥 |
| **Ceph FS** | High | EXTREME | **HIGH** 🔥 |

**Insight:** The untested filesystems are MORE COMPLEX and LESS MATURE = more bugs to find!

---

## Expected Bug Yield Analysis

### Factors That Increase Bug Likelihood:

1. **Newness** (bcachefs, ngnfs, famfs)
   - Less testing time
   - Evolving code
   - Immature implementations

2. **Complexity** (OCFS2, GFS2, Ceph FS)
   - Distributed systems
   - More state machines
   - More race windows

3. **Unique Designs** (F2FS, NILFS2)
   - Different assumptions
   - Flash-specific
   - Log-structured complexity

4. **Less Mainstream** (OCFS2, NILFS2)
   - Fewer users = less testing
   - Niche use cases
   - Less scrutiny

### Bug Yield Prediction:

| Filesystem | Expected Bugs | Confidence |
|------------|---------------|------------|
| bcachefs | VERY HIGH | High |
| F2FS | HIGH | High |
| OCFS2 | HIGH | Very High (already found one!) |
| Ceph FS | HIGH | Medium |
| ngnfs | VERY HIGH | High (if new) |
| famfs | HIGH | Medium |
| NFS | MEDIUM | Medium |
| GFS2 | MEDIUM | Medium |
| Others | LOW-MEDIUM | Low |

---

## Validation Opportunities

### Immediate Validation (Prove Xibalba Works):

1. **OCFS2 + CVE-2024-40943**
   - We documented this bug
   - Test if Xibalba can find it
   - Validate on kernel BEFORE fix
   - Validate fix works

2. **F2FS + CVE-2025-38164**
   - Same approach
   - GC corruption bug
   - Test before/after fix

**Impact:** Proves Xibalba can find real bugs = instant credibility!

---

## Summary: Top 3 Recommendations

### 🥇 1. bcachefs (DO THIS FIRST!)
- **Why:** New, complex, high bug potential
- **When:** NOW (in Linux 6.7+)
- **Effort:** Low (standard setup)
- **Payoff:** Very High

### 🥈 2. F2FS (DO THIS SECOND!)
- **Why:** Known bugs, Android impact
- **When:** NOW
- **Effort:** Low (standard setup)
- **Payoff:** High (validation opportunity)

### 🥉 3. OCFS2 (DO THIS THIRD!)
- **Why:** Validate CVE-2024-40943
- **When:** NOW
- **Effort:** Low-Medium (single-node DLM)
- **Payoff:** Very High (proves Xibalba works!)

---

## Quick-Start Implementation

### Add bcachefs (10 minutes):

```bash
# 1. Check kernel version (need 6.7+)
uname -r

# 2. Install tools
sudo apt install bcachefs-tools

# 3. Add to Xibalba test script
mkfs.bcachefs -f /dev/vdb
mount -t bcachefs /dev/vdb /mnt/test

# 4. Run Xibalba chaos test
bazel run //chaos:simple_chaos_test -- /mnt/test
```

### Add F2FS (5 minutes):

```bash
# 1. Install tools
sudo apt install f2fs-tools

# 2. Add to test script
mkfs.f2fs -f /dev/vdb
mount -t f2fs /dev/vdb /mnt/test

# 3. Run Xibalba
bazel run //chaos:simple_chaos_test -- /mnt/test
```

---

*Document created: October 12, 2025*  
*For Xibalba Filesystem Testing Framework*

