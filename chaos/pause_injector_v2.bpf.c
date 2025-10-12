/*
 * Enhanced eBPF Fault Injector v2
 * 
 * Multi-syscall delay injection to find POSIX violations (duplicates)
 * 
 * Targets:
 * - sys_getdents64: Directory reading (current)
 * - vfs_create: File creation (NEW)
 * - do_unlinkat: File deletion (NEW)
 * - vfs_rename: File rename (NEW - highest risk for cursor bugs!)
 * 
 * Each syscall has independent delay configuration for targeted testing.
 */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

// ============================================================================
// Configuration Maps
// ============================================================================

// Per-syscall delay configuration
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, __u32);
    __type(value, __u64);
    __uint(max_entries, 16);
} config SEC(".maps");

// Configuration keys
#define CFG_GETDENTS_DELAY_PCT     0   // Probability for getdents64 (0-100)
#define CFG_CREATE_DELAY_PCT       1   // Probability for create (0-100)
#define CFG_UNLINK_DELAY_PCT       2   // Probability for unlink (0-100)
#define CFG_RENAME_DELAY_PCT       3   // Probability for rename (0-100)
#define CFG_DELAY_ITERATIONS       4   // Busy-wait iterations
#define CFG_MAX_DELAY_NS           5   // Maximum delay in nanoseconds

// Per-syscall statistics
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, __u32);
    __type(value, __u64);
    __uint(max_entries, 16);
} stats SEC(".maps");

// Statistics keys
#define STAT_GETDENTS_CALLS        0
#define STAT_GETDENTS_DELAYED      1
#define STAT_CREATE_CALLS          2
#define STAT_CREATE_DELAYED        3
#define STAT_UNLINK_CALLS          4
#define STAT_UNLINK_DELAYED        5
#define STAT_RENAME_CALLS          6
#define STAT_RENAME_DELAYED        7

// ============================================================================
// Helper Functions
// ============================================================================

static __always_inline int should_delay(__u32 delay_pct_key) {
    __u64 *delay_pct_ptr = bpf_map_lookup_elem(&config, &delay_pct_key);
    if (!delay_pct_ptr)
        return 0;
    
    __u64 delay_pct = *delay_pct_ptr;
    if (delay_pct == 0)
        return 0;
    
    // Simple probabilistic check: use current nanoseconds % 100
    __u64 now = bpf_ktime_get_ns();
    return (now % 100) < delay_pct;
}

static __always_inline void inject_delay(void) {
    __u32 iter_key = CFG_DELAY_ITERATIONS;
    __u32 max_delay_key = CFG_MAX_DELAY_NS;
    
    __u64 *iterations_ptr = bpf_map_lookup_elem(&config, &iter_key);
    __u64 *max_delay_ptr = bpf_map_lookup_elem(&config, &max_delay_key);
    
    if (!iterations_ptr || !max_delay_ptr)
        return;
    
    __u64 delay_iterations = *iterations_ptr;
    __u64 max_delay_ns = *max_delay_ptr;
    
    __u64 start = bpf_ktime_get_ns();
    
    // Bounded busy-wait
    #pragma unroll
    for (int i = 0; i < 1000; i++) {
        if (i >= delay_iterations)
            break;
        
        if ((i % 100) == 0) {
            __u64 now = bpf_ktime_get_ns();
            if ((now - start) > max_delay_ns)
                break;
        }
        
        __sync_fetch_and_add(&start, 0);  // Busy work
    }
}

static __always_inline void increment_stat(__u32 key) {
    __u64 *val = bpf_map_lookup_elem(&stats, &key);
    if (val) {
        __sync_fetch_and_add(val, 1);
    }
}

// ============================================================================
// Syscall Hooks
// ============================================================================

// Hook 1: getdents64 (directory reading)
SEC("kprobe/sys_getdents64")
int trace_getdents64(struct pt_regs *ctx) {
    increment_stat(STAT_GETDENTS_CALLS);
    
    if (should_delay(CFG_GETDENTS_DELAY_PCT)) {
        inject_delay();
        increment_stat(STAT_GETDENTS_DELAYED);
    }
    
    return 0;
}

// Hook 2: vfs_create (file creation)
// This targets the VFS layer, works for all filesystems
SEC("kprobe/vfs_create")
int trace_create(struct pt_regs *ctx) {
    increment_stat(STAT_CREATE_CALLS);
    
    if (should_delay(CFG_CREATE_DELAY_PCT)) {
        inject_delay();
        increment_stat(STAT_CREATE_DELAYED);
    }
    
    return 0;
}

// Hook 3: do_unlinkat (file deletion)
// Catches both unlink() and rmdir()
SEC("kprobe/do_unlinkat")
int trace_unlink(struct pt_regs *ctx) {
    increment_stat(STAT_UNLINK_CALLS);
    
    if (should_delay(CFG_UNLINK_DELAY_PCT)) {
        inject_delay();
        increment_stat(STAT_UNLINK_DELAYED);
    }
    
    return 0;
}

// Hook 4: vfs_rename (file rename)
// HIGH RISK: Renames can confuse directory cursors!
SEC("kprobe/vfs_rename")
int trace_rename(struct pt_regs *ctx) {
    increment_stat(STAT_RENAME_CALLS);
    
    if (should_delay(CFG_RENAME_DELAY_PCT)) {
        inject_delay();
        increment_stat(STAT_RENAME_DELAYED);
    }
    
    return 0;
}

// ============================================================================
// Notes
// ============================================================================
/*
 * Why these hooks?
 * 
 * 1. sys_getdents64: Delays directory READING
 *    - Widens window for concurrent modifications
 *    - Current hook, proven to work
 * 
 * 2. vfs_create: Delays file CREATION
 *    - If delayed during directory scan, creates race
 *    - Might cause htree/B+tree split during scan
 *    - Could confuse directory cursor
 * 
 * 3. do_unlinkat: Delays file DELETION
 *    - Deleting entry might move cursor
 *    - Classic source of "skip" or "duplicate" bugs
 *    - Widens window for cursor invalidation
 * 
 * 4. vfs_rename: Delays RENAME operations
 *    - HIGHEST RISK for duplicates!
 *    - Entry might appear at old AND new location
 *    - Cursor might see same file twice
 *    - This is the most likely to find POSIX bugs
 * 
 * Example Race (with delays):
 * 
 *   Thread A: getdents64() [DELAYED by eBPF]
 *   Thread B: rename("file_X", "file_Y") [ALSO DELAYED by eBPF]
 *   Thread A: Resumes, cursor at position P
 *   Thread B: Completes rename, entry moved
 *   Thread A: Continues scan, sees entry at new position
 *   → Cursor confusion → DUPLICATE!
 * 
 * If we find duplicates with this: Real kernel bug!
 * If we find 0 duplicates: Kernel is rock-solid (good news!)
 */

