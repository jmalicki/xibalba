/*
 * Xibalba eBPF Injector Registry
 * 
 * Central registry of all available eBPF fault injectors.
 */

#include "injector.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// Injector 1: getdents64 Syscall Entry Delay (LEGACY - INEFFECTIVE)
// ============================================================================

const injector_descriptor_t injector_getdents_delay = {
    .name = "getdents_delay",
    .description = "Delay at getdents64 syscall entry (LEGACY - proven ineffective)",
    .bpf_object_filename = "pause_injector.bpf.o",
    
    .supports_generic = true,
    .supports_ext4 = true,
    .supports_xfs = true,
    .supports_btrfs = true,
    .supports_f2fs = true,
    .supports_bcachefs = true,
    
    .requirements = {
        .min_kernel_version_major = 5,
        .min_kernel_version_minor = 4,
        .requires_error_injection = false,
        .requires_fentry = false,
    },
    
    .config = {
        .default_probability_pct = 20,
        .default_delay_us = 10,
        .min_delay_us = 1,
        .max_delay_us = 1000,
    },
    
    .hook_points = {
        "tracepoint/syscalls/sys_enter_getdents64",
    },
    .num_hooks = 1,
    
    .targets = {
        "directory scan duplicates (ineffective)",
    },
    .num_targets = 1,
    
    .effectiveness = EFFECTIVENESS_INEFFECTIVE,
};

// ============================================================================
// Injector 2: Rename Window Delay (HIGH EFFECTIVENESS)
// ============================================================================

const injector_descriptor_t injector_rename_window = {
    .name = "rename_window",
    .description = "Delay during rename operation (file invisible window)",
    .bpf_object_filename = "rename_window.bpf.o",
    
    .supports_generic = false,
    .supports_ext4 = true,   // Has do_unlinkat hook
    .supports_xfs = true,    // Has do_unlinkat hook
    .supports_btrfs = true,  // Has __btrfs_unlink_inode hook
    .supports_f2fs = true,   // Has do_unlinkat hook
    .supports_bcachefs = true,
    
    .requirements = {
        .min_kernel_version_major = 5,
        .min_kernel_version_minor = 10,
        .requires_error_injection = false,
        .requires_fentry = false,
    },
    
    .config = {
        .default_probability_pct = 30,
        .default_delay_us = 15,
        .min_delay_us = 5,
        .max_delay_us = 100,
    },
    
    .hook_points = {
        "kretprobe/__btrfs_unlink_inode",  // Btrfs-specific
        "kretprobe/do_unlinkat",            // Generic Linux
        "kretprobe/vfs_rename",             // VFS layer
    },
    .num_hooks = 3,
    
    .targets = {
        "rename visibility window",
        "file temporarily invisible",
        "lost rename updates",
    },
    .num_targets = 3,
    
    .effectiveness = EFFECTIVENESS_HIGH,
};

// ============================================================================
// Injector 3: Transaction Abort (BTRFS-SPECIFIC, HIGH EFFECTIVENESS)
// ============================================================================

const injector_descriptor_t injector_transaction_abort = {
    .name = "transaction_abort",
    .description = "Force btrfs transaction aborts to test dirty read scenarios",
    .bpf_object_filename = "transaction_abort.bpf.o",
    
    .supports_generic = false,
    .supports_ext4 = false,
    .supports_xfs = false,
    .supports_btrfs = true,  // Btrfs-only!
    .supports_f2fs = false,
    .supports_bcachefs = false,
    
    .requirements = {
        .min_kernel_version_major = 5,
        .min_kernel_version_minor = 15,
        .requires_error_injection = true,  // Needs CONFIG_BPF_KPROBE_OVERRIDE
        .requires_fentry = false,
    },
    
    .config = {
        .default_probability_pct = 20,  // 20% abort rate
        .default_delay_us = 50,   // 50μs to widen dirty read window
        .min_delay_us = 10,
        .max_delay_us = 200,
    },
    
    .hook_points = {
        "kretprobe/btrfs_insert_inode_ref",  // Delay to widen window
        "kprobe/btrfs_insert_dir_item",      // Force -ENOSPC error
    },
    .num_hooks = 2,
    
    .targets = {
        "transaction abort dirty reads",
        "reference count corruption",
        "lost hard links",
    },
    .num_targets = 3,
    
    .effectiveness = EFFECTIVENESS_HIGH,
};

// ============================================================================
// Injector 4: VFS Layer Delays (GENERIC, MEDIUM EFFECTIVENESS)
// ============================================================================

const injector_descriptor_t injector_vfs_delay = {
    .name = "vfs_delay",
    .description = "Delay at VFS layer functions (filesystem-agnostic)",
    .bpf_object_filename = "vfs_delay.bpf.o",
    
    .supports_generic = true,
    .supports_ext4 = true,
    .supports_xfs = true,
    .supports_btrfs = true,
    .supports_f2fs = true,
    .supports_bcachefs = true,
    
    .requirements = {
        .min_kernel_version_major = 5,
        .min_kernel_version_minor = 4,
        .requires_error_injection = false,
        .requires_fentry = false,
    },
    
    .config = {
        .default_probability_pct = 25,
        .default_delay_us = 12,
        .min_delay_us = 5,
        .max_delay_us = 50,
    },
    
    .hook_points = {
        "kprobe/iterate_dir",
        "kprobe/vfs_create",
        "kprobe/vfs_unlink",
        "kprobe/vfs_rename",
        "kprobe/vfs_link",
    },
    .num_hooks = 5,
    
    .targets = {
        "VFS layer races",
        "filesystem-agnostic testing",
    },
    .num_targets = 2,
    
    .effectiveness = EFFECTIVENESS_MEDIUM,
};

// ============================================================================
// Injector 5: Btrfs-Specific Deep Hooks (HIGH EFFECTIVENESS)
// ============================================================================

const injector_descriptor_t injector_btrfs_specific = {
    .name = "btrfs_specific",
    .description = "Btrfs-specific hooks at critical internal functions",
    .bpf_object_filename = "btrfs_specific.bpf.o",
    
    .supports_generic = false,
    .supports_ext4 = false,
    .supports_xfs = false,
    .supports_btrfs = true,
    .supports_f2fs = false,
    .supports_bcachefs = false,
    
    .requirements = {
        .min_kernel_version_major = 5,
        .min_kernel_version_minor = 10,
        .requires_error_injection = false,
        .requires_fentry = false,
    },
    
    .config = {
        .default_probability_pct = 25,
        .default_delay_us = 20,
        .min_delay_us = 5,
        .max_delay_us = 100,
    },
    
    .hook_points = {
        "kprobe/btrfs_readdir",
        "kprobe/btrfs_create",
        "kprobe/btrfs_unlink",
        "kprobe/btrfs_rename",
        "kprobe/btrfs_link",
        "kprobe/btrfs_free_path",
    },
    .num_hooks = 6,
    
    .targets = {
        "btrfs btree path operations",
        "btrfs directory operations",
        "btrfs lock release points",
    },
    .num_targets = 3,
    
    .effectiveness = EFFECTIVENESS_MEDIUM,
};

// ============================================================================
// Injector 6: Multi-Hook Sandwich Attack (EXPERIMENTAL, HIGH OVERHEAD)
// ============================================================================

const injector_descriptor_t injector_multi_hook = {
    .name = "multi_hook",
    .description = "Multiple hooks at all layers (sandwich attack)",
    .bpf_object_filename = "multi_hook.bpf.o",
    
    .supports_generic = true,
    .supports_ext4 = true,
    .supports_xfs = true,
    .supports_btrfs = true,
    .supports_f2fs = true,
    .supports_bcachefs = true,
    
    .requirements = {
        .min_kernel_version_major = 5,
        .min_kernel_version_minor = 10,
        .requires_error_injection = false,
        .requires_fentry = false,
    },
    
    .config = {
        .default_probability_pct = 15,  // Lower rate (many hooks!)
        .default_delay_us = 8,
        .min_delay_us = 2,
        .max_delay_us = 30,
    },
    
    .hook_points = {
        "tracepoint/syscalls/sys_enter_getdents64",
        "kprobe/iterate_dir",
        "kprobe/vfs_create",
        "kprobe/vfs_unlink",
        "kprobe/vfs_rename",
        "kprobe/do_unlinkat",
    },
    .num_hooks = 6,
    
    .targets = {
        "all layers simultaneously",
        "maximum race exposure",
    },
    .num_targets = 2,
    
    .effectiveness = EFFECTIVENESS_MEDIUM,  // High overhead, medium gains
};

// ============================================================================
// Injector 7: Rename Tracepoint (WORKING, MODERATE EFFECTIVENESS)
// ============================================================================

const injector_descriptor_t injector_rename_tracepoint = {
    .name = "rename_tracepoint",
    .description = "Delay at rename/unlink syscall exit (syscall-level, portable)",
    .bpf_object_filename = "rename_tracepoint.bpf.o",
    
    .supports_generic = true,
    .supports_ext4 = true,
    .supports_xfs = true,
    .supports_btrfs = true,
    .supports_f2fs = true,
    .supports_bcachefs = true,
    
    .requirements = {
        .min_kernel_version_major = 5,
        .min_kernel_version_minor = 4,
        .requires_error_injection = false,
        .requires_fentry = false,
    },
    
    .config = {
        .default_probability_pct = 30,
        .default_delay_us = 15,
        .min_delay_us = 5,
        .max_delay_us = 100,
    },
    
    .hook_points = {
        "tracepoint/syscalls/sys_exit_renameat2",
        "tracepoint/syscalls/sys_exit_unlinkat",
    },
    .num_hooks = 2,
    
    .targets = {
        "rename visibility window (syscall level)",
        "file missing after rename",
        "directory scan inconsistency",
    },
    .num_targets = 3,
    
    .effectiveness = EFFECTIVENESS_MEDIUM,
};

// ============================================================================
// Injector 8: Link Tracepoint (WORKING, FOR DIRTY READ TESTING)
// ============================================================================

const injector_descriptor_t injector_link_tracepoint = {
    .name = "link_tracepoint",
    .description = "Delay at link/unlink syscall exit (dirty read testing)",
    .bpf_object_filename = "link_tracepoint.bpf.o",
    
    .supports_generic = true,
    .supports_ext4 = true,
    .supports_xfs = true,
    .supports_btrfs = true,
    .supports_f2fs = true,
    .supports_bcachefs = true,
    
    .requirements = {
        .min_kernel_version_major = 5,
        .min_kernel_version_minor = 4,
        .requires_error_injection = false,
        .requires_fentry = false,
    },
    
    .config = {
        .default_probability_pct = 25,
        .default_delay_us = 20,
        .min_delay_us = 5,
        .max_delay_us = 100,
    },
    
    .hook_points = {
        "tracepoint/syscalls/sys_exit_linkat",
        "tracepoint/syscalls/sys_exit_unlinkat",
    },
    .num_hooks = 2,
    
    .targets = {
        "transaction abort dirty reads",
        "reference count races",
        "hard link consistency",
    },
    .num_targets = 3,
    
    .effectiveness = EFFECTIVENESS_MEDIUM,
};

// ============================================================================
// Registry Functions
// ============================================================================

// All available injectors
static const injector_descriptor_t *all_injectors[] = {
    &injector_getdents_delay,
    &injector_rename_window,
    &injector_transaction_abort,
    &injector_vfs_delay,
    &injector_btrfs_specific,
    &injector_multi_hook,
    &injector_rename_tracepoint,
    &injector_link_tracepoint,
    NULL  // Sentinel
};

const injector_descriptor_t *get_injector(const char *name) {
    if (!name) return NULL;
    
    for (int i = 0; all_injectors[i] != NULL; i++) {
        if (strcmp(all_injectors[i]->name, name) == 0) {
            return all_injectors[i];
        }
    }
    return NULL;
}

void list_injectors(FILE *out) {
    fprintf(out, "Available eBPF Injectors:\n\n");
    
    for (int i = 0; all_injectors[i] != NULL; i++) {
        const injector_descriptor_t *inj = all_injectors[i];
        
        fprintf(out, "  %s\n", inj->name);
        fprintf(out, "    Description: %s\n", inj->description);
        fprintf(out, "    Filesystems: ");
        bool first = true;
        if (inj->supports_generic) { fprintf(out, "all"); first = false; }
        if (inj->supports_ext4) { fprintf(out, "%sext4", first ? "" : ", "); first = false; }
        if (inj->supports_xfs) { fprintf(out, "%sxfs", first ? "" : ", "); first = false; }
        if (inj->supports_btrfs) { fprintf(out, "%sbtrfs", first ? "" : ", "); first = false; }
        if (inj->supports_f2fs) { fprintf(out, "%sf2fs", first ? "" : ", "); first = false; }
        fprintf(out, "\n");
        
        fprintf(out, "    Effectiveness: ");
        switch (inj->effectiveness) {
            case EFFECTIVENESS_INEFFECTIVE:
                fprintf(out, "INEFFECTIVE (proven ineffective)\n");
                break;
            case EFFECTIVENESS_LOW:
                fprintf(out, "Low (0-10 bugs per 100K ops)\n");
                break;
            case EFFECTIVENESS_MEDIUM:
                fprintf(out, "Medium (10-100 bugs per 100K ops)\n");
                break;
            case EFFECTIVENESS_HIGH:
                fprintf(out, "High (100-1000 bugs per 100K ops)\n");
                break;
            default:
                fprintf(out, "Unknown\n");
        }
        
        fprintf(out, "    Targets:\n");
        for (int j = 0; j < inj->num_targets; j++) {
            fprintf(out, "      - %s\n", inj->targets[j]);
        }
        
        if (inj->requirements.requires_error_injection) {
            fprintf(out, "    ⚠️  Requires: CONFIG_BPF_KPROBE_OVERRIDE=y\n");
        }
        
        fprintf(out, "\n");
    }
}

bool injector_supports_filesystem(const injector_descriptor_t *inj, const char *fs_type) {
    if (!inj || !fs_type) return false;
    
    if (inj->supports_generic) return true;
    
    if (strcmp(fs_type, "ext4") == 0) return inj->supports_ext4;
    if (strcmp(fs_type, "xfs") == 0) return inj->supports_xfs;
    if (strcmp(fs_type, "btrfs") == 0) return inj->supports_btrfs;
    if (strcmp(fs_type, "f2fs") == 0) return inj->supports_f2fs;
    if (strcmp(fs_type, "bcachefs") == 0) return inj->supports_bcachefs;
    
    return false;
}

bool injector_check_requirements(const injector_descriptor_t *inj, 
                                 char *error_buf, size_t error_len) {
    (void)error_buf;  // TODO: Use for error reporting
    (void)error_len;  // TODO: Use for error reporting
    
    if (!inj) return false;
    
    // TODO: Check kernel version via uname()
    // TODO: Check if CONFIG_BPF_KPROBE_OVERRIDE is enabled
    // For now, assume requirements are met
    
    return true;
}

