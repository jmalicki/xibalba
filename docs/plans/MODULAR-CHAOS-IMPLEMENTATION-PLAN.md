# Modular Chaos Testing - Complete Implementation Plan

**Start Date:** 2025-10-13  
**Branch:** enhance/ebpf-injection  
**Goal:** Complete modular chaos testing framework with working eBPF injectors

---

## Current Status (Starting Point)

### ✅ Completed
- [x] Phase 1: Interfaces and registries (1,160 lines)
- [x] Phase 2: Workload modules (908 lines)  
- [x] Phase 3: Generic controller (370 lines)
- [x] Unit tests (1,038 lines, 47 tests passing)
- [x] Documentation (4,687 lines of research)

### ⚠️ Blocked
- [ ] BPF injectors (kretprobe won't compile - PT_REGS_RC issue)

### 📋 Not Started
- [ ] Phase 4: Unified test runner
- [ ] Integration tests
- [ ] End-to-end validation

---

## Phase 3a: Tracepoint-Based Injectors (Quick Win)

**Goal:** Get working BPF injectors using syscall tracepoints  
**Time Estimate:** 2-3 hours  
**Expected Result:** Compiling BPF programs, 10-50 bugs per 100K ops

### Task 1: Create Tracepoint Rename Injector

- [ ] Create `chaos/injectors/rename_tracepoint.bpf.c`
  - [ ] Include standard BPF headers
  - [ ] Define config map (probability, delay, enabled)
  - [ ] Define stats map (calls, delays, timing)
  - [ ] Implement `should_inject()` helper
  - [ ] Implement `inject_delay()` helper (busy-wait)
  
- [ ] Hook `sys_exit_renameat2` tracepoint
  - [ ] Check if syscall succeeded (`ctx->ret == 0`)
  - [ ] Inject delay after successful rename
  - [ ] Update statistics
  
- [ ] Hook `sys_exit_unlinkat` tracepoint
  - [ ] Check if syscall succeeded
  - [ ] Inject delay after successful unlink
  - [ ] Update statistics

- [ ] Add to `chaos/injectors/BUILD.bazel`
  - [ ] Add genrule for `rename_tracepoint_bpf`
  - [ ] Use same compile flags as `pause_injector_bpf`
  - [ ] Verify builds successfully

- [ ] Test compilation
  - [ ] Run `bazel build //chaos/injectors:rename_tracepoint_bpf`
  - [ ] Fix any compilation errors
  - [ ] Verify .bpf.o file is created

**Estimated time:** 1 hour

---

### Task 2: Create Tracepoint Link Injector

- [ ] Create `chaos/injectors/link_tracepoint.bpf.c`
  - [ ] Same structure as rename_tracepoint
  - [ ] Define config and stats maps
  - [ ] Implement helpers
  
- [ ] Hook `sys_exit_linkat` tracepoint
  - [ ] Check for success
  - [ ] Inject delay (widens dirty read window!)
  - [ ] Update statistics
  
- [ ] Hook `sys_exit_unlinkat` tracepoint (for hardlink testing)
  - [ ] Different from rename version
  - [ ] Focus on ref count operations
  - [ ] Track unlink-specific stats

- [ ] Add to BUILD.bazel
  - [ ] Add genrule for `link_tracepoint_bpf`
  - [ ] Test compilation

**Estimated time:** 45 minutes

---

### Task 3: Update Injector Registry

- [ ] Update `chaos/injectors/registry.c`
  - [ ] Add descriptor for `injector_rename_tracepoint`
    - [ ] Name: "rename_tracepoint"
    - [ ] Description: "Delay at rename/unlink syscall exit"
    - [ ] BPF filename: "rename_tracepoint.bpf.o"
    - [ ] Filesystem support: generic (all)
    - [ ] Effectiveness: MEDIUM (expected 10-50 bugs)
    - [ ] Hook points: sys_exit_renameat2, sys_exit_unlinkat
    - [ ] Targets: rename visibility, file missing
  
  - [ ] Add descriptor for `injector_link_tracepoint`
    - [ ] Name: "link_tracepoint"  
    - [ ] Description: "Delay at link syscall exit (dirty read testing)"
    - [ ] BPF filename: "link_tracepoint.bpf.o"
    - [ ] Filesystem support: generic (all)
    - [ ] Effectiveness: MEDIUM (expected 10-30 bugs)
    - [ ] Hook points: sys_exit_linkat, sys_exit_unlinkat
    - [ ] Targets: transaction abort dirty reads, ref count races
  
  - [ ] Update `all_injectors[]` array
  - [ ] Export descriptors

- [ ] Update `chaos/injectors/injector.h`
  - [ ] Add extern declarations
  - [ ] Update documentation

**Estimated time:** 15 minutes

---

### Task 4: Add BPF Tests for New Injectors

**IMPORTANT:** eBPF execution testing happens in VMs, not on build host!

**Unit tests (no kernel needed):**
- [ ] Update `chaos/injectors/bpf_test.cc`
  - [ ] Add `RenameTracepoint_OpensSuccessfully` test
  - [ ] Add `RenameTracepoint_MapsDefinedInObject` test
  - [ ] Add `RenameTracepoint_MapStructure` test
  - [ ] Add `RenameTracepoint_ProgramDefined` test
  - [ ] Add `RenameTracepoint_HooksCorrectTracepoints` test
  
  - [ ] Add same 5 tests for `link_tracepoint`
  
  - [ ] NO execution tests in unit tests (those run in VM integration tests)

- [ ] Update BUILD.bazel data dependencies
  - [ ] Add `//chaos/injectors:rename_tracepoint_bpf`
  - [ ] Add `//chaos/injectors:link_tracepoint_bpf`

- [ ] Run tests
  - [ ] `bazel test //chaos/injectors:bpf_test`
  - [ ] Verify all tests pass (should be ~16 tests now)
  - [ ] All pass WITHOUT kernel permissions

**VM integration tests (Phase 5):**
- [ ] Actual execution happens in `bazel test //vm:...` tests
- [ ] VMs have kernel BPF enabled
- [ ] End-to-end testing with real filesystems

**Estimated time:** 30 minutes

---

### Task 5: Update Registry Tests

- [ ] Update `chaos/injectors/registry_test.cc`
  - [ ] Add "rename_tracepoint" to valid names test
  - [ ] Add "link_tracepoint" to valid names test
  - [ ] Verify tests still pass
  - [ ] Run `bazel test //chaos/injectors:registry_test`

**Estimated time:** 10 minutes

---

**Phase 3a Total Time:** 2-3 hours  
**Phase 3a Deliverables:**
- 2 new working BPF programs
- 2 new injector descriptors  
- 10+ new tests
- All tests passing

---

## Phase 4: Unified Test Runner

**Goal:** Single command to run any workload with any injector  
**Time Estimate:** 4-6 hours  
**Expected Result:** `chaos_test_runner --workload rename --injector rename_tracepoint /tmp/test`

### Task 1: Create Test Runner Foundation

- [ ] Create `chaos/tests/chaos_test_runner.c`
  - [ ] Add copyright header
  - [ ] Include necessary headers:
    - [ ] `workloads/workload.h`
    - [ ] `injectors/injector.h`
    - [ ] `controllers/controller.h`
    - [ ] `common/state_tracker.h`
    - [ ] Standard C headers (stdio, pthread, etc.)

- [ ] Define usage string
  ```c
  Usage: chaos_test_runner [OPTIONS] <directory>
  
  Options:
    --workload NAME      Workload to run (default: create_delete)
    --injector NAME      eBPF injector (default: none)
    --filesystem FS      Filesystem type (default: auto-detect)
    --model MODEL        Consistency model (posix/weak/strict/eventual)
    --duration N         Test duration in seconds (default: 60)
    --readers N          Number of reader threads (default: workload default)
    --writers N          Number of writer threads (default: workload default)
    --probability N      Injection probability % (default: injector default)
    --delay N            Injection delay μs (default: injector default)
    --json               Output as JSON
    --list-workloads     List available workloads
    --list-injectors     List available injectors
  ```

**Estimated time:** 30 minutes

---

### Task 2: Implement Argument Parsing

- [ ] Implement `parse_args()` function
  - [ ] Parse `--workload` (string)
  - [ ] Parse `--injector` (string)
  - [ ] Parse `--filesystem` (string)
  - [ ] Parse `--model` (enum)
  - [ ] Parse `--duration` (int)
  - [ ] Parse `--readers` (int, optional)
  - [ ] Parse `--writers` (int, optional)
  - [ ] Parse `--probability` (int, optional)
  - [ ] Parse `--delay` (int, optional)
  - [ ] Parse `--json` (bool)
  - [ ] Parse `--list-workloads` (bool)
  - [ ] Parse `--list-injectors` (bool)
  - [ ] Extract directory path (last argument)

- [ ] Implement validation
  - [ ] Check directory path provided
  - [ ] Check directory exists and is writable
  - [ ] Validate numeric ranges
  - [ ] Handle `--help`

**Estimated time:** 1 hour

---

### Task 3: Implement Workload Loading

- [ ] Implement workload lookup
  - [ ] Call `get_workload(workload_name)`
  - [ ] Handle not found (print list)
  - [ ] Handle `--list-workloads` flag

- [ ] Get thread counts
  - [ ] Use provided --readers or workload default
  - [ ] Use provided --writers or workload default
  - [ ] Validate reasonable ranges (1-100)

- [ ] Initialize workload state
  - [ ] Allocate `workload_state_t`
  - [ ] Set test_dir
  - [ ] Create state_tracker
  - [ ] Parse consistency model
  - [ ] Initialize atomics
  - [ ] Create bug_queue
  - [ ] Open scan_export file
  - [ ] Call `workload->init()`

**Estimated time:** 45 minutes

---

### Task 4: Implement Injector Loading (Optional)

- [ ] Implement injector lookup
  - [ ] Call `get_injector(injector_name)` if provided
  - [ ] Handle "none" case (no injection)
  - [ ] Handle not found (print list)
  - [ ] Handle `--list-injectors` flag

- [ ] Check compatibility
  - [ ] Auto-detect filesystem type if needed
  - [ ] Call `injector_supports_filesystem()`
  - [ ] Print error if incompatible
  - [ ] Suggest compatible injectors

- [ ] Check requirements
  - [ ] Call `injector_check_requirements()`
  - [ ] Print warning if unsupported
  - [ ] Continue anyway (best-effort)

- [ ] Load injector via controller
  - [ ] Build `injector_config_t`
    - [ ] Use provided --probability or injector default
    - [ ] Use provided --delay or injector default
    - [ ] enabled = true
  - [ ] Call `controller_load_injector()`
  - [ ] Handle errors (print error_buf)
  - [ ] Print success message with config

**Estimated time:** 1 hour

---

### Task 5: Run Test

- [ ] Launch threads
  - [ ] Create pthread arrays based on counts
  - [ ] Launch reader threads (call `workload->reader_fn`)
  - [ ] Launch writer threads (call `workload->writer_fn`)
  - [ ] Launch bug writer thread (from simple_chaos_test pattern)
  - [ ] Handle pthread_create failures

- [ ] Run for duration
  - [ ] Print status message
  - [ ] Sleep for specified duration
  - [ ] OR: Periodically print stats (every 10s)
    - [ ] Operations count
    - [ ] Bugs found
    - [ ] If injector: injection rate

- [ ] Stop threads
  - [ ] Set `atomic_store(&state.stop, true)`
  - [ ] Join all reader threads
  - [ ] Join all writer threads
  - [ ] Join bug writer thread

**Estimated time:** 45 minutes

---

### Task 6: Output Results

- [ ] Implement normal output mode
  - [ ] Print test summary
    - [ ] Workload name and description
    - [ ] Injector name (if used)
    - [ ] Duration
    - [ ] Thread counts
  - [ ] Print statistics
    - [ ] Total operations
    - [ ] Bugs found (missing, duplicates, phantoms)
    - [ ] Operations per second
    - [ ] Workload-specific stats
  - [ ] Print injector stats (if used)
    - [ ] Call `controller_print_stats()`
    - [ ] Injection rate
    - [ ] Total delays

- [ ] Implement JSON output mode
  - [ ] Build JSON structure
  - [ ] Include all metrics
  - [ ] Output to stdout
  - [ ] Format for machine parsing

**Estimated time:** 45 minutes

---

### Task 7: Cleanup and Error Handling

- [ ] Implement cleanup function
  - [ ] Unload injector (if loaded)
  - [ ] Cleanup workload state
  - [ ] Cleanup state tracker
  - [ ] Close scan_export file
  - [ ] Free allocations

- [ ] Handle signals
  - [ ] SIGINT handler (Ctrl+C)
  - [ ] SIGTERM handler
  - [ ] Set stop flag
  - [ ] Wait for threads
  - [ ] Clean shutdown

- [ ] Error handling throughout
  - [ ] Check all return values
  - [ ] Print meaningful errors
  - [ ] Exit with proper codes

**Estimated time:** 30 minutes

---

### Task 8: Build Integration

- [ ] Update `chaos/BUILD.bazel`
  - [ ] Add `chaos_test_runner` cc_binary
  - [ ] Dependencies:
    - [ ] `//chaos/workloads:workloads`
    - [ ] `//chaos/injectors:registry`
    - [ ] `//chaos/controllers:generic_controller`
    - [ ] `//common:state_tracker`
    - [ ] `//common:dir_reader`
  - [ ] linkopts: `-pthread`, `-lbpf`, `-lelf`, `-lz`

- [ ] Test build
  - [ ] `bazel build //chaos:chaos_test_runner`
  - [ ] Fix compilation errors
  - [ ] Verify binary created

**Estimated time:** 15 minutes

---

### Task 9: Documentation

- [ ] Update `chaos/README.md`
  - [ ] Add usage examples for `chaos_test_runner`
  - [ ] Document all command-line flags
  - [ ] Show example outputs
  - [ ] Update implementation status

- [ ] Update `chaos/tests/README.md` (create if needed)
  - [ ] Explain test runner architecture
  - [ ] Document how to add new tests
  - [ ] Troubleshooting guide

**Estimated time:** 30 minutes

---

**Phase 4 Total Time:** 4-6 hours  
**Phase 4 Deliverables:**
- Working `chaos_test_runner` (~400 lines)
- Updated documentation
- End-to-end functionality

---

## Phase 5: Integration Testing and Validation

**Goal:** Verify the complete system works  
**Time Estimate:** 2-3 hours  
**Expected Result:** Bugs found, architecture validated

**TESTING STRATEGY:**
- Unit tests run on build host (no kernel BPF needed)
- Integration tests run in VMs (via `bazel test //vm:...`)
- VMs have full kernel BPF support + multiple filesystems

### Task 1: Manual Testing (On Build Host)

- [ ] Test runner help and listing
  - [ ] Run `chaos_test_runner --help`
  - [ ] Run `chaos_test_runner --list-workloads`
  - [ ] Run `chaos_test_runner --list-injectors`
  - [ ] Verify output is helpful

- [ ] Test each workload WITHOUT injector (on tmpfs, no BPF)
  - [ ] `--workload create_delete /tmp/test`
  - [ ] `--workload rename /tmp/test`
  - [ ] `--workload hardlink /tmp/test`
  - [ ] `--workload mixed /tmp/test`
  - [ ] Verify no crashes
  - [ ] Verify operations execute
  - [ ] Expect 0 bugs (no injection)

**Estimated time:** 30 minutes

---

### Task 2: Create VM Integration Tests

**NOTE:** eBPF execution tests run in VMs, not on build host!

- [ ] Create `vm/tests/test_chaos_injectors.sh`
  - [ ] Script to run in VM with BPF support
  - [ ] Test getdents_delay with each workload
  - [ ] Test rename_tracepoint with rename workload
  - [ ] Test link_tracepoint with hardlink workload
  - [ ] Record results to output file

- [ ] Update VM test framework
  - [ ] Add chaos_test_runner to VM image
  - [ ] Ensure BPF is enabled in test kernel
  - [ ] Add btrfs filesystem to test VMs

- [ ] Run VM tests
  - [ ] `bazel test //vm:chaos_injector_tests`
  - [ ] Verify tests pass
  - [ ] Check results for bug detection

**Estimated time:** 1-2 hours

---

### Task 3: Analyze VM Test Results

- [ ] Review bug detection data from VMs
  - [ ] How many bugs with getdents_delay? (expect 0-5)
  - [ ] How many bugs with rename_tracepoint? (expect 10-50)
  - [ ] How many bugs with link_tracepoint? (expect 10-30)
  - [ ] Compare to predictions

- [ ] Analyze bug types
  - [ ] Missing files during rename?
  - [ ] Duplicate entries?
  - [ ] Reference count issues?
  - [ ] Match expected race conditions?

- [ ] Decision point
  - [ ] If >10 bugs/100K → SUCCESS! Ship it
  - [ ] If 5-10 bugs/100K → Marginal, consider fentry/fexit
  - [ ] If <5 bugs/100K → Need better precision (Phase 6)

**Estimated time:** 30 minutes (analysis)

---

### Task 4: Comprehensive Experiments

- [ ] Create test matrix script
  - [ ] Test all workload × injector combinations
  - [ ] Record results in JSON
  - [ ] 4 workloads × 3 injectors = 12 runs
  - [ ] Duration: 300s each = 1 hour total runtime

- [ ] Run experiments on different filesystems
  - [ ] btrfs (primary target)
  - [ ] ext4 (comparison)
  - [ ] xfs (comparison)
  - [ ] tmpfs (negative control - should find 0 bugs)

- [ ] Analyze results
  - [ ] Which workload finds most bugs?
  - [ ] Which injector is most effective?
  - [ ] Which filesystem has most bugs?
  - [ ] Compare to baseline (getdents_delay)

**Estimated time:** 3-4 hours (mostly runtime)

---

### Task 5: Update Documentation with Results

- [ ] Create `docs/results/TRACEPOINT-INJECTOR-RESULTS.md`
  - [ ] Document bug detection rates
  - [ ] Compare to baseline
  - [ ] Show effectiveness of each combination
  - [ ] Include example bugs found

- [ ] Update `docs/design/EBPF-INJECTION-RESEARCH-SUMMARY.md`
  - [ ] Add "Implementation Results" section
  - [ ] Validate or revise predictions
  - [ ] Document lessons learned

**Estimated time:** 30 minutes

---

**Phase 5 Total Time:** 2-3 hours (+ 1 hour test runtime)  
**Phase 5 Deliverables:**
- Validated bug detection
- Performance measurements
- Results documentation

---

## Phase 6 (Optional): fentry/fexit Injectors

**Goal:** Improve precision if tracepoint isn't effective enough  
**Time Estimate:** 1-2 days  
**Trigger:** Only if Phase 5 finds <10 bugs per 100K ops

### Task 1: Research fentry/fexit Timing

- [ ] Study kernel source for exact timing
  - [ ] Where does `vfs_rename` call `__btrfs_unlink_inode`?
  - [ ] Where does `btrfs_add_link` get called?
  - [ ] What's the exact sequence?

- [ ] Design multi-hook strategy
  - [ ] `fentry/__btrfs_unlink_inode` → record operation start
  - [ ] `fexit/__btrfs_unlink_inode` → record unlink complete
  - [ ] `fentry/btrfs_add_link` → delay HERE (the window!)
  - [ ] Use BPF map to correlate operations

- [ ] Document strategy in `docs/design/FENTRY-FEXIT-STRATEGY.md`

**Estimated time:** 4-6 hours (research heavy)

---

### Task 2: Implement fentry/fexit Injector

- [ ] Create `chaos/injectors/rename_fentry.bpf.c`
  - [ ] Define operation tracking map
  - [ ] Implement `fentry/__btrfs_unlink_inode`
  - [ ] Implement `fexit/__btrfs_unlink_inode`
  - [ ] Implement `fentry/btrfs_add_link`
  - [ ] Coordinate via map

- [ ] Test compilation
  - [ ] May have issues with `BPF_PROG` macro
  - [ ] May need kernel 5.5+ check
  - [ ] Debug and fix

- [ ] Add to registry and BUILD

**Estimated time:** 3-4 hours

---

### Task 3: Compare Effectiveness

- [ ] Run same experiments as Phase 5
  - [ ] All workloads with fentry/fexit injector
  - [ ] Compare bug counts to tracepoint
  - [ ] Measure overhead

- [ ] Analyze trade-offs
  - [ ] Precision vs complexity
  - [ ] Bug count vs implementation time
  - [ ] Decide if worth it

**Estimated time:** 2-3 hours

---

**Phase 6 Total Time:** 1-2 days  
**Phase 6 Deliverables:**
- fentry/fexit injector (if effective)
- Comparison data
- Decision on best approach

---

## Phase 7 (Optional): kretprobe Deep Dive

**Goal:** Maximum precision for research publication  
**Time Estimate:** 3-7 days  
**Trigger:** Only if Phases 5-6 aren't effective enough AND this is for publication

### Task 1: Research kretprobe Solutions

- [ ] Research vmlinux.h generation
  - [ ] Study `bpftool btf dump`
  - [ ] Understand BTF format
  - [ ] Test on current kernel

- [ ] Research BPF CO-RE
  - [ ] Study `bpf_core_read.h`
  - [ ] Understand `BPF_CORE_READ` macro
  - [ ] Find examples in kernel tree

- [ ] Research PT_REGS_RC alternatives
  - [ ] Manual register access
  - [ ] Architecture-specific solutions
  - [ ] Cross-kernel-version portability

**Estimated time:** 1 day (reading and experimentation)

---

### Task 2: Generate vmlinux.h

- [ ] Install bpftool (if not present)
  - [ ] Check version
  - [ ] Ensure BTF support

- [ ] Generate vmlinux.h
  ```bash
  bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
  ```
  - [ ] Verify file generated
  - [ ] Check size (should be ~5MB)
  - [ ] Add to project

- [ ] Update build system
  - [ ] Add vmlinux.h to injectors directory
  - [ ] Update include paths
  - [ ] Test compilation

**Estimated time:** 2-4 hours

---

### Task 3: Implement CO-RE Version

- [ ] Rewrite `rename_window.bpf.c` with CO-RE
  ```c
  #include "vmlinux.h"
  #include <bpf/bpf_core_read.h>
  
  SEC("kretprobe/__btrfs_unlink_inode")
  int hook(struct pt_regs *ctx) {
      long ret = BPF_CORE_READ(ctx, ax);  // x86_64 return register
      if (ret != 0) return 0;
      inject_delay();
      return 0;
  }
  ```

- [ ] Test compilation
  - [ ] Expect new errors
  - [ ] Debug and fix
  - [ ] May need multiple iterations

- [ ] Test on running kernel
  - [ ] Load and attach
  - [ ] Verify works
  - [ ] Compare to tracepoint version

**Estimated time:** 1-2 days (lots of debugging)

---

### Task 4: Validate and Compare

- [ ] Run comprehensive comparison
  - [ ] Tracepoint vs fentry/fexit vs kretprobe
  - [ ] Measure bug detection rates
  - [ ] Measure overhead
  - [ ] Measure complexity

- [ ] Document findings
  - [ ] Which approach is best?
  - [ ] What are the trade-offs?
  - [ ] Recommendations for future work

**Estimated time:** 4-6 hours

---

**Phase 7 Total Time:** 3-7 days  
**Phase 7 Risk:** High (may not succeed)  
**Phase 7 Deliverables:**
- kretprobe injector (if successful)
- Comprehensive comparison
- Research paper material

---

## Alternative Paths

### Alternative 1: Kernel Module Approach

**If eBPF proves too limiting:**

- [ ] Create `xibalba.ko` kernel module
- [ ] Use kprobes API directly
- [ ] Real delays (not busy-wait)
- [ ] Maximum control

**Pros:** No eBPF limitations  
**Cons:** Must compile per kernel, can crash kernel

**Time:** 1-2 days  
**Use case:** Maximum control, don't care about eBPF specifically

---

### Alternative 2: Hybrid Approach

**Use different techniques for different injectors:**

- Tracepoint for generic injectors (getdents, vfs_delay)
- fentry/fexit for btrfs-specific (where available)
- Kernel module for transaction abort (if eBPF can't do it)

**Pros:** Use best tool for each job  
**Cons:** More complexity, more to maintain

---

### Alternative 3: Focus on Transaction Aborts Only

**Pivot to the most novel finding:**

The transaction abort dirty read theory is the breakthrough. Focus there:

- [ ] Simplify: Only implement transaction abort testing
- [ ] Use whatever hook works (tracepoint, kernel module, etc.)
- [ ] Prove the theory with real bugs
- [ ] Publish that finding

**Pros:** Focus on novel contribution  
**Cons:** Abandon broader chaos testing

---

## Complete Timeline

### **Week 1: Foundation**
**Day 1 (Today):**
- [x] Phase 1-2 complete (interfaces, workloads)
- [x] Unit tests (47 tests)
- [ ] Phase 3a: Tracepoint injectors (2-3 hours)

**Day 2:**
- [ ] Phase 4: Test runner implementation (4-6 hours)
- [ ] Basic validation testing

**Day 3:**
- [ ] Phase 5: Comprehensive experiments
- [ ] Document initial results
- [ ] Decision point: Is tracepoint good enough?

### **Week 2: Enhancement (If Needed)**
**Days 4-6:**
- [ ] Phase 6: fentry/fexit (if tracepoint insufficient)
- [ ] OR: Ship tracepoint version if effective
- [ ] OR: Start kretprobe research if needed

### **Week 3+: Advanced (If Research-Focused)**
**Days 7+:**
- [ ] Phase 7: kretprobe (if required for paper)
- [ ] Comprehensive comparison study
- [ ] Academic writing

---

## Decision Tree

```
START: Phase 3a (Tracepoint)
  ↓
  3 hours
  ↓
Phase 4 (Test Runner)
  ↓
  6 hours
  ↓
Phase 5 (Experiments)
  ↓
  Is bug detection good? (>10 bugs/100K ops)
  ├─ YES → ✅ SHIP IT
  │         Document results
  │         Write paper about transaction aborts
  │         Add more workloads/injectors over time
  │
  └─ NO → Try Phase 6 (fentry/fexit)
            ↓
            2 days
            ↓
            Is bug detection better?
            ├─ YES → ✅ USE IT
            │         Compare approaches in paper
            │
            └─ NO → Try Phase 7 (kretprobe) OR
                    ├─ Accept current results
                    ├─ Try kernel module
                    └─ Re-evaluate approach
```

---

## Success Criteria by Phase

### Phase 3a Success:
- [ ] Tracepoint BPF programs compile
- [ ] Registry updated
- [ ] Tests pass

### Phase 4 Success:
- [ ] Test runner runs without crashing
- [ ] Can select workload and injector
- [ ] Proper error messages
- [ ] Statistics collected

### Phase 5 Success:
- [ ] Finds >10 bugs per 100K ops with tracepoint
- [ ] Better than baseline (getdents_delay)
- [ ] Architecture proven effective

### Phase 6 Success (Optional):
- [ ] fentry/fexit finds 2-5x more bugs than tracepoint
- [ ] Worth the added complexity

### Phase 7 Success (Optional):
- [ ] kretprobe compiles and runs
- [ ] Finds 2-5x more bugs than fentry/fexit
- [ ] Enables research publication

---

## Risk Assessment

### Low Risk Tasks (Will Succeed):
- ✅ Phase 3a (tracepoint) - proven to work
- ✅ Phase 4 (test runner) - just glue code
- ✅ Phase 5 (experiments) - just running tests

### Medium Risk Tasks (Should Succeed):
- ⚠️ Phase 6 (fentry/fexit) - may have issues
- ⚠️ Bug detection rates - unknown effectiveness

### High Risk Tasks (May Fail):
- 🔴 Phase 7 (kretprobe) - complex, may not solve
- 🔴 Finding enough bugs - depends on kernel/workload

---

## Recommendations by Goal

### **Goal: Working Tool in Production**
**Path:** Phase 3a → Phase 4 → Phase 5  
**Time:** 1 week  
**Stop when:** Finds bugs consistently

### **Goal: Research Publication**
**Path:** Phase 3a → Phase 4 → Phase 5 → Phase 6 → Phase 7  
**Time:** 2-3 weeks  
**Stop when:** Have comprehensive comparison

### **Goal: Find Specific Bug**
**Path:** Phase 3a → Phase 4 → Run targeted tests  
**Time:** 2-3 days  
**Stop when:** Bug reproduced

### **Goal: Validate Transaction Abort Theory**
**Path:** Focus on link_tracepoint + hardlink workload  
**Time:** 1 week  
**Stop when:** Theory proven or disproven

---

## My Concrete Proposal for Next Session

### **Immediate Next Steps:**

**Session 1 (Now → 2 hours):**
- [ ] Implement `rename_tracepoint.bpf.c`
- [ ] Implement `link_tracepoint.bpf.c`
- [ ] Update registry
- [ ] Add tests
- [ ] Verify builds

**Session 2 (2-3 hours):**
- [ ] Implement `chaos_test_runner.c`
- [ ] Argument parsing
- [ ] Workload + injector integration
- [ ] Output formatting

**Session 3 (1-2 hours):**
- [ ] Manual testing
- [ ] Run experiments
- [ ] Analyze results
- [ ] Make decision: ship, enhance, or pivot

**Total:** 1-2 days of work spread over 3 sessions

**Decision point after Session 3:**
- Effective? → Ship and document
- Not effective? → Try fentry/fexit
- Partially effective? → Enhance and iterate

---

## Open Questions for You

1. **Primary goal?**
   - Quick win (working tool)?
   - Research rigor (comprehensive comparison)?
   - Specific bug hunting (transaction aborts)?

2. **Time available?**
   - 1 week (pragmatic path)?
   - 2-3 weeks (thorough investigation)?
   - Ongoing project (try everything)?

3. **Success criteria?**
   - Find any bugs (>10)?
   - Find many bugs (>100)?
   - Prove concept?

4. **Risk tolerance?**
   - Low (tracepoint, will work)?
   - Medium (fentry/fexit, should work)?
   - High (kretprobe, may work)?

---

*My vote: Start with tracepoint, iterate based on results.*  
*The architecture is solid - now let's get data!*

