#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * Xibalba Error Injector Controller
 * 
 * Controls eBPF error injection for chaos testing.
 * 
 * This program:
 *   1. Loads the eBPF program
 *   2. Configures error injection probability and error code
 *   3. Monitors how many errors are injected
 * 
 * The eBPF program injects errors (like -EAGAIN) to force retries
 * and expose race conditions - TRUE Jepsen-style chaos!
 */

static volatile bool keep_running = true;

static void signal_handler(int sig) {
    (void)sig;
    keep_running = false;
}

int main(int argc, char *argv[]) {
    struct bpf_object *obj;
    int err;
    
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <error_probability_pct> <error_code>\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Error codes:\n");
        fprintf(stderr, "  11 = EAGAIN  (resource temporarily unavailable - RECOMMENDED)\n");
        fprintf(stderr, "   4 = EINTR   (interrupted system call)\n");
        fprintf(stderr, "   2 = ENOENT  (no such file or directory)\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s 50 11   # 50%% error probability, inject -EAGAIN\n", argv[0]);
        fprintf(stderr, "  %s 30 4    # 30%% probability, inject -EINTR\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "The eBPF program will inject errors to force retries.\n");
        fprintf(stderr, "This expands race windows and makes bugs MUCH more likely!\n");
        return 1;
    }
    
    uint32_t error_prob = (uint32_t)atoi(argv[1]);
    uint32_t error_code = (uint32_t)atoi(argv[2]);
    
    if (error_prob > 100) {
        fprintf(stderr, "ERROR: Error probability must be 0-100%%\n");
        return 1;
    }
    
    if (error_code == 0) {
        fprintf(stderr, "ERROR: Error code must be non-zero (try 11 for EAGAIN)\n");
        return 1;
    }
    
    const char *error_name = "UNKNOWN";
    if (error_code == 11) error_name = "EAGAIN";
    else if (error_code == 4) error_name = "EINTR";
    else if (error_code == 2) error_name = "ENOENT";
    
    printf("=== Xibalba Error Injector ===\n");
    printf("Error probability: %u%%\n", error_prob);
    printf("Error code: -%s (%u)\n", error_name, error_code);
    printf("\n");
    printf("eBPF will inject errors to force syscall retries.\n");
    printf("Expected errors/sec: ~%u (if 40K ops/sec baseline)\n", 
           error_prob * 400);
    printf("\n");
    
    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Load eBPF program
    printf("Loading eBPF program...\n");
    obj = bpf_object__open_file("pause_injector.bpf.o", NULL);
    if (!obj) {
        fprintf(stderr, "ERROR: Failed to open eBPF object file\n");
        fprintf(stderr, "Make sure pause_injector.bpf.o is in current directory\n");
        return 1;
    }
    
    err = bpf_object__load(obj);
    if (err) {
        fprintf(stderr, "ERROR: Failed to load eBPF program: %d\n", err);
        fprintf(stderr, "\n");
        fprintf(stderr, "This program requires CONFIG_BPF_KPROBE_OVERRIDE=y\n");
        fprintf(stderr, "Check kernel config: zgrep CONFIG_BPF_KPROBE_OVERRIDE /proc/config.gz\n");
        fprintf(stderr, "Or check dmesg for more errors\n");
        bpf_object__close(obj);
        return 1;
    }
    
    printf("  ✓ eBPF program loaded\n");
    
    // Attach programs
    printf("Attaching eBPF hooks...\n");
    struct bpf_program *prog;
    struct bpf_link *link = NULL;
    
    bpf_object__for_each_program(prog, obj) {
        link = bpf_program__attach(prog);
        if (link) {
            printf("  ✓ Attached: %s\n", bpf_program__name(prog));
        } else {
            fprintf(stderr, "  ✗ Failed to attach: %s\n", bpf_program__name(prog));
            fprintf(stderr, "\n");
            fprintf(stderr, "Kprobe attachment requires CONFIG_KPROBES=y\n");
            fprintf(stderr, "And may require sudo or CAP_BPF capability\n");
            bpf_object__close(obj);
            return 1;
        }
    }
    
    // Configure error injection
    printf("Setting configuration...\n");
    int config_fd = bpf_object__find_map_fd_by_name(obj, "config");
    if (config_fd < 0) {
        fprintf(stderr, "ERROR: Can't find config map\n");
        bpf_object__close(obj);
        return 1;
    }
    
    // Set error probability
    uint32_t key = 0;  // CFG_ERROR_PROBABILITY
    err = bpf_map_update_elem(config_fd, &key, &error_prob, BPF_ANY);
    if (err) {
        fprintf(stderr, "ERROR: Failed to set error probability: %d\n", err);
        bpf_object__close(obj);
        return 1;
    }
    
    // Set error code
    key = 1;  // CFG_ERROR_CODE
    err = bpf_map_update_elem(config_fd, &key, &error_code, BPF_ANY);
    if (err) {
        fprintf(stderr, "ERROR: Failed to set error code: %d\n", err);
        bpf_object__close(obj);
        return 1;
    }
    
    printf("  ✓ Error probability: %u%%\n", error_prob);
    printf("  ✓ Error code: -%s\n", error_name);
    
    // Initialize statistics
    int stats_fd = bpf_object__find_map_fd_by_name(obj, "stats");
    if (stats_fd >= 0) {
        key = 0;
        uint64_t zero = 0;
        bpf_map_update_elem(stats_fd, &key, &zero, BPF_ANY);
    }
    
    printf("\n");
    printf("🌩️  ERROR INJECTOR ACTIVE!\n");
    printf("   eBPF is now intercepting __x64_sys_getdents64\n");
    printf("   %u%% of calls will return -%s\n", error_prob, error_name);
    printf("   This forces retries and expands race windows!\n");
    printf("\n");
    printf("   Jepsen-style chaos: Operations fail → Retry → Races exposed!\n");
    printf("\n");
    printf("Press Ctrl+C to stop and see statistics\n");
    printf("\n");
    
    // Monitor statistics periodically
    uint64_t last_count = 0;
    while (keep_running) {
        sleep(2);
        
        if (stats_fd >= 0) {
            key = 0;
            uint64_t count = 0;
            if (bpf_map_lookup_elem(stats_fd, &key, &count) == 0) {
                if (count != last_count) {
                    uint64_t delta = count - last_count;
                    printf("Errors injected: %lu (+%lu in last 2s = ~%lu/sec)\n", 
                           count, delta, delta / 2);
                    last_count = count;
                }
            }
        }
    }
    
    printf("\n=== Shutdown ===\n");
    if (stats_fd >= 0) {
        key = 0;
        uint64_t count = 0;
        if (bpf_map_lookup_elem(stats_fd, &key, &count) == 0) {
            printf("Total errors injected: %lu\n", count);
        }
    }
    
    if (link)
        bpf_link__destroy(link);
    bpf_object__close(obj);
    
    return 0;
}
