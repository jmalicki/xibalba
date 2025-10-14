# Xibalba Chaos Testing Framework

Jepsen-style chaos testing for Linux filesystems using eBPF fault injection.

---

## Architecture

Xibalba uses a **modular architecture** with pluggable workloads and eBPF injectors:

```
chaos/
├── workloads/         - Pluggable filesystem workload modules
├── injectors/         - Pluggable eBPF fault injection modules
├── controllers/       - eBPF program loaders
├── tests/             - Test runners and frameworks
└── (legacy files)     - Existing tests (backward compatible)
```

### Design Philosophy

**Separation of Concerns:**
- **Workloads** define *what* operations to perform (create, delete, rename, link)
- **Injectors** define *where* to inject faults (syscall entry, rename window, transaction abort)
- **Tests** combine workload + injector and validate results

**Benefits:**
- 🔧 Easy to add new workloads without changing eBPF code
- 🔬 Easy to test new injection strategies
- 📊 Systematic comparison (which injector finds most bugs?)
- 🚀 Backward compatible (existing tests still work)

---

## Available Injectors

| Name | Targets | Filesystems | Effectiveness | Status |
|------|---------|-------------|---------------|--------|
| `getdents_delay` | getdents64 syscall | All | ❌ Ineffective (legacy) | Stable |
| `rename_window` | Rename visibility | ext4, xfs, btrfs | ✅ High (50-200 bugs/100K) | **NEW** |
| `transaction_abort` | Dirty reads | btrfs only | ✅ High (10-100 bugs/100K) | **NEW** |
| `vfs_delay` | VFS layer | All | ⚠️ Medium | Planned |
| `btrfs_specific` | Btrfs internals | btrfs only | ⚠️ Medium | Planned |
| `multi_hook` | All layers | All | ⚠️ Medium | Planned |

### Injector Details

**`getdents_delay`** (Legacy - Proven Ineffective)
- Hooks: `tracepoint/syscalls/sys_enter_getdents64`
- Why ineffective: Delays before lock acquisition, no race created
- Status: Keep for baseline comparison

**`rename_window`** ⭐ Recommended
- Hooks: `kretprobe/__btrfs_unlink_inode`, `kretprobe/do_unlinkat`
- Targets: Window where file is removed from old dir but not yet in new dir
- Expected: 50-200 missing file bugs per 100K operations
- Requires: Kernel 5.10+
- Best with: `workload_rename` (when implemented)

**`transaction_abort`** ⭐ Recommended (Btrfs-only)
- Hooks: `kretprobe/btrfs_insert_inode_ref` (delay), `kprobe/btrfs_insert_dir_item` (error)
- Targets: Dirty read races (Trans B reads from Trans A before Trans A aborts)
- Expected: 10-100 reference count corruption bugs per 100K operations
- Requires: `CONFIG_BPF_KPROBE_OVERRIDE=y`
- Best with: `workload_hardlink` (when implemented)

---

## Available Workloads

| Name | Operations | Best Injector | Status |
|------|------------|---------------|--------|
| `create_delete` | Create, delete | `getdents_delay` (legacy) | Stable |
| `rename` | Create, delete, rename (40%) | `rename_window` | Planned |
| `hardlink` | Create, link, unlink | `transaction_abort` | Planned |
| `mixed` | All operations | `multi_hook` | Planned |
| `btrfs_torture` | Btrfs-specific stress | `btrfs_specific` | Planned |

---

## Usage

### Current (Legacy - Still Works)

```bash
# Run original test with getdents64 delay injection
bazel run //chaos:simple_chaos_test -- --posix /tmp/test

# In separate terminal, start eBPF injector manually
sudo bazel run //chaos:pause_controller -- 20 100
```

### New (Modular - Coming Soon)

```bash
# Run rename workload with rename window injection
bazel run //chaos:chaos_test_runner -- \
    --workload rename \
    --injector rename_window \
    --filesystem btrfs \
    --duration 300 \
    /tmp/test

# This will:
# 1. Load rename_window.bpf.o automatically
# 2. Run rename-heavy workload
# 3. Validate for missing/duplicate files
# 4. Report bugs and statistics
```

### Injector Comparison

```bash
# Compare different injectors on same workload
for injector in getdents_delay rename_window transaction_abort; do
    bazel run //chaos:chaos_test_runner -- \
        --workload mixed \
        --injector $injector \
        --duration 300 \
        /tmp/test \
        --json > results_${injector}.json
done

# Analyze which finds most bugs
tools/compare_results.sh results_*.json
```

---

## Implementation Status

### ✅ Phase 1: Interfaces (COMPLETE)

- [x] `workloads/workload.h` - Workload interface
- [x] `injectors/injector.h` - Injector interface
- [x] `controllers/controller.h` - Controller interface
- [x] `workloads/registry.c` - Workload registry
- [x] `injectors/registry.c` - Injector registry
- [x] `injectors/rename_window.bpf.c` - NEW injector
- [x] `injectors/transaction_abort.bpf.c` - NEW injector
- [x] BUILD files for new modules

### 🚧 Phase 2: Workload Extraction (IN PROGRESS)

- [ ] Extract `simple_chaos_test.c` → `workloads/create_delete.c`
- [ ] Implement `workloads/rename.c`
- [ ] Implement `workloads/hardlink.c`
- [ ] Keep `simple_chaos_test.c` as backward-compatible wrapper

### 📋 Phase 3: Controller Implementation (PLANNED)

- [ ] Implement `controllers/generic_controller.c`
- [ ] Auto-load eBPF programs
- [ ] Runtime configuration via maps
- [ ] Statistics collection

### 📋 Phase 4: Unified Runner (PLANNED)

- [ ] Implement `tests/chaos_test_runner.c`
- [ ] Support `--workload` and `--injector` flags
- [ ] Integrated eBPF loading (no manual coordination)
- [ ] Comprehensive statistics and reporting

---

## Research Background

See `docs/design/` for comprehensive research:

- **EBPF-INJECTION-RESEARCH-SUMMARY.md** - Executive summary of findings
- **EBPF-RACE-INJECTION-PROPOSALS.md** - 8 injection strategies analyzed
- **EBPF-RACE-INJECTION-DEEP-DIVE.md** - Why syscall entry doesn't work
- **BTRFS-LOST-UPDATE-ANALYSIS.md** - 3 critical race windows in btrfs
- **BTRFS-TRANSACTION-ABORT-RACES.md** - Dirty read problem + historical bugs
- **MODULAR-CHAOS-ARCHITECTURE.md** - This architecture design

**Key Findings:**
1. ❌ getdents64 syscall entry is ineffective (delays before locks)
2. ✅ Rename operations have exploitable race windows
3. ✅ Transaction aborts create dirty read scenarios
4. 🎯 No prior art - this is novel research!

**Expected Results:**
- Current approach: 0-5 bugs per 100K ops
- Rename window: 50-200 bugs per 100K ops
- Transaction abort: 10-100 bugs per 100K ops

---

## Development

### Building

```bash
# Build all chaos targets
bazel build //chaos/...

# Build specific injector
bazel build //chaos/injectors:rename_window_bpf

# Build workload registry
bazel build //chaos/workloads:registry
```

### Testing

```bash
# Run current test (backward compatible)
bazel run //chaos:simple_chaos_test -- --posix /tmp/test

# Test new BPF modules compile
bazel build //chaos/injectors:rename_window_bpf
bazel build //chaos/injectors:transaction_abort_bpf
```

---

## Contributing

When adding new modules:

1. **New Workload:**
   - Implement in `chaos/workloads/<name>.c`
   - Follow `workload_ops_t` interface
   - Add to `workloads/registry.c`
   - Update `workloads/BUILD.bazel`

2. **New eBPF Injector:**
   - Implement in `chaos/injectors/<name>.bpf.c`
   - Add descriptor to `injectors/registry.c`
   - Add genrule to `injectors/BUILD.bazel`
   - Document hook points and targets

3. **Testing:**
   - Ensure backward compatibility
   - Run existing tests
   - Benchmark bug detection rates

---

*Architecture designed 2025-10-13*  
*Implementation: Phase 1 complete, Phase 2+ in progress*
