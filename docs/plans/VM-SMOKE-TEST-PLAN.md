# VM Smoke Test Plan for Modular Chaos

**Goal:** Verify eBPF execution works before declaring success  
**Time:** 10-15 minutes  
**Confidence gain:** 45% → 85%

---

## What We're Testing

**NOT testing:**
- ❌ Whether we find lots of bugs (unknown, don't care yet)
- ❌ Whether predictions are accurate (future work)
- ❌ Whether approach is optimal (iteration later)

**YES testing:**
- ✅ Does BPF load into kernel?
- ✅ Do tracepoints attach?
- ✅ Do delays inject?
- ✅ Does the system run without crashing?
- ✅ Do we get stats output?

**Success criteria:**
- BPF program loads (no verifier errors)
- Test runs for full duration
- `stats.delays_injected > 0` (proof injection works)
- Exit code 0 or bug count > 0 (either is fine!)

**Failure criteria:**
- BPF fails to load → 🔴 Fix BPF program
- Tracepoints don't attach → 🔴 Wrong tracepoint names
- `delays_injected == 0` → 🔴 Tracepoints not triggering
- Crashes/segfaults → 🔴 Code bugs

---

## Build Steps

### Step 1: Build initramfs with modular chaos
```bash
# This will take ~5 minutes (Docker, apt, cpio)
bazel build //vm:build_initramfs

# Verify chaos_test_runner is in there:
bazel-bin/vm/build-scripts/build-initramfs.sh --help 2>&1 | grep CHAOS_RUNNER
```

**Expected:** Initramfs builds successfully, includes:
- `/usr/bin/chaos_test_runner`
- `/opt/xibalba/bpf/rename_tracepoint.bpf.o`
- `/opt/xibalba/bpf/link_tracepoint.bpf.o`

---

### Step 2: Run baseline test (no injection)
```bash
# Build and run test
bazel test //vm:chaos_rename_baseline --test_output=all

# What to look for:
# ✅ Test completes
# ✅ "Bugs found: 0-5" (baseline should be clean)
# ✅ No crashes
```

**Expected output:**
```
=== Xibalba Modular Chaos Test ===
Workload: rename
Injector: none
Filesystem: btrfs
Duration: 60s
...
Test completed with exit code: 0
Bugs found: 0-5
```

**If this fails:**
- Check init-modular.sh parameters
- Check chaos_test_runner CLI
- Check workload module

---

### Step 3: Run smoke test (WITH injection) 🔬
```bash
# THE CRITICAL TEST
bazel test //vm:chaos_rename_with_tracepoint --test_output=all

# What to look for:
# ✅ BPF loads: "Loaded injector: rename_tracepoint"
# ✅ Injection works: "Delays injected: >0"
# ✅ Test runs: Duration completes
# ✅ Stats printed: "Injector stats" section
```

**Expected output (BEST CASE):**
```
=== Xibalba Modular Chaos Test ===
Workload: rename
Injector: rename_tracepoint (50% @ 15μs)
Filesystem: btrfs
Duration: 60s
...
✓ Loaded injector: rename_tracepoint
✓ Attached to tracepoints
...
Test completed with exit code: 1
Bugs found: 523

=== XIBALBA_INJECTOR_STATS ===
{
  "injector": "rename_tracepoint",
  "total_calls": 45230,
  "delays_injected": 22615,
  "injection_rate": 50.0,
  "total_delay_ms": 340
}
```

**Expected output (GOOD CASE):**
```
Bugs found: 10-50
Delays injected: >1000
```

**Expected output (MEH CASE):**
```
Bugs found: 1-10
Delays injected: >1000
```

**Expected output (BAD CASE but still DATA):**
```
Bugs found: 0
Delays injected: >1000  ← BUT THIS PROVES IT WORKS!
```

**Expected output (FAILURE):**
```
ERROR: Failed to load BPF program
ERROR: rename_tracepoint.bpf.o: No such file or directory
ERROR: bpf_object__load: -EINVAL
```

---

## Decision Matrix

### ✅ BPF loads AND delays inject AND bugs > 10
**Verdict:** 🎉 **SUCCESS! Ship it immediately!**  
**Action:**
- Merge PR
- Document results
- Run comprehensive experiments
- Write research paper

**Confidence:** 95%

---

### ✅ BPF loads AND delays inject AND bugs 1-10
**Verdict:** ✅ **WORKS! Ship with caveats**  
**Action:**
- Merge PR
- Document effectiveness
- Consider Phase 6 (fentry/fexit) for improvement
- Run more experiments

**Confidence:** 80%

---

### ✅ BPF loads AND delays inject AND bugs = 0
**Verdict:** ⚠️ **WORKS but ineffective**  
**Action:**
- BPF execution proven ✅
- But approach needs improvement
- Try Phase 6 (fentry/fexit)
- OR: Increase delay, probability
- OR: Different tracepoints

**Confidence:** 70% (execution works, effectiveness unknown)

---

### ❌ BPF loads BUT delays_injected = 0
**Verdict:** 🔴 **Tracepoints not triggering**  
**Action:**
- Check tracepoint names: `ls /sys/kernel/debug/tracing/events/syscalls/`
- Verify syscalls happening: `strace -e renameat2,unlinkat chaos_test_runner ...`
- May need different hooks
- Try manual bpftrace test

**Confidence:** 30% (broken)

---

### ❌ BPF fails to load
**Verdict:** 🔴🔴 **Fundamental issue**  
**Action:**
- Check verifier errors
- Simplify BPF program
- Check kernel config (CONFIG_BPF=y?)
- Check CAP_BPF available
- May need kernel upgrade

**Confidence:** 10% (broken)

---

## Post-Test Analysis

### If smoke test PASSES (any bugs > 0):

**Immediate:**
```bash
# Run all 4 VM tests
bazel test //vm:modular_chaos_tests --test_output=summary

# Collect results
grep "Bugs found:" bazel-testlogs/vm/*/test.log
```

**Next steps:**
1. ✅ Update implementation plan (Task 2 complete)
2. ✅ Run Task 3 (Analysis)
3. ✅ Document results
4. ✅ Merge PR
5. ✅ Write paper

---

### If smoke test FAILS (BPF doesn't work):

**Debug steps:**
1. Check kernel version: `uname -r` (need 5.5+)
2. Check BPF support: `cat /boot/config-$(uname -r) | grep CONFIG_BPF`
3. Check tracepoints exist: `ls /sys/kernel/debug/tracing/events/syscalls/ | grep rename`
4. Manual BPF test:
   ```bash
   sudo bpftrace -e 'tracepoint:syscalls:sys_exit_renameat2 { @[comm] = count(); }'
   # In another terminal: touch a; mv a b; mv b c
   # Should see counts increasing
   ```
5. Simplify BPF program (remove busy-wait, just count calls)

---

## What Success Looks Like

**Minimum (60% confidence):**
- BPF loads ✅
- Delays inject ✅
- Test runs ✅
- 0 bugs (but execution proven)

**Good (80% confidence):**
- BPF loads ✅
- Delays inject ✅
- 1-10 bugs ✅
- Stats show correct injection rate

**Excellent (95% confidence):**
- BPF loads ✅
- Delays inject ✅
- 10-50 bugs ✅
- Theory validated ✅

---

## Time Estimates

**Build initramfs:** 5 minutes  
**Run baseline:** 2 minutes  
**Run smoke test:** 3 minutes  
**Analyze results:** 5 minutes  
**Total:** 15 minutes

**If it works first try:** 🎉  
**If debugging needed:** +30-60 minutes

---

## Next Steps After Smoke Test

### If successful:
1. ✅ Commit and push
2. ✅ Update implementation plan
3. ✅ Run comprehensive experiments
4. ✅ Document findings
5. ✅ Merge PR

### If needs iteration:
1. 🔧 Debug and fix
2. 🔧 Re-run smoke test
3. 🔧 Iterate until works
4. ✅ Then comprehensive experiments

---

## The Question This Answers

**Before:** "Does our eBPF code actually work?"  
**Answer:** "Unknown (0% confidence)"

**After smoke test:** "Yes/No + specific details"  
**Confidence:** 85%

**This is the GAP between theory and reality!**

---

*Run this test BEFORE claiming anything works!*  
*Data > Assumptions*

