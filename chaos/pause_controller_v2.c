/*
 * Enhanced eBPF Pause Controller v2
 * 
 * Controls multi-syscall fault injection for finding POSIX violations.
 * 
 * Usage:
 *   pause_controller_v2 \
 *     --getdents-delay 50 \
 *     --create-delay 30 \
 *     --unlink-delay 30 \
 *     --rename-delay 80 \
 *     --iterations 500 \
 *     --max-delay-ns 100000
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <signal.h>
#include <stdbool.h>

// Configuration keys (must match eBPF program)
#define CFG_GETDENTS_DELAY_PCT     0
#define CFG_CREATE_DELAY_PCT       1
#define CFG_UNLINK_DELAY_PCT       2
#define CFG_RENAME_DELAY_PCT       3
#define CFG_DELAY_ITERATIONS       4
#define CFG_MAX_DELAY_NS           5

// Statistics keys
#define STAT_GETDENTS_CALLS        0
#define STAT_GETDENTS_DELAYED      1
#define STAT_CREATE_CALLS          2
#define STAT_CREATE_DELAYED        3
#define STAT_UNLINK_CALLS          4
#define STAT_UNLINK_DELAYED        5
#define STAT_RENAME_CALLS          6
#define STAT_RENAME_DELAYED        7

static volatile bool running = true;

static void sig_handler(int sig) {
    (void)sig;
    running = false;
}

static int set_config(__u32 key, __u64 value, int config_fd) {
    if (bpf_map_update_elem(config_fd, &key, &value, BPF_ANY) < 0) {
        fprintf(stderr, "Failed to set config key %u: %s\n", key, strerror(errno));
        return -1;
    }
    return 0;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [OPTIONS]\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Enhanced multi-syscall eBPF fault injection for POSIX violation testing.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Per-Syscall Delay Configuration:\n");
    fprintf(stderr, "  --getdents-delay PCT    Delay probability for getdents64 (0-100, default: 50)\n");
    fprintf(stderr, "  --create-delay PCT      Delay probability for create (0-100, default: 30)\n");
    fprintf(stderr, "  --unlink-delay PCT      Delay probability for unlink (0-100, default: 30)\n");
    fprintf(stderr, "  --rename-delay PCT      Delay probability for rename (0-100, default: 80)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Delay Amount Configuration:\n");
    fprintf(stderr, "  --iterations N          Busy-wait iterations (default: 500)\n");
    fprintf(stderr, "  --max-delay-ns NS       Maximum delay in nanoseconds (default: 100000 = 100μs)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Presets:\n");
    fprintf(stderr, "  --aggressive            All delays 80%%, iterations 1000 (find ALL races)\n");
    fprintf(stderr, "  --moderate              Balanced delays, iterations 500 (default-ish)\n");
    fprintf(stderr, "  --minimal               Low delays, iterations 200 (subtle races only)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  # Target rename races (highest risk for duplicates):\n");
    fprintf(stderr, "  %s --rename-delay 90 --getdents-delay 50\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "  # Target directory growth (htree/B+tree splits):\n");
    fprintf(stderr, "  %s --create-delay 70 --getdents-delay 60 --iterations 800\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "  # Aggressive: try to find ANY duplicates:\n");
    fprintf(stderr, "  %s --aggressive\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Press Ctrl+C to stop and see statistics.\n");
}

int main(int argc, char *argv[]) {
    // Default configuration
    __u64 getdents_delay = 50;  // 50% probability
    __u64 create_delay = 30;
    __u64 unlink_delay = 30;
    __u64 rename_delay = 80;    // Rename is highest risk!
    __u64 iterations = 500;
    __u64 max_delay_ns = 100000;  // 100 microseconds
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--getdents-delay") == 0 && i + 1 < argc) {
            getdents_delay = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--create-delay") == 0 && i + 1 < argc) {
            create_delay = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--unlink-delay") == 0 && i + 1 < argc) {
            unlink_delay = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--rename-delay") == 0 && i + 1 < argc) {
            rename_delay = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            iterations = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--max-delay-ns") == 0 && i + 1 < argc) {
            max_delay_ns = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--aggressive") == 0) {
            getdents_delay = 80;
            create_delay = 80;
            unlink_delay = 80;
            rename_delay = 90;
            iterations = 1000;
            max_delay_ns = 200000;  // 200μs
        } else if (strcmp(argv[i], "--moderate") == 0) {
            getdents_delay = 50;
            create_delay = 40;
            unlink_delay = 40;
            rename_delay = 70;
            iterations = 500;
            max_delay_ns = 100000;
        } else if (strcmp(argv[i], "--minimal") == 0) {
            getdents_delay = 20;
            create_delay = 10;
            unlink_delay = 10;
            rename_delay = 30;
            iterations = 200;
            max_delay_ns = 50000;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Validate ranges
    if (getdents_delay > 100 || create_delay > 100 || 
        unlink_delay > 100 || rename_delay > 100) {
        fprintf(stderr, "Error: Delay percentages must be 0-100\n");
        return 1;
    }
    
    if (iterations > 1000) {
        fprintf(stderr, "Error: Iterations must be ≤ 1000 (eBPF verifier limit)\n");
        return 1;
    }
    
    // Load eBPF program (Bazel runfiles location)
    const char *bpf_paths[] = {
        "chaos/pause_injector_v2.bpf.o",  // Bazel runfiles
        "pause_injector_v2.bpf.o",         // Local build
        "bin/pause_injector_v2.bpf.o",     // Legacy location
    };
    
    struct bpf_object *obj = NULL;
    for (int i = 0; i < 3; i++) {
        obj = bpf_object__open_file(bpf_paths[i], NULL);
        if (obj) break;
    }
    
    if (!obj) {
        fprintf(stderr, "Failed to open eBPF object: %s\n", strerror(errno));
        fprintf(stderr, "\nMake sure to build first:\n");
        fprintf(stderr, "  bazel build //chaos:pause_controller_v2\n");
        return 1;
    }
    
    if (bpf_object__load(obj) < 0) {
        fprintf(stderr, "Failed to load eBPF object: %s\n", strerror(errno));
        bpf_object__close(obj);
        return 1;
    }
    
    // Get config map FD
    struct bpf_map *config_map = bpf_object__find_map_by_name(obj, "config");
    if (!config_map) {
        fprintf(stderr, "Failed to find config map\n");
        bpf_object__close(obj);
        return 1;
    }
    
    int config_fd = bpf_map__fd(config_map);
    if (config_fd < 0) {
        fprintf(stderr, "Failed to get config map FD\n");
        bpf_object__close(obj);
        return 1;
    }
    
    // Set configuration
    if (set_config(CFG_GETDENTS_DELAY_PCT, getdents_delay, config_fd) < 0 ||
        set_config(CFG_CREATE_DELAY_PCT, create_delay, config_fd) < 0 ||
        set_config(CFG_UNLINK_DELAY_PCT, unlink_delay, config_fd) < 0 ||
        set_config(CFG_RENAME_DELAY_PCT, rename_delay, config_fd) < 0 ||
        set_config(CFG_DELAY_ITERATIONS, iterations, config_fd) < 0 ||
        set_config(CFG_MAX_DELAY_NS, max_delay_ns, config_fd) < 0) {
        bpf_object__close(obj);
        return 1;
    }
    
    // Initialize statistics
    struct bpf_map *stats_map = bpf_object__find_map_by_name(obj, "stats");
    if (stats_map) {
        int stats_fd = bpf_map__fd(stats_map);
        __u64 zero = 0;
        for (__u32 i = 0; i <= STAT_RENAME_DELAYED; i++) {
            bpf_map_update_elem(stats_fd, &i, &zero, BPF_ANY);
        }
    }
    
    // Attach all programs
    struct bpf_link *links[4];
    int num_links = 0;
    
    struct bpf_program *prog;
    bpf_object__for_each_program(prog, obj) {
        links[num_links] = bpf_program__attach(prog);
        if (!links[num_links]) {
            fprintf(stderr, "Failed to attach program %s\n", 
                    bpf_program__name(prog));
            goto cleanup;
        }
        num_links++;
    }
    
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  Enhanced eBPF Fault Injector v2 - Multi-Syscall Delays\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("Configuration:\n");
    printf("  getdents64 delay: %llu%% probability\n", getdents_delay);
    printf("  create delay:     %llu%% probability\n", create_delay);
    printf("  unlink delay:     %llu%% probability\n", unlink_delay);
    printf("  rename delay:     %llu%% probability ⚠️  (highest risk!)\n", rename_delay);
    printf("  Iterations:       %llu (busy-wait loops)\n", iterations);
    double max_delay_us = (double)max_delay_ns / 1000.0;
    printf("  Max delay:        %llu ns (%.1f μs)\n", max_delay_ns, max_delay_us);
    printf("\n");
    printf("Syscalls Hooked:\n");
    printf("  ✓ sys_getdents64 - Directory reading\n");
    printf("  ✓ vfs_create     - File creation\n");
    printf("  ✓ do_unlinkat    - File deletion\n");
    printf("  ✓ vfs_rename     - File rename (high-risk for cursor bugs!)\n");
    printf("\n");
    printf("Goal: Find POSIX violations (duplicates in directory scans)\n");
    printf("\n");
    printf("Press Ctrl+C to stop and see statistics.\n");
    printf("\n");
    
    // Set up signal handler
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    
    // Keep running and periodically show stats
    while (running) {
        sleep(5);
        
        if (!stats_map)
            continue;
        
        int stats_fd = bpf_map__fd(stats_map);
        __u64 stats_vals[8];
        
        for (__u32 i = 0; i < 8; i++) {
            __u64 val = 0;
            bpf_map_lookup_elem(stats_fd, &i, &val);
            stats_vals[i] = val;
        }
        
        printf("\rStats: ");
        double getdents_pct = stats_vals[STAT_GETDENTS_CALLS] > 0 ? 
            100.0 * (double)stats_vals[STAT_GETDENTS_DELAYED] / (double)stats_vals[STAT_GETDENTS_CALLS] : 0;
        printf("getdents: %llu/%llu (%.1f%%)  ", 
               stats_vals[STAT_GETDENTS_DELAYED],
               stats_vals[STAT_GETDENTS_CALLS],
               getdents_pct);
        printf("create: %llu/%llu  ",
               stats_vals[STAT_CREATE_DELAYED],
               stats_vals[STAT_CREATE_CALLS]);
        printf("unlink: %llu/%llu  ",
               stats_vals[STAT_UNLINK_DELAYED],
               stats_vals[STAT_UNLINK_CALLS]);
        printf("rename: %llu/%llu",
               stats_vals[STAT_RENAME_DELAYED],
               stats_vals[STAT_RENAME_CALLS]);
        fflush(stdout);
    }
    
    printf("\n\n");
    printf("Stopping eBPF fault injection...\n");
    
    // Print final statistics
    if (stats_map) {
        int stats_fd = bpf_map__fd(stats_map);
        __u64 stats_vals[8];
        
        for (__u32 i = 0; i < 8; i++) {
            __u64 val = 0;
            bpf_map_lookup_elem(stats_fd, &i, &val);
            stats_vals[i] = val;
        }
        
        printf("\n");
        printf("Final Statistics:\n");
        printf("─────────────────────────────────────────────────────────────\n");
        printf("  Syscall      │   Total Calls │     Delayed │   Delay %%\n");
        printf("─────────────────────────────────────────────────────────────\n");
        double getdents_final_pct = stats_vals[STAT_GETDENTS_CALLS] > 0 ?
            100.0 * (double)stats_vals[STAT_GETDENTS_DELAYED] / (double)stats_vals[STAT_GETDENTS_CALLS] : 0;
        double create_pct = stats_vals[STAT_CREATE_CALLS] > 0 ?
            100.0 * (double)stats_vals[STAT_CREATE_DELAYED] / (double)stats_vals[STAT_CREATE_CALLS] : 0;
        double unlink_pct = stats_vals[STAT_UNLINK_CALLS] > 0 ?
            100.0 * (double)stats_vals[STAT_UNLINK_DELAYED] / (double)stats_vals[STAT_UNLINK_CALLS] : 0;
        double rename_pct = stats_vals[STAT_RENAME_CALLS] > 0 ?
            100.0 * (double)stats_vals[STAT_RENAME_DELAYED] / (double)stats_vals[STAT_RENAME_CALLS] : 0;
        
        printf("  getdents64   │  %12llu │ %11llu │    %5.1f%%\n",
               stats_vals[STAT_GETDENTS_CALLS],
               stats_vals[STAT_GETDENTS_DELAYED],
               getdents_final_pct);
        printf("  create       │  %12llu │ %11llu │    %5.1f%%\n",
               stats_vals[STAT_CREATE_CALLS],
               stats_vals[STAT_CREATE_DELAYED],
               create_pct);
        printf("  unlink       │  %12llu │ %11llu │    %5.1f%%\n",
               stats_vals[STAT_UNLINK_CALLS],
               stats_vals[STAT_UNLINK_DELAYED],
               unlink_pct);
        printf("  rename       │  %12llu │ %11llu │    %5.1f%%\n",
               stats_vals[STAT_RENAME_CALLS],
               stats_vals[STAT_RENAME_DELAYED],
               rename_pct);
        printf("─────────────────────────────────────────────────────────────\n");
        
        __u64 total_delays = stats_vals[STAT_GETDENTS_DELAYED] +
                             stats_vals[STAT_CREATE_DELAYED] +
                             stats_vals[STAT_UNLINK_DELAYED] +
                             stats_vals[STAT_RENAME_DELAYED];
        
        double delay_ms_per_op = (double)max_delay_ns / 1000000.0;
        double total_delay_ms = (double)total_delays * delay_ms_per_op;
        
        printf("  Total delays injected: %llu\n", total_delays);
        printf("  Est. total delay time: %.2f ms\n", total_delay_ms);
    }
    
cleanup:
    // Detach programs
    for (int i = 0; i < num_links; i++) {
        if (links[i]) {
            bpf_link__destroy(links[i]);
        }
    }
    
    bpf_object__close(obj);
    printf("\neBPF programs detached. Fault injection stopped.\n");
    return 0;
}

