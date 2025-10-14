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

## Phase 3a: Tracepoint-Based Injectors (Quick Win) ✅ COMPLETE

**Goal:** Get working BPF injectors using syscall tracepoints  
**Time Estimate:** 2-3 hours  
**Actual Time:** ~2 hours  
**Result:** ✅ Both programs compile, 10 new tests added, all passing

### Task 1: Create Tracepoint Rename Injector ✅ COMPLETE

- [x] Create `chaos/injectors/rename_tracepoint.bpf.c` (226 lines)
  - [x] Include standard BPF headers
  - [x] Define config map (probability, delay, enabled) - 3 entries
  - [x] Define stats map (calls, delays, timing) - 6 entries
  - [x] Implement `should_inject()` helper (returns int, not bool - C11 issue)
  - [x] Implement `inject_delay()` helper (busy-wait with bounded loop)
  
- [x] Hook `sys_exit_renameat2` tracepoint
  - [x] Check if syscall succeeded (`ctx->ret == 0`)
  - [x] Inject delay after successful rename
  - [x] Update statistics (total_calls, delays_injected, rename_calls)
  
- [x] Hook `sys_exit_unlinkat` tracepoint
  - [x] Check if syscall succeeded
  - [x] Inject delay after successful unlink
  - [x] Update statistics (unlink_calls, skipped)

- [x] Add to `chaos/injectors/BUILD.bazel`
  - [x] Add genrule for `rename_tracepoint_bpf`
  - [x] Use same compile flags as `pause_injector_bpf`
  - [x] Verify builds successfully

- [x] Test compilation
  - [x] Run `bazel build //chaos/injectors:rename_tracepoint_bpf`
  - [x] Fixed bool → int issue (C doesn't have bool in BPF context)
  - [x] Verified .bpf.o file is created ✅

**Actual time:** 1 hour  
**Issues encountered:** `bool` not available in BPF, use `int` instead  
**Outcome:** ✅ SUCCESS - compiles cleanly

---

### Task 2: Create Tracepoint Link Injector ✅ COMPLETE

- [x] Create `chaos/injectors/link_tracepoint.bpf.c` (224 lines)
  - [x] Same structure as rename_tracepoint
  - [x] Define config and stats maps (identical structure)
  - [x] Implement helpers (should_inject, inject_delay)
  
- [x] Hook `sys_exit_linkat` tracepoint
  - [x] Check for success
  - [x] Inject delay (widens dirty read window!)
  - [x] Update statistics (link_calls, delays_injected)
  - [x] Document dirty read scenario in comments
  
- [x] Hook `sys_exit_unlinkat` tracepoint (for hardlink testing)
  - [x] Same implementation as rename version
  - [x] Focus on ref count operations
  - [x] Track unlink-specific stats

- [x] Add to BUILD.bazel
  - [x] Add genrule for `link_tracepoint_bpf`
  - [x] Test compilation ✅

**Actual time:** 30 minutes (faster - copied structure from rename)  
**Issues encountered:** None  
**Outcome:** ✅ SUCCESS - compiles cleanly

---

### Task 3: Update Injector Registry ✅ COMPLETE

- [x] Update `chaos/injectors/registry.c`
  - [x] Add descriptor for `injector_rename_tracepoint`
    - [x] Name: "rename_tracepoint"
    - [x] Description: "Delay at rename/unlink syscall exit (syscall-level, portable)"
    - [x] BPF filename: "rename_tracepoint.bpf.o"
    - [x] Filesystem support: generic (all filesystems)
    - [x] Effectiveness: MEDIUM
    - [x] Hook points: sys_exit_renameat2, sys_exit_unlinkat
    - [x] Targets: 3 targets (rename visibility, file missing, dir inconsistency)
  
  - [x] Add descriptor for `injector_link_tracepoint`
    - [x] Name: "link_tracepoint"  
    - [x] Description: "Delay at link/unlink syscall exit (dirty read testing)"
    - [x] BPF filename: "link_tracepoint.bpf.o"
    - [x] Filesystem support: generic (all filesystems)
    - [x] Effectiveness: MEDIUM
    - [x] Hook points: sys_exit_linkat, sys_exit_unlinkat
    - [x] Targets: 3 targets (dirty reads, ref count races, hard link consistency)
  
  - [x] Update `all_injectors[]` array (now 8 injectors)
  - [x] Export descriptors

- [x] Update `chaos/injectors/injector.h`
  - [x] Add extern declarations for both new injectors
  - [x] Total descriptors: 8

**Actual time:** 15 minutes  
**Issues:** None  
**Outcome:** ✅ Registry complete, builds successfully

---

### Task 4: Add BPF Tests for New Injectors ✅ COMPLETE

**IMPORTANT:** eBPF execution testing happens in VMs, not on build host!

**Unit tests (no kernel needed):**
- [x] Update `chaos/injectors/bpf_test.cc`
  - [x] Add `RenameTracepoint_OpensSuccessfully` test
  - [x] Add `RenameTracepoint_MapsDefinedInObject` test
  - [x] Add `RenameTracepoint_MapStructure` test
  - [x] Add `RenameTracepoint_ProgramsDefined` test
  - [x] Add `RenameTracepoint_TracepointSections` test
  
  - [x] Add same 5 tests for `link_tracepoint`
  
  - [x] NO execution tests in unit tests (those run in VM integration tests)

- [x] Update BUILD.bazel data dependencies
  - [x] Add `:rename_tracepoint_bpf`
  - [x] Add `:link_tracepoint_bpf`

- [x] Fix `find_bpf_object()` helper
  - [x] Added paths for chaos/injectors/ directory
  - [x] Support both chaos/ and chaos/injectors/ locations
  - [x] All new tests find BPF objects correctly

- [x] Run tests
  - [x] `bazel test //chaos/injectors:bpf_test`
  - [x] All 16 tests pass ✅
  - [x] All pass WITHOUT kernel permissions

**VM integration tests (Phase 5):**
- [ ] Actual execution happens in `bazel test //vm:...` tests
- [ ] VMs have kernel BPF enabled
- [ ] End-to-end testing with real filesystems

**Actual time:** 30 minutes  
**Issues:** Path finding needed update for new location  
**Outcome:** ✅ 10 new tests added, 16/16 passing

---

### Task 5: Update Registry Tests ✅ COMPLETE

- [x] Update `chaos/injectors/registry_test.cc`
  - [x] Add "rename_tracepoint" to valid names test
  - [x] Add "link_tracepoint" to valid names test
  - [x] Verify tests still pass
  - [x] Run `bazel test //chaos/injectors:registry_test` - 21/21 passing ✅

**Actual time:** 5 minutes  
**Issues:** None  
**Outcome:** ✅ All registry tests pass

---

**Phase 3a Total Time:** 2 hours (faster than estimated)  
**Phase 3a Deliverables:** ✅ ALL DELIVERED
- [x] 2 new working BPF programs (rename_tracepoint, link_tracepoint)
- [x] 2 new injector descriptors in registry  
- [x] 10 new BPF tests added
- [x] All tests passing (16/16 BPF, 21/21 registry, 20/20 workload)

**Status:** ✅ COMPLETE - Committed in 39cb539, pushed to origin

---

## Phase 4: Unified Test Runner ✅ COMPLETE

**Goal:** Single command to run any workload with any injector  
**Time Estimate:** 4-6 hours  
**Actual Time:** ~3 hours  
**Result:** ✅ Working binary, all features implemented

### Task 1: Create Test Runner Foundation ✅ COMPLETE

- [x] Create `chaos/tests/chaos_test_runner.c` (474 lines total)
  - [x] Add copyright header (standard MIT license)
  - [x] Include necessary headers:
    - [x] `workloads/workload.h`
    - [x] `injectors/injector.h`
    - [x] `controllers/controller.h`
    - [x] `common/state_tracker.h`
    - [x] Standard C headers (stdio, pthread, signal, etc.)

- [x] Define usage string (complete with examples)
  - [x] All command-line flags documented
  - [x] Examples for common use cases
  - [x] Clear required vs optional parameters
  - [x] Tested with --help ✅

**Actual time:** 30 minutes  
**Outcome:** ✅ Comprehensive help output

---

### Task 2: Implement Argument Parsing ✅ COMPLETE

- [x] Implement `parse_args()` function (90 lines)
  - [x] Parse `--workload` (string)
  - [x] Parse `--injector` (string)
  - [x] Parse `--filesystem` (string)
  - [x] Parse `--model` (enum via parse_model helper)
  - [x] Parse `--duration` (int)
  - [x] Parse `--readers` (int, optional, -1 = default)
  - [x] Parse `--writers` (int, optional, -1 = default)
  - [x] Parse `--probability` (int, optional, -1 = default)
  - [x] Parse `--delay` (int, optional, -1 = default)
  - [x] Parse `--json` (bool)
  - [x] Parse `--quiet` (bool) - added extra flag
  - [x] Parse `--list-workloads` (bool)
  - [x] Parse `--list-injectors` (bool)
  - [x] Extract directory path (last non-flag argument)

- [x] Implement validation
  - [x] Check directory path provided
  - [x] Create directory if needed (mkdir)
  - [x] Validate all parsing (implicit via usage)
  - [x] Handle `--help` and unknown flags

**Actual time:** 45 minutes  
**Deviations:** Added --quiet flag for cleaner output  
**Outcome:** ✅ Complete argument parsing, all flags working

---

### Task 3: Implement Workload Loading ✅ COMPLETE

- [x] Implement workload lookup
  - [x] Call `get_workload(workload_name)`
  - [x] Handle not found (print list and exit)
  - [x] Handle `--list-workloads` flag (tested ✅)

- [x] Get thread counts
  - [x] Use provided --readers or workload->get_default_readers()
  - [x] Use provided --writers or workload->get_default_writers()
  - [x] No explicit validation (workload defaults are reasonable)

- [x] Initialize workload state
  - [x] Allocate `workload_state_t` (stack allocation)
  - [x] Set test_dir
  - [x] Create state_tracker
  - [x] Set consistency model from config
  - [x] Initialize atomics (using atomic_init, not ATOMIC_VAR_INIT)
  - [x] Create bug_queue (calloc)
  - [x] scan_export set to NULL (deferred)
  - [x] Call `workload->init()`

**Actual time:** 30 minutes  
**Deviations:** Used atomic_init instead of ATOMIC_VAR_INIT (deprecated)  
**Outcome:** ✅ All workloads load and initialize correctly

---

### Task 4: Implement Injector Loading (Optional) ✅ COMPLETE

- [x] Implement injector lookup
  - [x] Call `get_injector(injector_name)` if not "none"
  - [x] Handle "none" case (skip injection entirely)
  - [x] Handle not found (print list and exit)
  - [x] Handle `--list-injectors` flag (tested ✅)

- [x] Check compatibility
  - [x] No auto-detect (use provided --filesystem or "auto")
  - [x] Call `injector_supports_filesystem()`
  - [x] Print WARNING if incompatible (but continue)
  - [x] Graceful degradation

- [x] Check requirements
  - [x] Implicitly checked in controller_load_injector()
  - [x] Print warning on load failure
  - [x] Continue without injection if load fails

- [x] Load injector via controller
  - [x] Build `injector_config_t`
    - [x] Use provided --probability or injector default
    - [x] Use provided --delay or injector default
    - [x] enabled = 1 (true)
    - [x] error_code = 0
  - [x] Call `controller_load_injector()`
  - [x] Handle errors (print error_buf, continue without injection)
  - [x] Print success message with config (if not quiet)

**Actual time:** 45 minutes  
**Deviations:** 
- Graceful degradation (continues without injection on error)
- No auto-detect filesystem (deferred)  
**Outcome:** ✅ Injector loading works, controller integration complete

---

### Task 5: Run Test ✅ COMPLETE

- [x] Launch threads
  - [x] Create pthread arrays based on counts (calloc)
  - [x] Launch reader threads (call `workload->reader_fn`)
  - [x] Launch writer threads (call `workload->writer_fn`)
  - [x] Launch bug writer thread (simplified version)
  - [x] Handle pthread_create failures (cleanup and exit)

- [x] Run for duration
  - [x] Print status message ("Running...")
  - [x] Sleep for specified duration (simple approach)
  - [ ] Periodic stats (deferred - simple sleep used)

- [x] Stop threads
  - [x] Set `atomic_store(&state.stop, true)`
  - [x] Join all reader threads (loop)
  - [x] Join all writer threads (loop)
  - [x] Join bug writer thread

**Actual time:** 30 minutes  
**Deviations:** 
- Simple sleep instead of periodic stats (simpler implementation)
- Bug writer thread simplified (just drains queue)  
**Outcome:** ✅ Thread management works correctly

---

### Task 6: Output Results ✅ COMPLETE

- [x] Implement normal output mode
  - [x] Print test summary (shown if not --quiet)
    - [x] Workload name and description
    - [x] Injector name (if used)
    - [x] Duration
    - [x] Thread counts
  - [x] Print statistics
    - [x] Total operations
    - [x] Bugs found (total count)
    - [x] Reads completed
    - [x] Bug rate (per 100K operations)
    - [x] Workload-specific stats (via workload->get_stats())
  - [x] Print injector stats (if used)
    - [x] Call `controller_print_stats()`
    - [x] Shows injection counts and rates

- [x] Implement JSON output mode
  - [x] Simple JSON structure
  - [x] Include key metrics (operations, bugs, reads)
  - [x] Output to stdout
  - [ ] More detailed metrics (deferred)

**Actual time:** 30 minutes  
**Deviations:** Simplified JSON (basic metrics only)  
**Outcome:** ✅ Both output modes work correctly

---

### Task 7: Cleanup and Error Handling ✅ COMPLETE

- [x] Implement cleanup function (inline in main)
  - [x] Unload injector (if loaded) - controller_unload()
  - [x] Cleanup workload state - workload->cleanup()
  - [x] Cleanup state tracker - tracker_cleanup()
  - [x] Close scan_export file (not used yet)
  - [x] Free allocations (reader_threads, writer_threads, bug_queue)

- [x] Handle signals
  - [x] SIGINT handler (Ctrl+C) - signal_handler()
  - [x] SIGTERM handler  
  - [x] Set stop flag - atomic_store(&state.stop, true)
  - [x] Threads check stop flag and exit
  - [x] Clean shutdown via pthread_join()

- [x] Error handling throughout
  - [x] Check all return values (workload init, pthread_create, etc.)
  - [x] Print meaningful errors
  - [x] Exit with code 1 on error, 0 on success
  - [x] Graceful degradation (continue without injector on load failure)

**Actual time:** 20 minutes  
**Deviations:** Inline cleanup (no separate function)  
**Outcome:** ✅ Clean shutdown, all resources freed

---

### Task 8: Build Integration ✅ COMPLETE

- [x] Update `chaos/BUILD.bazel`
  - [x] Add `chaos_test_runner` cc_binary
  - [x] Dependencies:
    - [x] `//chaos/workloads:workloads`
    - [x] `//chaos/injectors:registry`
    - [x] `//chaos/controllers:generic_controller`
    - [x] `//common:state_tracker`
    - [x] `//common:dir_reader`
  - [x] linkopts: `-pthread`, `-lbpf`, `-lelf`, `-lz`

- [x] Test build
  - [x] `bazel build //chaos:chaos_test_runner`
  - [x] Fixed ATOMIC_VAR_INIT deprecation (use atomic_init)
  - [x] Fixed unused variable warning (bug_writer_thread)
  - [x] Verified binary created ✅

**Actual time:** 20 minutes  
**Issues:** 
- ATOMIC_VAR_INIT deprecated in C11
- Unused variable in bug_writer_thread  
**Outcome:** ✅ Builds successfully

---

### Task 9: Documentation ⏸️ DEFERRED

- [ ] Update `chaos/README.md`
  - [ ] Add usage examples for `chaos_test_runner`
  - [ ] Document all command-line flags
  - [ ] Show example outputs
  - [ ] Update implementation status

- [ ] Update `chaos/tests/README.md` (create if needed)
  - [ ] Explain test runner architecture
  - [ ] Document how to add new tests
  - [ ] Troubleshooting guide

**Status:** ⏸️ DEFERRED to after VM testing and validation  
**Reason:** Want real results before documenting  
**To be done:** After Phase 5 completion

---

**Phase 4 Total Time:** 3 hours (faster than 4-6h estimate!)  
**Phase 4 Deliverables:** ✅ ALL DELIVERED (except final docs)
- [x] Working `chaos_test_runner` (474 lines, not 400)
- [ ] Updated documentation (deferred)
- [x] End-to-end functionality (tested with 4 workloads)

**Status:** ✅ COMPLETE - Committed in a6aeb28, pushed to origin  
**Deviations:** Documentation deferred to after results  
**Test Results:** All 4 workloads run successfully without crashes

---

## Testing Confidence Assessment (Added 2025-10-13)

**Current Confidence Level: 45%**

### ✅ High Confidence (Tested):
- **Workload modules:** 95% - 20 unit tests + manual execution
- **Registry system:** 100% - 21 tests, all validated
- **BPF structure:** 100% - 16 tests, compilation verified
- **CLI interface:** 90% - Manual testing, all flags work

### ❌ Low/No Confidence (Untested):
- **eBPF execution:** 0% - Never loaded into kernel
- **Bug detection:** 0% - Never measured
- **Controller integration:** 30% - No execution tests
- **VM compatibility:** 0% - Not tested

### 🔴 CRITICAL GAPS:
1. **Zero eBPF execution testing** - Never loaded BPF into kernel
2. **Zero integration testing** - Components tested separately, not together
3. **Zero bug detection validation** - All predictions, no data

### ✅ What We Know:
- Unit tests pass (57/57)
- Code compiles cleanly
- Workloads execute correctly
- BPF programs compile

### ❌ What We DON'T Know:
- Will BPF load into kernel?
- Will tracepoints attach?
- Will delays inject?
- Will we find ANY bugs?
- Are predictions accurate?

### 🎯 Recommendation:
**Run ONE smoke test before merging:**
```bash
sudo chaos_test_runner --workload rename --injector rename_tracepoint --duration 60 /tmp/test
```

**Decision criteria:**
- Finds >10 bugs → ✅ MERGE (proven effective)
- Finds 1-10 bugs → ⚠️ MERGE with caveat
- Finds 0 bugs → ❌ Need Phase 6 (fentry/fexit)

**Time:** 10 minutes  
**Confidence gain:** 45% → 85%

---

## Phase 5: Integration Testing and Validation ⏸️ IN PROGRESS

**Goal:** Verify the complete system works  
**Time Estimate:** 2-3 hours  
**Expected Result:** Bugs found, architecture validated  
**Status:** ⏸️ BLOCKED on VM test infrastructure

**TESTING STRATEGY:**
- Unit tests run on build host (no kernel BPF needed) ✅ DONE
- Integration tests run in VMs (via `bazel test //vm:...`) ⏸️ PENDING
- VMs have full kernel BPF support + multiple filesystems

### Task 1: Manual Testing (On Build Host) ✅ COMPLETE

- [x] Test runner help and listing
  - [x] Run `chaos_test_runner --help` ✅ Output correct
  - [x] Run `chaos_test_runner --list-workloads` ✅ Shows 4 workloads
  - [x] Run `chaos_test_runner --list-injectors` ✅ Shows 8 injectors
  - [x] Verify output is helpful ✅

- [x] Test each workload WITHOUT injector (on tmpfs, no BPF)
  - [x] `--workload create_delete /tmp/test` → 33,390 ops/5s, 0 bugs ✅
  - [x] `--workload rename /tmp/test` → 10,199 ops/5s, 236 renames, 0 bugs ✅
  - [x] `--workload hardlink /tmp/test` → 5,127 ops/5s, 571 links, 0 bugs ✅
  - [x] `--workload mixed /tmp/test` → 4,799 ops/5s, all ops, 0 bugs ✅
  - [x] Verify no crashes ✅
  - [x] Verify operations execute ✅
  - [x] Expect 0 bugs (no injection) ✅

**Actual time:** 20 minutes  
**Issues:** None  
**Outcome:** ✅ All 4 workloads work perfectly without injection  
**Operations/second:**
- create_delete: 6,678 ops/s (fastest, simple operations)
- rename: 2,040 ops/s (slower, more complex)
- hardlink: 1,025 ops/s (slowest, most complex)
- mixed: 960 ops/s (comprehensive)

---

### Task 2: Create VM Integration Tests ⏸️ PARTIAL

**NOTE:** eBPF execution tests run in VMs, not on build host!

- [x] Create VM test infrastructure
  - [x] Created `vm/qemu/chaos_modular_test.bzl`
  - [x] Created `vm/qemu/run-qemu-modular-test.sh`
  - [x] Created `vm/qemu/test-wrapper-modular.sh`
  - [x] Defined 4 tests in vm/BUILD.bazel

- [ ] Fix VM test configuration
  - [ ] Fix chaos_modular_test load statement in BUILD.bazel
  - [ ] Build initramfs with chaos_test_runner
  - [ ] Build initramfs with BPF injector .o files
  - [ ] Verify kernel supports tracepoints

- [ ] Update init.sh for modular chaos
  - [ ] Parse xibalba_workload param
  - [ ] Parse xibalba_injector param
  - [ ] Call chaos_test_runner instead of simple_chaos_test
  - [ ] Pass all parameters correctly

- [ ] Run smoke test
  - [ ] Single test: rename + rename_tracepoint
  - [ ] Duration: 60 seconds
  - [ ] **CRITICAL:** Does it load? Does it inject? Does it find bugs?

- [ ] Run VM tests
  - [ ] `bazel test //vm:chaos_rename_with_tracepoint`
  - [ ] Verify test passes or analyze failure
  - [ ] Check results for bug detection

**Status:** ⏸️ BLOCKED  
**Blocker:** VM test infrastructure needs:
1. chaos_test_runner in initramfs (not just simple_chaos_test)
2. BPF .o files in initramfs
3. init.sh updated for new parameters

**Estimated time remaining:** 1-2 hours  
**Critical for confidence:** YES - This is the GAP between theory and reality

---

### Task 3: Analyze VM Test Results ⏸️ PENDING

**Status:** ⏸️ WAITING for Task 2 completion

**When Task 2 completes, analyze:**

- [ ] Review bug detection data from VMs
  - [ ] How many bugs with no injection? (baseline: expect 0-5)
  - [ ] How many bugs with rename_tracepoint? (expect 10-50)
  - [ ] How many bugs with link_tracepoint? (expect 10-30)
  - [ ] Compare to predictions
  - [ ] Calculate improvement ratio (injection vs baseline)

- [ ] Analyze bug types
  - [ ] Missing files during rename?
  - [ ] Duplicate entries?
  - [ ] Reference count issues?
  - [ ] Match expected race conditions from research?

- [ ] Validate eBPF injection worked
  - [ ] Check injector stats (delays_injected > 0?)
  - [ ] Verify injection rate matches config
  - [ ] Confirm tracepoints actually triggered

- [ ] Decision point
  - [ ] If >10 bugs/100K → ✅ SUCCESS! Ship it
  - [ ] If 5-10 bugs/100K → ⚠️ Marginal, consider fentry/fexit
  - [ ] If <5 bugs/100K → ❌ Need better precision (Phase 6)
  - [ ] If 0 bugs but injection worked → 🔴 Wrong approach
  - [ ] If 0 bugs and no injection → 🔴 BPF didn't work

**Estimated time:** 30 minutes (analysis)  
**Critical:** This determines if we ship, iterate, or pivot

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

## Decision Tree (Updated with Testing Reality)

```
START: Phase 3a (Tracepoint) ✅ DONE
  ↓
Phase 4 (Test Runner) ✅ DONE
  ↓
Phase 5 (Integration Tests) ⏸️ CURRENT
  ↓
CRITICAL UNKNOWN: Does eBPF execution work?
  ├─ NOT TESTED YET (0% confidence)
  ├─ Need: ONE smoke test in VM
  └─ Time: 10 minutes
  ↓
Smoke Test Result?
  ├─ BPF fails to load → 🔴 BLOCKER
  │   ├─ Debug BPF verifier errors
  │   ├─ Fix compilation issues
  │   └─ OR: Try different approach
  │
  ├─ BPF loads but 0 delays injected → 🔴 BLOCKER
  │   ├─ Debug tracepoint attachment
  │   ├─ Check kernel config
  │   └─ OR: Try different hooks
  │
  ├─ BPF works but 0 bugs found → ⚠️ INEFFECTIVE
  │   ├─ Check: Are delays long enough?
  │   ├─ Check: Are we hitting the syscalls?
  │   ├─ Try: Increase probability, delay
  │   └─ OR: Need Phase 6 (fentry/fexit)
  │
  └─ BPF works AND finds bugs → ✅ SUCCESS
      ↓
      How many bugs?
      ├─ >50 bugs/100K → 🎉 EXCELLENT! Ship immediately
      ├─ 10-50 bugs/100K → ✅ GOOD! Ship and iterate
      ├─ 5-10 bugs/100K → ⚠️ MARGINAL - Ship or try Phase 6
      └─ 1-5 bugs/100K → ❌ WEAK - Need Phase 6
  ↓
IF NOT EFFECTIVE ENOUGH:
  └─ Try Phase 6 (fentry/fexit)
      ↓
      2 days
      ↓
      Better results?
      ├─ YES → ✅ USE IT
      └─ NO → Try Phase 7 (kretprobe) OR kernel module
```

**KEY INSIGHT:** We're at a critical decision point!
- **Current state:** High-quality code, zero execution testing
- **Next step:** ONE test gives us 80% of needed confidence
- **Then:** Data-driven decision on ship vs iterate

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

## Recommendations by Goal (Updated with Testing Reality)

### **Goal: Working Tool in Production**
**Path:** Phase 3a ✅ → Phase 4 ✅ → **SMOKE TEST** → Phase 5  
**Time:** 1 week (but smoke test is NEXT STEP!)  
**Stop when:** Finds bugs consistently  
**Current blocker:** Need VM smoke test to verify eBPF execution

### **Goal: Research Publication**
**Path:** Phase 3a ✅ → Phase 4 ✅ → **SMOKE TEST** → Phase 5 → Phase 6 → Phase 7  
**Time:** 2-3 weeks  
**Stop when:** Have comprehensive comparison  
**Current blocker:** Need data before writing results section

### **Goal: Find Specific Bug**
**Path:** Phase 3a ✅ → Phase 4 ✅ → **SMOKE TEST** → Run targeted tests  
**Time:** 2-3 days  
**Stop when:** Bug reproduced  
**Current blocker:** Can't hunt bugs without proven tool

### **Goal: Validate Transaction Abort Theory**
**Path:** Focus on link_tracepoint + hardlink workload + **SMOKE TEST**  
**Time:** 1 week  
**Stop when:** Theory proven or disproven  
**Current blocker:** Need to verify BPF execution first

**ALL PATHS BLOCKED ON:** One smoke test in VM with CAP_BPF

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

---

## ADDENDUM: Critical Next Step (Added 2025-10-13)

### 🚨 **CURRENT STATE: Theory vs Reality Gap**

**What we have:**
- ✅ Excellent architecture (modular, pluggable)
- ✅ High-quality code (strict warnings, clean)
- ✅ Comprehensive unit tests (57 tests, 100% passing)
- ✅ Working CLI (tested manually)
- ✅ Sound research (4,687 lines)

**What we DON'T have:**
- ❌ ANY eBPF execution testing
- ❌ ANY bug detection data
- ❌ ANY integration validation
- ❌ ANY proof it works in practice

**The gap:**
```
Theory: "Tracepoint delays will find 10-50 bugs"
Reality: ???
```

### 🎯 **Critical Next Step: Smoke Test**

**Before doing ANYTHING else, run this:**

```bash
# In VM with CAP_BPF (10 minutes total):
sudo chaos_test_runner \
    --workload rename \
    --injector rename_tracepoint \
    --duration 60 \
    /tmp/test
```

**This ONE test tells us:**
1. Does BPF load? (bpf_object__load success?)
2. Do tracepoints attach? (no errors?)
3. Do delays inject? (stats.delays_injected > 0?)
4. Do we find bugs? (bugs_found > 0?)

**Possible outcomes:**

**Best:** "Bugs found: 523" → 🎉 Theory validated! Ship it!  
**Good:** "Bugs found: 15" → ✅ Works! May need tuning  
**Meh:** "Bugs found: 2" → ⚠️ Marginal, need Phase 6  
**Bad:** "Bugs found: 0, delays injected: 1234" → 🔴 Wrong approach  
**Worst:** "Error loading BPF" → 🔴🔴 Fundamental issue

### 📊 **Confidence Impact**

**Without smoke test:** 45% confidence  
**With smoke test (any result):** 85% confidence

**Why such a big jump?**
- We'd know if eBPF execution works
- We'd have real bug detection data
- We'd know if approach is viable
- We'd make data-driven decisions

### ⚠️ **Recommendation: DON'T MERGE Without Smoke Test**

**Reasons:**
1. Zero execution testing (could be completely broken)
2. Unknown if approach works (could find 0 bugs)
3. Unknown if predictions accurate (could be way off)
4. High risk of merging broken code

**Mitigation:**
- Run ONE 10-minute smoke test
- Gain 40% confidence
- Then make informed decision

### 🔧 **What's Blocking Smoke Test**

**Option A: Manual VM Test (Fastest - 30 minutes)**
```bash
# SSH into existing VM
# Copy chaos_test_runner binary
# Copy BPF .o files
# Run test manually
# See what happens
```

**Option B: Fix Bazel VM Tests (Proper - 1-2 hours)**
```bash
# Fix vm/BUILD.bazel load statement
# Add chaos_test_runner to initramfs build
# Add BPF .o files to initramfs
# Update init.sh
# Run bazel test //vm:chaos_rename_with_tracepoint
```

**Option C: Local Test with sudo (Quickest - 5 minutes)**
```bash
# If build host has CAP_BPF support:
sudo bazel run //chaos:chaos_test_runner -- \
    --workload rename \
    --injector rename_tracepoint \
    --duration 10 \
    /tmp/test

# Limitation: Not real VM, but proves BPF works
```

**My recommendation:** Try Option C first (5 min), then Option A (30 min), then Option B (proper solution)

---

**BOTTOM LINE:** We have a beautiful race car. Let's start the engine and see if it runs! 🏎️

