# Xibalba Testing Guide

**How to use consistency models for effective bug finding**

---

## Quick Reference

| Scenario | Consistency Model | Command | Expected Bugs |
|----------|-------------------|---------|---------------|
| **CI testing (stable kernel)** | Eventual | `bazel run //chaos:simple_chaos_test -- --eventual /tmp/test` | 0 |
| **Local testing (ext4/XFS)** | Weak POSIX | `bazel run //chaos:simple_chaos_test -- --weak /mnt/test` | 0-5 without eBPF |
| **With eBPF delays** | Weak POSIX | `bazel run //chaos:simple_chaos_test -- --weak /mnt/test` | 10-50 with eBPF |
| **Research / find all races** | Strict | `bazel run //chaos:simple_chaos_test -- --strict /mnt/test` | Many |

---

## The Three Consistency Models

### 1. EVENTUAL (`--eventual`) - Most Permissive

**What it checks**:
- ✅ Duplicates (same file twice in ONE scan)
- ❌ Does NOT check missing entries
- ❌ Does NOT check phantom entries

**Use when**:
- CI on stable kernels (GitHub Actions)
- Testing on tmpfs
- Testing distributed filesystems (NFS)

**Example**:
```bash
bazel run //chaos:simple_chaos_test -- --eventual /tmp/test
```

**Expected**: 0 bugs on stable kernel

### 2. WEAK_POSIX (`--weak` or default) - POSIX Compliant

**What it checks**:
- ✅ Duplicates
- ✅ Missing entries (files created BEFORE scan but not read)
- ✅ Phantom entries (files deleted BEFORE scan but still read)
- ❌ Allows files created/deleted DURING scan to appear or not

**Use when**:
- Testing Linux filesystems (ext4, XFS, btrfs)
- Finding real kernel bugs
- Local testing with eBPF delays

**Example**:
```bash
bazel run //chaos:simple_chaos_test -- /tmp/test  # default is --weak
```

**Expected**: 
- Without eBPF: 0-5 bugs (rare races)
- With eBPF: 10-50 bugs (widened race windows)

### 3. STRICT (`--strict`) - Linearizable

**What it checks**:
- ✅ Duplicates
- ✅ Missing entries (ANY file created before read end)
- ✅ Phantom entries (ANY file deleted before read end)
- ✅ Maximum sensitivity

**Use when**:
- Research into ALL possible races
- Testing theoretical correctness
- Finding every possible race condition

**Example**:
```bash
bazel run //chaos:simple_chaos_test -- --strict /mnt/ext4/test
```

**Expected**: Many bugs (includes POSIX-allowed behavior)

---

## Typical Workflows

### CI Testing (Automated)

```yaml
# .github/workflows/ci.yml
- run: bazel-bin/chaos/simple_chaos_test --eventual /tmp/test
```

**Result**: Should always pass (0 bugs on stable kernel)

**If it fails**: Serious kernel bug or validation logic bug

### Daily Development

```bash
# Quick test on your system
mkdir -p /tmp/xibalba_dev
bazel run //chaos:simple_chaos_test -- /tmp/xibalba_dev

# Expected: 0 bugs (stable system)
```

### Testing with eBPF Delays

**Terminal 1** - Start eBPF delay injector:
```bash
bazel build //chaos:pause_controller
sudo ./grant_caps.sh
bazel run //chaos:pause_controller -- 50 500
```

**Terminal 2** - Run test with WEAK model:
```bash
mkdir -p /tmp/xibalba_ebpf
bazel run //chaos:simple_chaos_test -- --weak /tmp/xibalba_ebpf
```

**Expected**: 10-50 bugs found (eBPF widens race windows)

### Testing Custom Kernels in VMs

```bash
# Create VM with your kernel
bazel run //vm:create_vm -- --name test-kernel --kernel /path/to/vmlinuz

# Deploy Xibalba
bazel run //vm:deploy_xibalba -- test-kernel

# Run tests (inside VM or via script)
ssh root@test-kernel "xibalba-test --weak /test/data"
```

**Expected**: Depends on your kernel patches!

---

## Understanding the Output

### Zero Bugs Found

```
=== Results ===
Validation:
  Bugs found: 0
  Status: ✅ NO BUGS DETECTED
```

**Means**:
- Filesystem behaves correctly for chosen consistency model
- No race conditions detected
- Test infrastructure works

**Next step**: Try with eBPF delays or stricter model

### Bugs Found

```
=== Results ===
Validation:
  Bugs found: 47
  Status: 🐛 BUGS DETECTED!

🐛 BUG FOUND (thread 12345):
   Duplicate entries: 2
   Missing entries: 15
   Phantom entries: 3
```

**Means**:
- Race conditions exist in the filesystem
- eBPF delays widened the race windows
- Real bugs that need investigation

**What to do**:
1. Check `xibalba-history.json` for operation timeline
2. Try to reproduce with specific sequence
3. Report to kernel developers (if real kernel bug)

---

## Filesystem-Specific Recommendations

### ext4

```bash
# Use WEAK model (POSIX compliant)
bazel run //chaos:simple_chaos_test -- --weak /mnt/ext4/test

# With eBPF for race detection
bazel run //chaos:pause_controller -- 50 500
```

**Expected bugs**: Medium-High (complex htree structure)

### XFS

```bash
# Use WEAK model
bazel run //chaos:simple_chaos_test -- --weak /mnt/xfs/test
```

**Expected bugs**: Medium (B+ tree, good locking)

### btrfs

```bash
# Use WEAK model
bazel run //chaos:simple_chaos_test -- --weak /mnt/btrfs/test
```

**Expected bugs**: Low-Medium (COW provides natural consistency)

### tmpfs

```bash
# Use EVENTUAL model (simplest filesystem)
bazel run //chaos:simple_chaos_test -- --eventual /tmp/test
```

**Expected bugs**: Very Low (simple in-memory implementation)

### NFS / Network Filesystems

```bash
# Use EVENTUAL model (network delays are normal)
bazel run //chaos:simple_chaos_test -- --eventual /mnt/nfs/test
```

**Expected bugs**: Low (eventual consistency is expected)

---

## Troubleshooting

### "Too many bugs found" on stable kernel

**Problem**: Thousands of phantom/missing entry bugs

**Likely cause**: Using wrong consistency model

**Solution**: Use more permissive model:
```bash
# Instead of --strict or --weak, try:
bazel run //chaos:simple_chaos_test -- --eventual /tmp/test
```

### "No bugs found" even with eBPF

**Problem**: Expected to find bugs but getting 0

**Possible causes**:
1. eBPF not actually injecting delays
   - Check: `bazel run //chaos:pause_controller` shows "Delays injected: N"
   
2. Race windows still too narrow
   - Solution: Increase eBPF delay iterations
   - `bazel run //chaos:pause_controller -- 50 1000`

3. Filesystem is actually bug-free!
   - Try with `--strict` model to be more sensitive

### "Validation logic bug" errors

**Problem**: CI fails with bugs on stable kernel

**Cause**: Bug in our validation logic (not kernel)

**What to do**:
1. Check which consistency model CI is using
2. Verify it's `--eventual` (most permissive)
3. Investigate validation logic in `state_tracker.c`

---

## Performance Notes

**State tracking overhead**:
- Adds ~10-15% overhead (mutex locks, timestamp recording)
- Negligible compared to eBPF delay injection
- Worth it for bug detection!

**Memory usage**:
- MAX_ENTRIES = 10,000 files tracked
- ~2MB per state tracker
- Increase if testing larger directories

---

## See Also

- [FILESYSTEM-CONSISTENCY-MODELS.md](docs/design/FILESYSTEM-CONSISTENCY-MODELS.md) - Detailed consistency explanation
- [STATE-TRACKING-AND-VALIDATION.md](docs/design/STATE-TRACKING-AND-VALIDATION.md) - Implementation details
- [CI-AND-VM-TESTING.md](docs/CI-AND-VM-TESTING.md) - Why VMs need KVM

---

**Summary**: Use the right consistency model for your testing scenario. CI uses `--eventual` (stable kernel, 0 bugs expected). Local testing uses `--weak` (find real races). Research uses `--strict` (find all possible races).

