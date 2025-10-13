/*
 * Xibalba eBPF Injector: Transaction Abort (Btrfs-Specific)
 * 
 * Forces btrfs transaction aborts by injecting errors at critical points.
 * Tests "dirty read" scenarios where one transaction reads uncommitted
 * state from another transaction that later aborts.
 * 
 * Targets:
 * - Reference count corruption
 * - Lost hard links
 * - Transaction isolation violations
 * 
 * Based on research: docs/design/BTRFS-TRANSACTION-ABORT-RACES.md
 * 
 * REQUIRES: CONFIG_BPF_KPROBE_OVERRIDE=y in kernel
 */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

// ============================================================================
// Configuration Maps
// ============================================================================

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 4);
    __type(key, __u32);
    __type(value, __u32);
} config SEC(".maps");

#define CFG_ERROR_RATE 0      // Probability of injecting error (0-100)
#define CFG_DELAY_RATE 1      // Probability of injecting delay (0-100)
#define CFG_DELAY_US 2        // Delay in microseconds
#define CFG_ENABLED 3         // Enable/disable injection

// Statistics Maps
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 6);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

#define STAT_INODE_REF_CALLS 0
#define STAT_DIR_ITEM_CALLS 1
#define STAT_ERRORS_INJECTED 2
#define STAT_DELAYS_INJECTED 3
#define STAT_TOTAL_DELAY_NS 4
#define STAT_ABORTS_TRIGGERED 5  // Estimated (errors that likely caused aborts)

// ============================================================================
// Helper Functions
// ============================================================================

static __always_inline bool should_inject_error(void) {
    __u32 key_enabled = CFG_ENABLED;
    __u32 *enabled = bpf_map_lookup_elem(&config, &key_enabled);
    if (!enabled || *enabled == 0)
        return false;
    
    __u32 key_rate = CFG_ERROR_RATE;
    __u32 *error_rate = bpf_map_lookup_elem(&config, &key_rate);
    if (!error_rate || *error_rate == 0)
        return false;
    
    __u32 rand = bpf_get_prandom_u32();
    return (rand % 100) < *error_rate;
}

static __always_inline bool should_inject_delay(void) {
    __u32 key_enabled = CFG_ENABLED;
    __u32 *enabled = bpf_map_lookup_elem(&config, &key_enabled);
    if (!enabled || *enabled == 0)
        return false;
    
    __u32 key_rate = CFG_DELAY_RATE;
    __u32 *delay_rate = bpf_map_lookup_elem(&config, &key_rate);
    if (!delay_rate || *delay_rate == 0)
        return false;
    
    __u32 rand = bpf_get_prandom_u32();
    return (rand % 100) < *delay_rate;
}

static __always_inline void inject_delay(void) {
    __u32 key_delay = CFG_DELAY_US;
    __u32 *delay_us = bpf_map_lookup_elem(&config, &key_delay);
    if (!delay_us || *delay_us == 0)
        return;
    
    __u64 start_ns = bpf_ktime_get_ns();
    __u64 end_ns = start_ns + ((__u64)(*delay_us) * 1000);
    
    // Busy-wait (eBPF can't sleep)
    #pragma unroll
    for (int i = 0; i < 1000; i++) {
        if (bpf_ktime_get_ns() >= end_ns)
            break;
    }
    
    // Update delay stats
    __u32 stat_key = STAT_TOTAL_DELAY_NS;
    __u64 *delay_ns = bpf_map_lookup_elem(&stats, &stat_key);
    if (delay_ns) {
        __u64 actual_delay = bpf_ktime_get_ns() - start_ns;
        __sync_fetch_and_add(delay_ns, actual_delay);
    }
}

// ============================================================================
// Hook 1: After btrfs_insert_inode_ref (DELAY TO WIDEN DIRTY READ WINDOW)
// ============================================================================

SEC("kretprobe/btrfs_insert_inode_ref")
int hook_after_inode_ref_insert(struct pt_regs *ctx) {
    long ret = PT_REGS_RC(ctx);
    
    // Update call count
    __u32 stat_key = STAT_INODE_REF_CALLS;
    __u64 *count = bpf_map_lookup_elem(&stats, &stat_key);
    if (count)
        __sync_fetch_and_add(count, 1);
    
    // Only delay if insertion succeeded
    if (ret != 0)
        return 0;
    
    // At this point:
    // - Inode reference has been added (in-memory)
    // - Directory item NOT yet added
    // - Other transactions can SEE the inode ref
    // - If we delay here, other transactions have time to read it
    // - If this transaction later aborts, they have STALE DATA!
    
    if (!should_inject_delay())
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
// Hook 2: Force Error at btrfs_insert_dir_item (TRIGGER ABORTS)
// ============================================================================

SEC("kprobe/btrfs_insert_dir_item")
int inject_dir_item_error(struct pt_regs *ctx) {
    // Update call count
    __u32 stat_key = STAT_DIR_ITEM_CALLS;
    __u64 *count = bpf_map_lookup_elem(&stats, &stat_key);
    if (count)
        __sync_fetch_and_add(count, 1);
    
    // Should we inject error?
    if (!should_inject_error())
        return 0;
    
    // Force function to return -ENOSPC (disk full)
    // This will cause:
    // 1. btrfs_add_link() to fail
    // 2. Cleanup code to run (try to remove inode ref)
    // 3. btrfs_abort_transaction() to be called
    // 
    // Meanwhile, other transactions may have read the inode ref
    // from Hook 1's delay window!
    
    bpf_override_return(ctx, -28);  // -ENOSPC
    
    // Update error injection count
    __u32 err_key = STAT_ERRORS_INJECTED;
    __u64 *err_count = bpf_map_lookup_elem(&stats, &err_key);
    if (err_count)
        __sync_fetch_and_add(err_count, 1);
    
    // Estimate abort count (errors at dir_item likely cause aborts)
    __u32 abort_key = STAT_ABORTS_TRIGGERED;
    __u64 *abort_count = bpf_map_lookup_elem(&stats, &abort_key);
    if (abort_count)
        __sync_fetch_and_add(abort_count, 1);
    
    return 0;
}

