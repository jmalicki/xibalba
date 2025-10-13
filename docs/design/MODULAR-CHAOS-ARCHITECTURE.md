# Modular Chaos Testing Architecture

**Date:** 2025-10-13  
**Branch:** enhance/ebpf-injection  
**Goal:** Refactor chaos testing to support multiple workloads and eBPF injection strategies

---

## Current Architecture (Monolithic)

```
simple_chaos_test.c
  ├── Hardcoded workload (create/delete only)
  ├── Single test_state struct
  └── Expects pause_controller to be running externally

pause_injector.bpf.c
  └── Hardcoded to hook getdents64 syscall entry

pause_controller.c
  └── Loads pause_injector.bpf.c
```

**Problems:**
- ❌ Can't test different workloads (rename, link, symlink)
- ❌ Can't test different eBPF strategies (rename window, transaction abort)
- ❌ Can't target different filesystems (btrfs-specific vs generic)
- ❌ Test and eBPF injector are decoupled (manual coordination)

---

## New Architecture (Modular)

```
chaos/
├── workloads/               # Pluggable workload modules
│   ├── workload.h           # Common interface
│   ├── create_delete.c      # Current: create/delete workload
│   ├── rename.c             # NEW: Rename-heavy workload
│   ├── hardlink.c           # NEW: Hard link workload
│   ├── mixed.c              # NEW: Mixed operations
│   └── btrfs_torture.c      # NEW: Btrfs-specific stress
│
├── injectors/               # Pluggable eBPF injection modules
│   ├── injector.h           # Common interface
│   ├── getdents_delay.bpf.c     # Current: getdents64 syscall delay
│   ├── rename_window.bpf.c      # NEW: Delay at rename window
│   ├── transaction_abort.bpf.c  # NEW: Force transaction aborts
│   ├── vfs_delay.bpf.c          # NEW: VFS layer delays
│   ├── btrfs_specific.bpf.c     # NEW: Btrfs-specific hooks
│   └── multi_hook.bpf.c         # NEW: Multiple hook points
│
├── controllers/             # Userspace eBPF loaders
│   ├── controller.h         # Common interface
│   ├── generic_controller.c # Generic loader (selects injector)
│   └── btrfs_controller.c   # Btrfs-specific loader
│
├── tests/                   # Test runners
│   ├── chaos_test_runner.c  # NEW: Generic test framework
│   ├── simple_chaos_test.c  # KEEP: Original test (now uses workloads)
│   └── btrfs_torture_test.c # NEW: Btrfs-specific test
│
└── BUILD.bazel              # Build all modules
```

---

## Component Specifications

### 1. Workload Interface (`chaos/workloads/workload.h`)

```c
#ifndef XIBALBA_WORKLOAD_H
#define XIBALBA_WORKLOAD_H

#include <stdint.h>
#include <stdbool.h>
#include "../../common/state_tracker.h"

// Forward declaration
typedef struct workload_state workload_state_t;

// Workload operations
typedef struct {
    const char *name;
    const char *description;
    
    // Initialize workload-specific state
    int (*init)(workload_state_t *state, const char *test_dir);
    
    // Reader thread function
    void *(*reader_fn)(void *arg);
    
    // Writer thread function  
    void *(*writer_fn)(void *arg);
    
    // Cleanup workload state
    void (*cleanup)(workload_state_t *state);
    
    // Get recommended thread counts
    int (*get_default_readers)(void);
    int (*get_default_writers)(void);
    
    // Get workload-specific stats
    void (*get_stats)(workload_state_t *state, char *buf, size_t len);
    
} workload_ops_t;

// Common state shared across all workloads
struct workload_state {
    const char *test_dir;
    state_tracker_t *tracker;
    consistency_model_t model;
    atomic_bool stop;
    _Atomic uint64_t operations;
    _Atomic uint64_t bugs_found;
    _Atomic uint64_t reads_completed;
    void *bug_queue;
    FILE *scan_export;
    
    // Workload-specific data
    void *workload_data;
    
    // Workload operations
    const workload_ops_t *ops;
};

// Built-in workloads
extern const workload_ops_t workload_create_delete;
extern const workload_ops_t workload_rename;
extern const workload_ops_t workload_hardlink;
extern const workload_ops_t workload_mixed;
extern const workload_ops_t workload_btrfs_torture;

#endif
```

### 2. eBPF Injector Interface (`chaos/injectors/injector.h`)

```c
#ifndef XIBALBA_INJECTOR_H
#define XIBALBA_INJECTOR_H

#include <stdint.h>

// eBPF injector descriptor
typedef struct {
    const char *name;
    const char *description;
    const char *bpf_object_path;  // Path to .bpf.o file
    
    // Filesystem compatibility
    bool supports_generic;   // Works on any filesystem
    bool supports_ext4;
    bool supports_xfs;
    bool supports_btrfs;
    bool supports_f2fs;
    
    // Configuration
    struct {
        uint32_t default_probability_pct;  // Default delay/error probability
        uint32_t default_delay_us;         // Default delay in microseconds
        bool requires_error_injection;     // Needs CONFIG_BPF_KPROBE_OVERRIDE
    } config;
    
    // Hook points (for documentation/debugging)
    const char *hook_points[10];  // List of functions hooked
    int num_hooks;
    
} injector_descriptor_t;

// Built-in injectors
extern const injector_descriptor_t injector_getdents_delay;
extern const injector_descriptor_t injector_rename_window;
extern const injector_descriptor_t injector_transaction_abort;
extern const injector_descriptor_t injector_vfs_delay;
extern const injector_descriptor_t injector_btrfs_specific;
extern const injector_descriptor_t injector_multi_hook;

// Get injector by name
const injector_descriptor_t *get_injector(const char *name);

// List all available injectors
void list_injectors(void);

#endif
```

### 3. Generic Controller (`chaos/controllers/generic_controller.c`)

```c
// Loads any eBPF injector based on descriptor
int load_injector(const injector_descriptor_t *injector, 
                  uint32_t probability_pct,
                  uint32_t delay_us);

void unload_injector(void);

// Get injector stats
void get_injector_stats(char *buf, size_t len);
```

### 4. Unified Test Runner (`chaos/tests/chaos_test_runner.c`)

```c
int main(int argc, char *argv[]) {
    // Usage: chaos_test_runner --workload <name> --injector <name> [options]
    
    // Parse args
    const char *workload_name = "create_delete";  // default
    const char *injector_name = "none";           // default: no injection
    
    // Load workload
    const workload_ops_t *workload = get_workload(workload_name);
    
    // Load eBPF injector (optional)
    const injector_descriptor_t *injector = NULL;
    if (injector_name && strcmp(injector_name, "none") != 0) {
        injector = get_injector(injector_name);
        load_injector(injector, probability_pct, delay_us);
    }
    
    // Initialize workload
    workload_state_t state;
    workload->init(&state, test_dir);
    
    // Launch threads using workload->reader_fn and workload->writer_fn
    pthread_create(&readers[i], NULL, workload->reader_fn, &state);
    pthread_create(&writers[i], NULL, workload->writer_fn, &state);
    
    // Run test...
    
    // Cleanup
    if (injector) unload_injector();
    workload->cleanup(&state);
}
```

---

## Implementation Plan

### Phase 1: Create Abstractions (Keep Existing Tests Working)

**Step 1.1:** Create workload interface
```bash
# Create workloads directory
mkdir -p chaos/workloads

# Create interface header
touch chaos/workloads/workload.h

# Extract existing workload into create_delete.c
cp chaos/simple_chaos_test.c chaos/workloads/create_delete_impl.c
# (Refactor to match interface)
```

**Step 1.2:** Create injector interface
```bash
# Create injectors directory
mkdir -p chaos/injectors

# Create interface header
touch chaos/injectors/injector.h

# Move existing eBPF programs
mv chaos/pause_injector.bpf.c chaos/injectors/getdents_delay.bpf.c
# (Rename for clarity)
```

**Step 1.3:** Update `simple_chaos_test.c` to use abstraction
```c
// Keep simple_chaos_test as-is for backward compatibility
// But internally, it uses workload_create_delete
```

### Phase 2: Implement New Workloads

**Workload 2: Rename-Heavy** (`chaos/workloads/rename.c`)
```c
static void *rename_writer_thread(void *arg) {
    workload_state_t *state = (workload_state_t *)arg;
    
    while (!atomic_load(&state->stop)) {
        int operation = rand() % 10;
        
        if (operation < 3) {
            // 30%: Create
            create_file(state);
        }
        else if (operation < 6) {
            // 30%: Delete
            delete_file(state);
        }
        else {
            // 40%: RENAME (high rate!)
            rename_file(state);
        }
    }
}

const workload_ops_t workload_rename = {
    .name = "rename",
    .description = "Rename-heavy workload (40% renames)",
    .init = rename_init,
    .reader_fn = common_reader_thread,  // Reuse from create_delete
    .writer_fn = rename_writer_thread,
    .cleanup = common_cleanup,
    .get_default_readers = default_10_readers,
    .get_default_writers = default_3_writers,
};
```

**Workload 3: Hard Link** (`chaos/workloads/hardlink.c`)
```c
static void *link_writer_thread(void *arg) {
    workload_state_t *state = (workload_state_t *)arg;
    
    while (!atomic_load(&state->stop)) {
        int operation = rand() % 10;
        
        if (operation < 3) {
            // 30%: Create base files
            create_file(state);
        }
        else if (operation < 7) {
            // 40%: Create hard links
            create_hard_link(state);
        }
        else {
            // 30%: Unlink
            unlink_file(state);
        }
    }
}

const workload_ops_t workload_hardlink = {
    .name = "hardlink",
    .description = "Hard link stress test (tests ref count races)",
    .init = hardlink_init,
    .reader_fn = hardlink_reader_thread,  // Validates ref counts
    .writer_fn = link_writer_thread,
    .cleanup = hardlink_cleanup,
};
```

**Workload 4: Btrfs Torture** (`chaos/workloads/btrfs_torture.c`)
```c
// Btrfs-specific workload targeting known race windows
static void *btrfs_torture_writer(void *arg) {
    while (!stop) {
        // Specifically designed to hit btrfs race windows:
        // - Concurrent operations on same inode
        // - Rename across subvolumes
        // - Link/unlink during snapshot creation
        // - Heavy metadata operations
    }
}
```

### Phase 3: Implement New eBPF Injectors

**Injector 2: Rename Window** (`chaos/injectors/rename_window.bpf.c`)
```c
SEC("kretprobe/__btrfs_unlink_inode")
int inject_rename_delay(struct pt_regs *ctx) {
    // Delay after unlink, before add_link in rename
    if (should_inject_delay()) {
        bpf_busy_wait(15000);  // 15μs
    }
    return 0;
}

SEC("kretprobe/do_unlinkat")  // Generic Linux unlink
int inject_unlink_delay(struct pt_regs *ctx) {
    if (should_inject_delay()) {
        bpf_busy_wait(10000);  // 10μs
    }
    return 0;
}
```

**Injector 3: Transaction Abort** (`chaos/injectors/transaction_abort.bpf.c`)
```c
// Delay to widen dirty read window
SEC("kretprobe/btrfs_insert_inode_ref")
int delay_after_inode_ref(struct pt_regs *ctx) {
    int ret = PT_REGS_RC(ctx);
    if (ret == 0) {
        bpf_busy_wait(50000);  // 50μs - long delay for dirty reads
    }
    return 0;
}

// Force errors to trigger aborts
SEC("kprobe/btrfs_insert_dir_item")
int inject_enospc_error(struct pt_regs *ctx) {
    if (rand() % 100 < 20) {  // 20% error rate
        bpf_override_return(ctx, -28);  // -ENOSPC
    }
    return 0;
}
```

**Injector 4: VFS Layer** (`chaos/injectors/vfs_delay.bpf.c`)
```c
// Generic VFS layer delays (works for all filesystems)
SEC("kprobe/iterate_dir")
int inject_vfs_iterate_delay(struct pt_regs *ctx) {
    if (should_inject_delay()) {
        bpf_busy_wait(10000);
    }
    return 0;
}

SEC("kprobe/vfs_create")
int inject_vfs_create_delay(struct pt_regs *ctx) {
    if (should_inject_delay()) {
        bpf_busy_wait(8000);
    }
    return 0;
}

SEC("kprobe/vfs_rename")
int inject_vfs_rename_delay(struct pt_regs *ctx) {
    if (should_inject_delay()) {
        bpf_busy_wait(12000);
    }
    return 0;
}
```

**Injector Registry** (`chaos/injectors/registry.c`)
```c
const injector_descriptor_t injector_getdents_delay = {
    .name = "getdents_delay",
    .description = "Delay at getdents64 syscall entry (INEFFECTIVE - legacy)",
    .bpf_object_path = "chaos/injectors/getdents_delay.bpf.o",
    .supports_generic = true,
    .config = {
        .default_probability_pct = 20,
        .default_delay_us = 10,
        .requires_error_injection = false,
    },
    .hook_points = {"tracepoint/syscalls/sys_enter_getdents64"},
    .num_hooks = 1,
};

const injector_descriptor_t injector_rename_window = {
    .name = "rename_window",
    .description = "Delay during rename operation (file invisible window)",
    .bpf_object_path = "chaos/injectors/rename_window.bpf.o",
    .supports_btrfs = true,
    .supports_ext4 = true,  // Has do_unlinkat hook too
    .config = {
        .default_probability_pct = 30,
        .default_delay_us = 15,
        .requires_error_injection = false,
    },
    .hook_points = {
        "kretprobe/__btrfs_unlink_inode",
        "kretprobe/do_unlinkat",
    },
    .num_hooks = 2,
};

const injector_descriptor_t injector_transaction_abort = {
    .name = "transaction_abort",
    .description = "Force btrfs transaction aborts (dirty read testing)",
    .bpf_object_path = "chaos/injectors/transaction_abort.bpf.o",
    .supports_btrfs = true,
    .config = {
        .default_probability_pct = 20,  // 20% abort rate
        .default_delay_us = 50,  // 50μs delay to widen dirty read window
        .requires_error_injection = true,  // Needs CONFIG_BPF_KPROBE_OVERRIDE
    },
    .hook_points = {
        "kretprobe/btrfs_insert_inode_ref",
        "kprobe/btrfs_insert_dir_item",
    },
    .num_hooks = 2,
};

// Registry
const injector_descriptor_t *all_injectors[] = {
    &injector_getdents_delay,
    &injector_rename_window,
    &injector_transaction_abort,
    &injector_vfs_delay,
    &injector_btrfs_specific,
    &injector_multi_hook,
    NULL
};

const injector_descriptor_t *get_injector(const char *name) {
    for (int i = 0; all_injectors[i] != NULL; i++) {
        if (strcmp(all_injectors[i]->name, name) == 0) {
            return all_injectors[i];
        }
    }
    return NULL;
}
```

---

## Usage Examples

### Example 1: Original Test (Backward Compatible)

```bash
# Run original create/delete workload with getdents delay (legacy)
bazel run //chaos:simple_chaos_test -- --posix /tmp/test

# Manually start eBPF injector (old way)
sudo bazel run //chaos:pause_controller -- 20 100
```

### Example 2: Rename Workload with Rename Window Injection

```bash
# New unified interface
bazel run //chaos:chaos_test_runner -- \
    --workload rename \
    --injector rename_window \
    --filesystem btrfs \
    --model posix \
    --duration 300 \
    /tmp/test

# This will:
# 1. Load rename_window.bpf.o (hooks __btrfs_unlink_inode)
# 2. Run rename-heavy workload (40% renames)
# 3. Validate for missing/duplicate files
# 4. Report bugs
```

### Example 3: Transaction Abort Testing

```bash
# Test transaction abort dirty reads
bazel run //chaos:chaos_test_runner -- \
    --workload hardlink \
    --injector transaction_abort \
    --filesystem btrfs \
    --error-rate 20 \
    --delay 50 \
    /tmp/btrfs_test

# This will:
# 1. Load transaction_abort.bpf.o (forces ENOSPC errors)
# 2. Run hardlink workload (tests ref count races)
# 3. Validate reference counts
# 4. Report ref count corruption bugs
```

### Example 4: Multi-Filesystem Comparison

```bash
# Test same workload on different filesystems
for fs in ext4 xfs btrfs; do
    mkfs.$fs /dev/loop0
    mount /dev/loop0 /mnt/$fs
    
    bazel run //chaos:chaos_test_runner -- \
        --workload mixed \
        --injector vfs_delay \
        --filesystem $fs \
        /mnt/$fs/test
    
    # Compare bug rates across filesystems
done
```

### Example 5: Injector Comparison

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

# Analyze which injector finds most bugs
tools/compare_injector_results.sh results_*.json
```

---

## Migration Path

### Step 1: Create Abstractions (Don't Break Existing)

- ✅ Create `workloads/` directory
- ✅ Create `injectors/` directory  
- ✅ Define interfaces
- ✅ Keep `simple_chaos_test.c` as-is (backward compatibility)

### Step 2: Refactor Existing Code

- ✅ Extract create/delete logic into `workloads/create_delete.c`
- ✅ Rename `pause_injector.bpf.c` → `injectors/getdents_delay.bpf.c`
- ✅ Create generic controller
- ✅ Update BUILD.bazel

### Step 3: Implement New Modules

- ✅ Implement `workloads/rename.c`
- ✅ Implement `workloads/hardlink.c`
- ✅ Implement `injectors/rename_window.bpf.c`
- ✅ Implement `injectors/transaction_abort.bpf.c`

### Step 4: Create Unified Runner

- ✅ Implement `tests/chaos_test_runner.c`
- ✅ Support `--workload` and `--injector` flags
- ✅ Auto-load eBPF injector (no manual coordination!)

---

## Directory Structure After Refactoring

```
chaos/
├── BUILD.bazel                          # Build all targets
├── README.md                            # Updated with new architecture
│
├── workloads/                           # Workload modules
│   ├── workload.h                       # Interface definition
│   ├── create_delete.c                  # Original workload
│   ├── rename.c                         # NEW: Rename-heavy
│   ├── hardlink.c                       # NEW: Hard links
│   ├── mixed.c                          # NEW: All operations
│   ├── btrfs_torture.c                  # NEW: Btrfs-specific
│   └── BUILD.bazel                      # Workload library
│
├── injectors/                           # eBPF injection modules
│   ├── injector.h                       # Interface definition
│   ├── getdents_delay.bpf.c             # Original (renamed)
│   ├── rename_window.bpf.c              # NEW: Rename window
│   ├── transaction_abort.bpf.c          # NEW: Force aborts
│   ├── vfs_delay.bpf.c                  # NEW: VFS layer
│   ├── btrfs_specific.bpf.c             # NEW: Btrfs hooks
│   ├── multi_hook.bpf.c                 # NEW: Multiple hooks
│   ├── registry.c                       # Injector registry
│   └── BUILD.bazel                      # BPF compilation rules
│
├── controllers/                         # eBPF loaders
│   ├── controller.h                     # Interface
│   ├── generic_controller.c             # Generic loader
│   ├── btrfs_controller.c               # Btrfs-specific
│   └── BUILD.bazel                      # Controller binaries
│
├── tests/                               # Test runners
│   ├── chaos_test_runner.c              # NEW: Unified runner
│   ├── simple_chaos_test.c              # KEEP: Backward compatible
│   ├── btrfs_torture_test.c             # NEW: Btrfs-specific
│   └── BUILD.bazel                      # Test binaries
│
└── common/                              # Shared utilities
    ├── thread_helpers.c                 # NEW: Common thread patterns
    └── injector_loader.c                # NEW: eBPF loading utilities
```

---

## Benefits

### 1. Modularity
- ✅ Easy to add new workloads
- ✅ Easy to add new eBPF injectors
- ✅ Mix and match workload + injector

### 2. Testability
- ✅ Compare injectors (which finds most bugs?)
- ✅ Compare workloads (which exercises best?)
- ✅ Compare filesystems (btrfs vs ext4 vs XFS)

### 3. Maintainability
- ✅ Clear separation of concerns
- ✅ Easier to understand (one module = one concept)
- ✅ Easier to debug (isolate issues to specific module)

### 4. Extensibility
- ✅ Add new filesystem support (F2FS, bcachefs)
- ✅ Add new injection strategies (LSM hooks, lock-level)
- ✅ Add new workload patterns (symlinks, xattrs)

### 5. Research Value
- ✅ Systematic comparison of approaches
- ✅ Data-driven selection of best injector
- ✅ Publishable methodology

---

## Implementation Checklist

### Phase 1: Abstractions (Week 1)
- [ ] Create `chaos/workloads/workload.h`
- [ ] Create `chaos/injectors/injector.h`
- [ ] Create `chaos/controllers/controller.h`
- [ ] Extract `create_delete` workload
- [ ] Verify `simple_chaos_test.c` still works (backward compat)

### Phase 2: New Workloads (Week 2)
- [ ] Implement `workloads/rename.c`
- [ ] Implement `workloads/hardlink.c`
- [ ] Implement `workloads/mixed.c`
- [ ] Test each workload standalone

### Phase 3: New Injectors (Week 3)
- [ ] Implement `injectors/rename_window.bpf.c`
- [ ] Implement `injectors/transaction_abort.bpf.c`
- [ ] Implement `injectors/vfs_delay.bpf.c`
- [ ] Test each injector with simple workload

### Phase 4: Unified Runner (Week 4)
- [ ] Implement `tests/chaos_test_runner.c`
- [ ] Support `--workload` and `--injector` flags
- [ ] Auto-load eBPF injectors
- [ ] Test all combinations

### Phase 5: Validation (Week 5)
- [ ] Run experiments: workload × injector matrix
- [ ] Measure bug rates for each combination
- [ ] Document results
- [ ] Select best approaches

---

## Testing Matrix

| Workload | Injector | FS | Expected Bugs | Status |
|----------|----------|----|--------------| -------|
| create_delete | getdents_delay | all | 0-5 | ❌ Legacy |
| create_delete | rename_window | btrfs | 10-50 | ⚠️ Partial |
| rename | rename_window | btrfs | 50-200 | ✅ **Recommended** |
| rename | transaction_abort | btrfs | 100-500 | ✅ **Recommended** |
| hardlink | transaction_abort | btrfs | 10-100 | ✅ **Recommended** |
| mixed | vfs_delay | all | 20-100 | ⚠️ Generic |
| btrfs_torture | btrfs_specific | btrfs | 100-1000 | 🔬 Research |

---

*This architecture supports systematic exploration of the bug space!*

