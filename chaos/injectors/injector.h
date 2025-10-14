/*
 * Xibalba eBPF Injector Interface
 * 
 * Defines the interface for pluggable eBPF fault injection modules.
 * Each injector targets specific kernel functions to inject delays,
 * errors, or other faults to expose race conditions.
 */

#ifndef XIBALBA_INJECTOR_H
#define XIBALBA_INJECTOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// eBPF injector descriptor
typedef struct {
    const char *name;
    const char *description;
    const char *bpf_object_filename;  // Filename of .bpf.o file (in injectors/)
    
    // Filesystem compatibility
    bool supports_generic;   // Works on any filesystem
    bool supports_ext4;
    bool supports_xfs;
    bool supports_btrfs;
    bool supports_f2fs;
    bool supports_bcachefs;
    
    // Kernel requirements
    struct {
        int min_kernel_version_major;  // e.g., 5 for 5.15
        int min_kernel_version_minor;  // e.g., 15 for 5.15
        bool requires_error_injection;  // Needs CONFIG_BPF_KPROBE_OVERRIDE
        bool requires_fentry;           // Needs fentry/fexit support (5.5+)
    } requirements;
    
    // Configuration
    struct {
        uint32_t default_probability_pct;  // Default delay/error probability (0-100)
        uint32_t default_delay_us;         // Default delay in microseconds
        uint32_t min_delay_us;             // Minimum useful delay
        uint32_t max_delay_us;             // Maximum safe delay
    } config;
    
    // Hook points (for documentation/debugging)
    const char *hook_points[20];  // List of functions hooked
    int num_hooks;
    
    // Target race conditions
    const char *targets[10];  // What races this targets (e.g., "rename visibility")
    int num_targets;
    
    // Effectiveness rating (based on research)
    enum {
        EFFECTIVENESS_UNKNOWN = 0,
        EFFECTIVENESS_LOW = 1,        // 0-10 bugs per 100K ops
        EFFECTIVENESS_MEDIUM = 2,     // 10-100 bugs per 100K ops
        EFFECTIVENESS_HIGH = 3,       // 100-1000 bugs per 100K ops
        EFFECTIVENESS_INEFFECTIVE = 4 // Proven ineffective (legacy)
    } effectiveness;
    
} injector_descriptor_t;

// Built-in injectors (defined in registry.c)
extern const injector_descriptor_t injector_getdents_delay;
extern const injector_descriptor_t injector_rename_window;
extern const injector_descriptor_t injector_transaction_abort;
extern const injector_descriptor_t injector_vfs_delay;
extern const injector_descriptor_t injector_btrfs_specific;
extern const injector_descriptor_t injector_multi_hook;
extern const injector_descriptor_t injector_rename_tracepoint;
extern const injector_descriptor_t injector_link_tracepoint;

// Get injector by name
// Returns NULL if not found
const injector_descriptor_t *get_injector(const char *name);

// List all available injectors
void list_injectors(FILE *out);

// Check if injector is compatible with filesystem
bool injector_supports_filesystem(const injector_descriptor_t *inj, const char *fs_type);

// Check if injector's kernel requirements are met
bool injector_check_requirements(const injector_descriptor_t *inj, char *error_buf, size_t error_len);

#endif /* XIBALBA_INJECTOR_H */

