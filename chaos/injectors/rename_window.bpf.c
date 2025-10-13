/*
 * Xibalba eBPF Injector: Rename Window
 * 
 * Injects delays during rename operations to expose the window where
 * a file is temporarily invisible (removed from old directory but not
 * yet added to new directory).
 * 
 * Targets:
 * - Missing files during rename
 * - Lost rename operations
 * - Directory consistency violations
 * 
 * Based on research: docs/design/BTRFS-LOST-UPDATE-ANALYSIS.md
 */

#include <linux/types.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "GPL";

// ============================================================================
// Configuration Maps
// ============================================================================

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 3);
    __type(key, __u32);
    __type(value, __u32);
} config SEC(".maps");

#define CFG_PROBABILITY 0  // Probability of injecting delay (0-100)
#define CFG_DELAY_US 1     // Delay in microseconds
#define CFG_ENABLED 2      // Enable/disable injection

// Statistics Maps
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 5);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

#define STAT_TOTAL_CALLS 0
#define STAT_DELAYS_INJECTED 1
#define STAT_BTRFS_UNLINK_CALLS 2
#define STAT_VFS_RENAME_CALLS 3
#define STAT_TOTAL_DELAY_NS 4

// ============================================================================
// Helper: Should Inject Delay?
// ============================================================================

static __always_inline bool should_inject(void) {
    __u32 key_enabled = CFG_ENABLED;
    __u32 *enabled = bpf_map_lookup_elem(&config, &key_enabled);
    if (!enabled || *enabled == 0)
        return false;
    
    __u32 key_prob = CFG_PROBABILITY;
    __u32 *probability = bpf_map_lookup_elem(&config, &key_prob);
    if (!probability || *probability == 0)
        return false;
    
    __u32 rand = bpf_get_prandom_u32();
    return (rand % 100) < *probability;
}

static __always_inline void inject_delay(void) {
    __u32 key_delay = CFG_DELAY_US;
    __u32 *delay_us = bpf_map_lookup_elem(&config, &key_delay);
    if (!delay_us || *delay_us == 0)
        return;
    
    // Busy-wait for specified duration
    __u64 start_ns = bpf_ktime_get_ns();
    __u64 end_ns = start_ns + ((__u64)(*delay_us) * 1000);
    
    // Bounded loop to prevent verifier issues
    #pragma unroll
    for (int i = 0; i < 1000; i++) {
        if (bpf_ktime_get_ns() >= end_ns)
            break;
    }
    
    // Update delay stat
    __u32 stat_key = STAT_TOTAL_DELAY_NS;
    __u64 *delay_ns = bpf_map_lookup_elem(&stats, &stat_key);
    if (delay_ns) {
        __u64 actual_delay = bpf_ktime_get_ns() - start_ns;
        __sync_fetch_and_add(delay_ns, actual_delay);
    }
}

// ============================================================================
// Hook 1: Btrfs-Specific __btrfs_unlink_inode (MOST EFFECTIVE)
// ============================================================================

SEC("kretprobe/__btrfs_unlink_inode")
int hook_btrfs_unlink_exit(struct pt_regs *ctx) {
    long ret = PT_REGS_RC(ctx);
    
    // Update call count
    __u32 stat_key = STAT_BTRFS_UNLINK_CALLS;
    __u64 *count = bpf_map_lookup_elem(&stats, &stat_key);
    if (count)
        __sync_fetch_and_add(count, 1);
    
    // Only delay if unlink succeeded
    if (ret != 0)
        return 0;
    
    // This hook fires AFTER file is removed from old directory
    // but BEFORE it's added to new directory (in rename context)
    // Perfect window to make file invisible!
    
    if (!should_inject())
        return 0;
    
    inject_delay();
    
    // Update injection count
    __u32 inj_key = STAT_DELAYS_INJECTED;
    __u64 *inj_count = bpf_map_lookup_elem(&stats, &inj_key);
    if (inj_count)
        __sync_fetch_and_add(inj_count, 1);
    
    return 0;
}

// ============================================================================
// Hook 2: Generic VFS vfs_rename (MODERATE EFFECTIVENESS)
// ============================================================================

SEC("kprobe/vfs_rename")
int hook_vfs_rename_entry(struct pt_regs *ctx) {
    // Update call count
    __u32 stat_key = STAT_VFS_RENAME_CALLS;
    __u64 *count = bpf_map_lookup_elem(&stats, &stat_key);
    if (count)
        __sync_fetch_and_add(count, 1);
    
    // Delay at VFS layer before filesystem-specific rename
    if (!should_inject())
        return 0;
    
    inject_delay();
    
    // Update injection count
    __u32 inj_key = STAT_DELAYS_INJECTED;
    __u64 *inj_count = bpf_map_lookup_elem(&stats, &inj_key);
    if (inj_count)
        __sync_fetch_and_add(inj_count, 1);
    
    return 0;
}

// ============================================================================
// Hook 3: Generic do_unlinkat (FILESYSTEM-AGNOSTIC)
// ============================================================================

SEC("kretprobe/do_unlinkat")
int hook_unlinkat_exit(struct pt_regs *ctx) {
    long ret = PT_REGS_RC(ctx);
    
    // Update total calls
    __u32 stat_key = STAT_TOTAL_CALLS;
    __u64 *count = bpf_map_lookup_elem(&stats, &stat_key);
    if (count)
        __sync_fetch_and_add(count, 1);
    
    // Only delay if unlink succeeded
    if (ret != 0)
        return 0;
    
    // Delay after unlink completes
    // Might be in rename context
    if (!should_inject())
        return 0;
    
    inject_delay();
    
    __u32 inj_key = STAT_DELAYS_INJECTED;
    __u64 *inj_count = bpf_map_lookup_elem(&stats, &inj_key);
    if (inj_count)
        __sync_fetch_and_add(inj_count, 1);
    
    return 0;
}

