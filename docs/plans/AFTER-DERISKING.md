# Xibalba: After Tech De-Risking - What's Next?

*The roadmap from PoC to production-ready framework*
*Assumes: Tech de-risking succeeded - eBPF pause injection works!*

---

## What Tech De-Risking Gave You

After 2-3 weeks of de-risking, you have:

✅ **Proven technology**:
- eBPF programs load and attach
- Pause injection works (eBPF → userspace → SIGSTOP)
- Pauses increase race detection
- Basic chaos testing framework

✅ **Working code** (~500 lines):
- Minimal DirectoryReader
- Simple concurrent test
- eBPF pause injector
- Userspace controller

✅ **Validation**:
- Found at least one race condition (even if intentional)
- Demonstrated pauses expand race windows
- Proved core approach is viable

---

## The Gap: PoC → Production Framework

**What you have**: Proof that eBPF fault injection can find races

**What you need**: A framework that systematically tests kernel VFS changes

**The gap**:

| Component | PoC Status | Production Needed |
|-----------|------------|-------------------|
| **DirectoryReader** | Classic only | + io_uring support |
| **Chaos tests** | Simple concurrent reads | + Writers, + invariant checking |
| **eBPF** | Basic pauses | + Fault override, + adaptive modes |
| **Filesystems** | Local ext4/tmpfs | + VMs with 5 filesystems |
| **Kernel testing** | N/A | + Custom kernel builds, + deployment |
| **Automation** | Manual execution | + Orchestration, + reporting |
| **Detection** | Manual observation | + Automated race detection |

---

## Post-De-Risking Roadmap

### Phase A: Enhanced Fault Injection (2-3 weeks)

**Goal**: Beyond pauses - inject actual faults (EAGAIN, ENOMEM, EIO)

**What to build**:

1. **bpf_override_return() implementation**:
   ```c
   // Override kmalloc to return NULL
   SEC("kprobe/__kmalloc")
   int BPF_KPROBE(fault_kmalloc, size_t size) {
       if (should_inject_fault()) {
           bpf_override_return(regs, 0);  // Return NULL
       }
       return 0;
   }
   ```

2. **Multiple fault types**:
   - Memory allocation failures (-ENOMEM)
   - I/O errors (-EIO)
   - Lock contention (-EAGAIN)
   - Configurable probabilities

3. **Adaptive injection**:
   - Start at 5% fault rate
   - Increase if no bugs found
   - Learn which faults are effective

**Deliverable**: eBPF injector that can inject multiple fault types, not just pauses

**Validation**: Successfully inject -ENOMEM into test program, verify error handling works

---

### Phase B: Race Detection & Invariant Checking (2 weeks)

**Goal**: Automatically detect race conditions, not just observe them

**What to build**:

1. **Duplicate detector**:
   ```c
   // Hash set to track entries seen
   // Detect if same entry appears twice
   // Report violation with context
   ```

2. **Missing entry detector**:
   ```c
   // Compare against baseline (snapshot)
   // Detect if entries disappear
   ```

3. **Invariant checker framework**:
   ```c
   struct invariant_check {
       char *name;
       bool (*check)(test_results);
       char *violation_msg;
   };
   
   // Register checkers
   // Run after each test
   // Report violations clearly
   ```

4. **History recording**:
   ```c
   // Log all operations with timestamps
   // Enable post-mortem analysis
   // Support replay
   ```

**Deliverable**: Automated race detection - test fails if races detected

**Validation**: Inject intentional f_pos corruption bug, verify detector catches it

---

### Phase C: Real Kernel Testing (2-3 weeks)

**Goal**: Test actual kernel VFS code, not just test framework

**What to build**:

1. **Kernel build automation**:
   ```bash
   # Script to build kernel with your patches
   ./build_test_kernel.sh
   
   # Produces: linux-image-*.deb
   ```

2. **Single VM setup** (start with one filesystem):
   ```bash
   # Create ext4 test VM
   ./vms/create_test_vm.sh ext4
   
   # Install custom kernel
   ./vms/deploy_kernel.sh linux-image-*.deb
   
   # Deploy Xibalba
   ./vms/deploy_xibalba.sh
   ```

3. **Remote test execution**:
   ```bash
   # Run Xibalba tests in VM
   ./vms/run_tests.sh ext4
   
   # Collect results
   # Analyze
   ```

4. **VFS-level eBPF hooks**:
   ```c
   // Hook vfs_getdents_async (if it exists in your kernel)
   SEC("fentry/vfs_getdents_async")
   int trace_vfs_getdents(void *ctx) {
       // Inject pauses/faults at VFS layer
   }
   ```

**Deliverable**: Can test custom kernel in VM with Xibalba

**Validation**: Run Xibalba against kernel with your async getdents patches

---

### Phase D: Multi-Filesystem Testing (3-4 weeks)

**Goal**: Validate across all major filesystems

**What to build**:

1. **VM templates for each filesystem**:
   - ext4 (htree directories)
   - XFS (B+ tree directories)
   - tmpfs (in-memory)
   - btrfs (if needed)
   - ZFS (if needed - defer if time-constrained)

2. **Filesystem-specific tests**:
   ```bash
   # ext4 htree tests
   tests/ext4_htree_stress.sh
   
   # XFS B-tree tests
   tests/xfs_btree_stress.sh
   ```

3. **Parallel execution**:
   ```bash
   # Run tests on all VMs concurrently (overnight)
   ./vms/run_all_parallel.sh
   ```

**Deliverable**: 3-5 filesystem VMs, can test all simultaneously

**Validation**: Same test passes on all filesystems

---

### Phase E: Automation & Reporting (2 weeks)

**Goal**: One-command test execution and clear results

**What to build**:

1. **Orchestration**:
   ```bash
   ./test_all.sh
   # Does everything: build, deploy, test, report
   ```

2. **HTML reports**:
   - Test results per filesystem
   - Performance comparisons
   - Race conditions found
   - Timeline graphs

3. **CI integration** (optional):
   - GitHub Actions workflow
   - Automated kernel testing
   - Nightly runs

**Deliverable**: `./test_all.sh` runs everything, produces HTML report

---

## Decision Point After De-Risking

**Option 1: Focused Kernel Development** (Recommended if you're developing async getdents)

```
Path: A → C → B → D → E
      ↓    ↓    ↓    ↓    ↓
      Enhanced  Real  Race  Multi- Auto-
      faults    kernel detect FS    mation

Timeline: 9-12 weeks
Focus: Get kernel patches tested ASAP
Defer: Multi-filesystem, fancy reporting
```

**Rationale**: If you're developing kernel patches, you want to test them early. Skip multi-filesystem initially.

**Progression**:
1. **Week 1-3**: Enhanced fault injection (Phase A)
2. **Week 4-6**: Single VM + kernel testing (Phase C) ← Test your patches!
3. **Week 7-9**: Race detection (Phase B)
4. **Week 10-12**: Add 2-3 more filesystems (Phase D minimal)
5. **Later**: Full automation (Phase E)

---

**Option 2: Comprehensive Framework First**

```
Path: B → A → D → C → E
      ↓    ↓    ↓    ↓    ↓
      Race  Enhanced Multi- Real  Auto-
      detect faults  FS    kernel mation

Timeline: 12-16 weeks
Focus: Build complete framework
Defer: Nothing - do it all
```

**Rationale**: If testing existing kernels, build complete framework first.

**Progression**:
1. **Week 1-2**: Race detection (Phase B)
2. **Week 3-5**: Enhanced fault injection (Phase A)
3. **Week 6-9**: Multi-filesystem VMs (Phase D)
4. **Week 10-12**: Kernel integration (Phase C)
5. **Week 13-16**: Automation (Phase E)

---

**Option 3: Incremental (Balanced)**

```
Path: A₁ → B₁ → C₁ → A₂ → B₂ → D → E
      ↓     ↓     ↓     ↓     ↓     ↓    ↓
      Basic Basic Single Advanced Complete Multi Full
      faults race  VM    faults    race     FS   auto

Timeline: 14-18 weeks
Focus: Build iteratively, validate each step
```

**Rationale**: Lower risk, more iterations, learn as you go

---

## Immediate Next Steps (Week 4)

**After tech de-risking succeeds**, here's what to do in week 4:

### Week 4, Days 1-2: Commit & Document

- [ ] Commit tech de-risking code
- [ ] Document results in TECH-DERISKING-RESULTS.md
- [ ] Decide: Option 1, 2, or 3 above?
- [ ] Create detailed plan for chosen option

### Week 4, Days 3-5: Quick Wins

**Build race detector** (proves value immediately):

```c
// chaos/race_detector.c
// Hash all entries seen
// Detect duplicates
// Report violations clearly
```

**Run against test with injected bug**:
```c
// Intentionally corrupt shared state
// Prove detector catches it
// Validate detection works
```

**Deliverable**: `./race_detector /tmp/dir` → reports "Found 5 duplicate entries"

---

## Resource Requirements by Phase

| Phase | RAM Needed | Disk Needed | Time |
|-------|------------|-------------|------|
| **A: Enhanced faults** | Host only (8GB) | 50GB | 2-3 weeks |
| **B: Race detection** | Host only (8GB) | 50GB | 2 weeks |
| **C: Single VM** | 16GB (host + 1 VM) | 150GB | 2-3 weeks |
| **D: Multi-FS (3)** | 24GB (host + 3 VMs) | 350GB | 3-4 weeks |
| **D: Multi-FS (5)** | 32GB (host + 5 VMs) | 550GB | 4-5 weeks |
| **E: Automation** | Same as D | Same | 2 weeks |

**Your resources**: 32GB RAM ✅, 500GB disk ✅ → Can do everything!

**Strategy for shared machine**:
- Phases A-B: Use anytime (low resources)
- Phase C: Single VM, can run during day
- Phase D-E: Multiple VMs, run overnight

---

## Recommended Path for You

**Given**:
- You have hardware resources (32GB, 500GB) ✅
- Shared dev machine (use overnight for VMs) ✅
- Want to test kernel VFS changes

**I recommend: Option 1 (Focused Kernel Development)**

### Next 3 Months:

**Month 1** (Weeks 4-7):
- Week 4: Race detector + commit de-risking results
- Week 5-6: Enhanced fault injection (ENOMEM, EIO, EAGAIN)
- Week 7: Single VM setup (ext4)

**Month 2** (Weeks 8-11):
- Week 8-9: Deploy your kernel to VM, test with Xibalba
- Week 10: Add VFS-level eBPF hooks
- Week 11: Iterate on kernel patches based on findings

**Month 3** (Weeks 12-15):
- Week 12-13: Add XFS and tmpfs VMs
- Week 14: Parallel testing across 3 filesystems
- Week 15: Automation and reporting

**Result**: Testing framework for your kernel development workflow

---

## Key Milestones

**Milestone 1** (End of Month 1): 
- ✅ Can inject multiple fault types via eBPF
- ✅ Can detect races automatically
- ✅ Have 1 VM with custom kernel
- **Value**: Can test your kernel patches in isolation

**Milestone 2** (End of Month 2):
- ✅ VFS-level fault injection working
- ✅ Tested your async getdents patches
- ✅ Found and fixed at least one bug
- **Value**: Confidence in your kernel patches

**Milestone 3** (End of Month 3):
- ✅ Testing across 3 filesystems
- ✅ Automated test runs
- ✅ Clear reports
- **Value**: Ready to submit patches upstream

---

## What Each Phase Unlocks

### Phase A: Enhanced Fault Injection
**Unlocks**: Testing error handling paths
- Can your code handle -ENOMEM gracefully?
- Does it retry on -EAGAIN properly?
- What happens on I/O errors?

### Phase B: Race Detection
**Unlocks**: Automated bug finding
- No more manual inspection
- Clear "PASS/FAIL" results
- Reproducible bug reports

### Phase C: VM + Kernel Testing
**Unlocks**: Testing real patches
- Deploy your kernel changes
- Test in isolation
- Iterate quickly

### Phase D: Multi-Filesystem
**Unlocks**: Comprehensive validation
- Ensure works on ext4, XFS, tmpfs
- Find filesystem-specific issues
- Confidence for upstream submission

### Phase E: Automation
**Unlocks**: Continuous testing
- Test every patch iteration
- Overnight regression testing
- Professional reports

---

## Concrete Next Steps (Assuming De-Risking Succeeds)

### Week 4: Immediate Next Actions

**Day 1**: Commit de-risking results
```bash
git add -f test_*.sh  # Force add if needed
git commit -m "feat: Tech de-risking complete - eBPF works!"
git push
```

**Day 2**: Build race detector
```c
// File: chaos/race_detector.c
// ~200 lines
// Detects duplicate entries
// Reports violations clearly
```

**Day 3-5**: Test race detector
```c
// Inject intentional f_pos corruption
// Run chaos test with race detector
// Verify detector catches corruption
// Document: "Found X races in Y iterations"
```

**Deliverable**: Automated race detection working

---

### Week 5-6: Enhanced eBPF Fault Injection

**Goals**:
1. Use `bpf_override_return()` to inject faults (not just pauses)
2. Add multiple fault types
3. Make fault injection configurable

**New eBPF programs**:

```c
// fault_injector.bpf.c (new file)

// Inject -ENOMEM on kmalloc
SEC("kprobe/__kmalloc")
int BPF_KPROBE(fault_kmalloc, size_t size) {
    if (should_inject(5)) {  // 5% probability
        bpf_override_return(regs, 0);  // NULL
    }
    return 0;
}

// Inject -EAGAIN on lock acquisition
SEC("fentry/down_read_trylock")
int BPF_PROG(fault_trylock) {
    if (should_inject(10)) {  // 10% probability
        bpf_override_return(regs, 0);  // Failed to acquire
    }
    return 0;
}

// Inject -EIO on block reads
SEC("kprobe/submit_bio")
int BPF_KPROBE(fault_io) {
    // Mark bio as failed
}
```

**Configuration interface**:
```c
// Control fault injection dynamically
./fault_controller --enomem 5 --eagain 10 --eio 2
```

**Deliverable**: Multi-fault eBPF injector

---

### Week 7: Single VM Setup

**Goal**: Create one VM with custom kernel to test real code

**Tasks**:
1. Download Ubuntu cloud image
2. Create VM with virt-install
3. Build your kernel with async getdents patches
4. Deploy kernel to VM
5. Deploy Xibalba to VM
6. Run tests remotely

**Script to create**:
```bash
# vms/create_ext4_vm.sh
# Creates VM with ext4 filesystem
# Installs your custom kernel
# Ready for testing
```

**Validation**: 
- SSH into VM
- Run `./run_chaos_test.sh /test/ext4`
- See results

**Deliverable**: One working VM you can test against

---

### Week 8-9: Kernel Integration

**Goal**: Test your actual kernel patches with Xibalba

**Workflow**:
1. **Build kernel** with patches:
   ```bash
   cd ~/kernel/linux
   git checkout async-getdents-v1
   make -j$(nproc) bindeb-pkg
   ```

2. **Deploy to VM**:
   ```bash
   ./vms/deploy_kernel.sh linux-image-*.deb ext4
   ```

3. **Run Xibalba tests**:
   ```bash
   ./vms/run_tests.sh ext4 --with-ebpf --duration 300s
   ```

4. **Analyze results**:
   - Races detected?
   - Faults handled correctly?
   - Performance acceptable?

5. **Iterate**:
   - Fix bugs found
   - Rebuild kernel
   - Redeploy
   - Retest

**Deliverable**: Feedback loop for kernel development

---

### Week 10-11: Add More Filesystems

**Goal**: Expand from 1 to 3 filesystems

**VMs to add**:
- XFS (different directory format)
- tmpfs (in-memory, different characteristics)

**Why not all 5 yet?**:
- 3 filesystems covers most cases
- ZFS is complex (defer if time-limited)
- btrfs similar to ext4 for directory iteration

**Deliverable**: 3-filesystem test matrix

---

### Week 12-15: Automation & Polish

**Goal**: Make it easy to use

**What to build**:
1. One-command setup: `./SETUP_ALL.sh`
2. One-command testing: `./RUN_ALL_TESTS.sh`
3. HTML reports
4. Documentation for others

**Deliverable**: Usable by others, not just you

---

## Alternate Faster Path (If Time-Constrained)

**If you just want to test your kernel patches quickly**:

### Fast Track (6-8 weeks):

**Week 4-5**: Build race detector (Phase B)
**Week 6-7**: Single VM with your kernel (Phase C)
**Week 8**: Test your patches, find bugs, iterate

**Skip**:
- Multi-filesystem (just test on ext4)
- Advanced fault injection (pauses are enough)
- Automation (manual is fine)
- Fancy reports

**Deliverable**: Can test your kernel patches on ext4 with eBPF fault injection

**Then**: Expand later if needed

---

## My Recommendation

**Given your situation** (kernel VFS development, shared machine):

### 8-Week Focused Path:

1. **Week 4**: Race detector ← immediate value
2. **Week 5**: Enhanced eBPF (ENOMEM, EAGAIN) ← test error handling
3. **Week 6-7**: Single VM (ext4) + kernel deploy ← test real patches
4. **Week 8**: Iterate on patches with Xibalba feedback ← core value!
5. **Week 9-11**: Add XFS, tmpfs if time allows
6. **Week 12+**: Automation as needed

**Focus**: Get to testing real kernel code ASAP (Week 6-7)

**Defer**: 
- ZFS, btrfs (if ext4/XFS work, others likely do too)
- Fancy automation (manual is fine initially)
- Advanced eBPF modes (basic is enough)

---

## Success Metrics

**After 8 weeks, you should have**:

✅ **Working test framework**:
- Detects race conditions automatically
- Tests on 1-2 filesystems
- Can deploy and test custom kernels

✅ **Validated your kernel patches**:
- Tested with eBPF fault injection
- Found and fixed at least one bug
- Confidence in correctness

✅ **Reusable infrastructure**:
- Can test future patches
- Can expand to more filesystems
- Can share with others

---

## The Question

**What's your primary goal?**

**A)** Test kernel patches you're developing
   → **Recommended**: Fast track (Week 4 → 8)
   → **Focus**: Get to VM + kernel testing quickly

**B)** Build comprehensive testing framework
   → **Recommended**: Full path (Week 4 → 15)
   → **Focus**: Complete infrastructure

**C)** Research chaos testing techniques
   → **Recommended**: Exploratory path
   → **Focus**: Advanced eBPF modes, fault correlation

---

*Next conversation: Once de-risking succeeds, decide which path to take!*

