# What's Left To Do

## TL;DR

**Everything is built. Nothing has been tested with eBPF execution.**

We need to **RUN ONE TEST** to verify it works, then decide what to do next.

---

## Current State: ✅ Built, ❌ Not Tested

### ✅ What's DONE (100% complete):

**Infrastructure:**
- [x] Modular architecture (interfaces, registries)
- [x] 4 workload modules (create_delete, rename, hardlink, mixed)
- [x] 2 eBPF injectors (rename_tracepoint, link_tracepoint)
- [x] Generic controller (loads BPF, manages maps)
- [x] Unified test runner (chaos_test_runner CLI)
- [x] 57 unit tests (100% passing)
- [x] VM integration (initramfs with all components)
- [x] Smoke test plan documented
- [x] PR updated and ready

**Code quality:**
- [x] Compiles cleanly
- [x] No warnings
- [x] Tests pass
- [x] Linter happy

### ❌ What's NOT DONE (0% complete):

**Execution testing:**
- [ ] Never run eBPF in kernel
- [ ] Never loaded BPF program
- [ ] Never attached tracepoints
- [ ] Never injected delays
- [ ] Never measured bugs
- [ ] Never validated predictions

---

## What's Left: ONE Test + Analysis

### Step 1: Run Smoke Test (15 minutes)

**Command:**
```bash
bazel test //vm:chaos_rename_with_tracepoint --test_output=all
```

**What this does:**
1. Builds initramfs with chaos_test_runner + BPF programs
2. Boots QEMU VM
3. Formats btrfs filesystem
4. Runs chaos_test_runner with rename workload + tracepoint injector
5. Shows output

**What we're checking:**
- ✅ Does BPF load? (no verifier errors)
- ✅ Do tracepoints attach? (no "not found" errors)
- ✅ Do delays inject? (stats.delays_injected > 0)
- ✅ Does it run without crashing?

**NOT checking:**
- ❌ How many bugs (don't care yet!)
- ❌ Is it effective (too early!)
- ❌ Are predictions accurate (future work!)

**Time:** 15 minutes total
- 5 min: Build initramfs (Docker, apt, cpio)
- 2 min: Boot VM
- 2 min: Run test (60 seconds duration)
- 1 min: Shutdown
- 5 min: Analyze output

---

### Step 2: Analyze Results (5 minutes)

**Look for in the output:**

✅ **SUCCESS indicators:**
```
✓ Loaded injector: rename_tracepoint
✓ Attached to tracepoints
...
Delays injected: 1234
Total calls: 5678
```

🔴 **FAILURE indicators:**
```
ERROR: Failed to load BPF program
ERROR: bpf_object__load: -EINVAL
ERROR: Tracepoint not found: sys_exit_renameat2
```

---

### Step 3: Make Decision (1 minute)

**Based on results:**

**If BPF loads AND delays > 0:**
→ ✅ **IT WORKS!** Proceed to Step 4

**If BPF fails to load:**
→ 🔴 **Debug:** Check verifier errors, simplify program

**If delays == 0:**
→ 🔴 **Debug:** Check tracepoint names, verify syscalls happening

---

### Step 4: (Optional) Run All VM Tests (30 minutes)

**If Step 1 succeeded:**
```bash
# Run all 4 tests
bazel test //vm:modular_chaos_tests --test_output=summary
```

**This gives us:**
- Baseline (no injection) bug counts
- rename_tracepoint bug counts
- link_tracepoint bug counts
- Comparison data

---

## Decision Tree After Testing

### If smoke test shows eBPF works:

**Option A: Merge as-is**
- ✅ Infrastructure proven
- ✅ Can find bugs (or at least doesn't crash)
- ✅ Ready for iteration

**Option B: Run comprehensive experiments first**
- Run all workload × injector combinations
- Measure bug detection rates
- Document effectiveness
- THEN merge

**My recommendation:** Option A (merge proven infrastructure, iterate)

---

### If smoke test shows eBPF doesn't work:

**Debug steps:**
1. Check verifier errors
2. Simplify BPF program
3. Verify tracepoints exist on this kernel
4. Try manual bpftrace test
5. Fix issues
6. Re-run smoke test

**Then:** Merge when working

---

## Why This Feels Confusing

**We built a LOT:**
- 32 commits
- 46 files
- 15,921 lines
- 57 tests
- 4,687 lines of docs

**But we never ran the MAIN THING:**
- Zero eBPF execution

**It's like:**
- ✅ Built a car (done)
- ✅ Tested every component (done)
- ✅ Wrote the manual (done)
- ❌ Never turned the key (NOT DONE)

---

## Simple Answer: What's Left?

**1. Run this command:**
```bash
bazel test //vm:chaos_rename_with_tracepoint --test_output=all
```

**2. Look at the output**

**3. Decide:**
- Works? → Merge PR
- Doesn't work? → Debug, fix, re-test

**That's it!**

---

## Time Estimate

**Best case (it works first try):**
- 15 min: Smoke test
- 5 min: Analyze
- 1 min: Decision
- **Total: 21 minutes**

**Worst case (needs debugging):**
- 15 min: Smoke test
- 30 min: Debug
- 15 min: Re-test
- **Total: 60 minutes**

---

## The Critical Question

**Before smoke test:**
> "Does our eBPF code work?"  
> Answer: Unknown (theory only)

**After smoke test:**
> "Does our eBPF code work?"  
> Answer: Yes/No (with data)

**This ONE test closes the gap between theory and reality.**

---

## What You Can Do Right Now

**Option 1: Run the smoke test (recommended)**
```bash
cd /home/jmalicki/src/xibalba
bazel test //vm:chaos_rename_with_tracepoint --test_output=all
```

**Option 2: Just merge without testing (risky)**
- Infrastructure is solid
- Unit tests pass
- Code quality high
- But: unknown if eBPF works

**Option 3: Ask me to run it**
- I can run the test
- Show you output
- We analyze together

---

## My Honest Recommendation

**Just run the damn test! 😄**

We've spent 2 weeks building this.  
15 minutes to verify it works.  
Then we KNOW instead of GUESS.

**Command:**
```bash
bazel test //vm:chaos_rename_with_tracepoint --test_output=all 2>&1 | tee smoke-test-results.txt
```

**Then we have DATA and can make informed decisions!**

---

*Nothing is "left to build" - we need to "actually test" what we built.*

