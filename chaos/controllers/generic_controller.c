/*
 * Xibalba Generic eBPF Controller
 * 
 * Loads and manages eBPF injector programs using descriptors from the registry.
 * Provides runtime configuration and statistics collection.
 */

#include "controller.h"
#include "../injectors/injector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

// Controller handle implementation
struct controller_handle {
    const injector_descriptor_t *injector;
    struct bpf_object *obj;
    struct bpf_link **links;
    int num_links;
    int config_map_fd;
    int stats_map_fd;
    injector_config_t config;
};

// ============================================================================
// Helper Functions
// ============================================================================

__attribute__((format(printf, 2, 0)))
static int libbpf_print_fn(enum libbpf_print_level level, const char *format,
                            va_list args) {
    if (level == LIBBPF_DEBUG) {
        return 0;  // Suppress debug output
    }
    
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wformat-nonliteral"
    int ret = vfprintf(stderr, format, args);
    #pragma clang diagnostic pop
    
    return ret;
}

static int find_map_fd(struct bpf_object *obj, const char *map_name) {
    struct bpf_map *map = bpf_object__find_map_by_name(obj, map_name);
    if (!map) {
        return -1;
    }
    return bpf_map__fd(map);
}

static int configure_map(int map_fd, const injector_config_t *config) {
    __u32 key;
    __u32 value;
    
    // CFG_PROBABILITY = 0
    key = 0;
    value = config->probability_pct;
    if (bpf_map_update_elem(map_fd, &key, &value, BPF_ANY) < 0) {
        return -1;
    }
    
    // CFG_DELAY_US = 1
    key = 1;
    value = config->delay_us;
    if (bpf_map_update_elem(map_fd, &key, &value, BPF_ANY) < 0) {
        return -1;
    }
    
    // CFG_ENABLED = 2 (or 3 for some programs)
    key = 2;
    value = config->enabled ? 1 : 0;
    if (bpf_map_update_elem(map_fd, &key, &value, BPF_ANY) < 0) {
        // Try key 3 (some programs have error_code at index 2)
        key = 3;
        if (bpf_map_update_elem(map_fd, &key, &value, BPF_ANY) < 0) {
            return -1;
        }
    }
    
    return 0;
}

// ============================================================================
// Public API
// ============================================================================

controller_handle_t *controller_load_injector(
    const injector_descriptor_t *injector,
    const injector_config_t *config,
    char *error_buf,
    size_t error_len)
{
    if (!injector || !config) {
        if (error_buf) {
            snprintf(error_buf, error_len, "Invalid arguments");
        }
        return NULL;
    }
    
    // Check requirements
    if (!injector_check_requirements(injector, error_buf, error_len)) {
        return NULL;
    }
    
    // Allocate handle
    controller_handle_t *handle = calloc(1, sizeof(controller_handle_t));
    if (!handle) {
        if (error_buf) {
            snprintf(error_buf, error_len, "Out of memory");
        }
        return NULL;
    }
    
    handle->injector = injector;
    handle->config = *config;
    
    // Setup libbpf callbacks
    libbpf_set_print(libbpf_print_fn);
    
    // Build path to BPF object file
    // For now, assume it's in bazel-bin/chaos/injectors/
    char bpf_path[512];
    snprintf(bpf_path, sizeof(bpf_path), "bazel-bin/chaos/injectors/%s",
             injector->bpf_object_filename);
    
    // Try alternate path (for running from different directories)
    if (access(bpf_path, F_OK) != 0) {
        snprintf(bpf_path, sizeof(bpf_path), "chaos/injectors/%s",
                 injector->bpf_object_filename);
    }
    
    // Load BPF object
    handle->obj = bpf_object__open_file(bpf_path, NULL);
    if (!handle->obj) {
        if (error_buf) {
            snprintf(error_buf, error_len, "Failed to open BPF object: %s (%s)",
                     bpf_path, strerror(errno));
        }
        free(handle);
        return NULL;
    }
    
    // Load BPF program into kernel
    if (bpf_object__load(handle->obj) < 0) {
        if (error_buf) {
            snprintf(error_buf, error_len, "Failed to load BPF program: %s",
                     strerror(errno));
        }
        bpf_object__close(handle->obj);
        free(handle);
        return NULL;
    }
    
    // Find config and stats maps
    handle->config_map_fd = find_map_fd(handle->obj, "config");
    handle->stats_map_fd = find_map_fd(handle->obj, "stats");
    
    // Configure injector (if config map exists)
    if (handle->config_map_fd >= 0) {
        if (configure_map(handle->config_map_fd, config) < 0) {
            if (error_buf) {
                snprintf(error_buf, error_len,
                         "Failed to configure injector: %s",
                         strerror(errno));
            }
            bpf_object__close(handle->obj);
            free(handle);
            return NULL;
        }
    }
    
    // Attach all programs
    struct bpf_program *prog;
    int link_count = 0;
    
    // Count programs first
    bpf_object__for_each_program(prog, handle->obj) {
        link_count++;
    }
    
    handle->links = calloc((size_t)link_count, sizeof(struct bpf_link *));
    if (!handle->links) {
        if (error_buf) {
            snprintf(error_buf, error_len, "Out of memory");
        }
        bpf_object__close(handle->obj);
        free(handle);
        return NULL;
    }
    
    // Attach programs
    int i = 0;
    bpf_object__for_each_program(prog, handle->obj) {
        handle->links[i] = bpf_program__attach(prog);
        if (!handle->links[i]) {
            if (error_buf) {
                snprintf(error_buf, error_len,
                         "Failed to attach program %s: %s",
                         bpf_program__name(prog), strerror(errno));
            }
            // Cleanup already attached links
            for (int j = 0; j < i; j++) {
                bpf_link__destroy(handle->links[j]);
            }
            free(handle->links);
            bpf_object__close(handle->obj);
            free(handle);
            return NULL;
        }
        i++;
    }
    
    handle->num_links = link_count;
    
    return handle;
}

int controller_update_config(controller_handle_t *handle,
                               const injector_config_t *config)
{
    if (!handle || !config || handle->config_map_fd < 0) {
        return -1;
    }
    
    handle->config = *config;
    return configure_map(handle->config_map_fd, config);
}

int controller_get_stats(controller_handle_t *handle, injector_stats_t *stats)
{
    if (!handle || !stats || handle->stats_map_fd < 0) {
        return -1;
    }
    
    // Read stats from BPF map
    // Map format: key -> value
    // 0 -> total_calls
    // 1 -> delays_injected
    // 2 -> errors_injected (if applicable)
    // 3+ -> custom stats
    
    __u32 key;
    __u64 value;
    
    // Total calls
    key = 0;
    if (bpf_map_lookup_elem(handle->stats_map_fd, &key, &value) == 0) {
        stats->total_calls = value;
    }
    
    // Delays injected
    key = 1;
    if (bpf_map_lookup_elem(handle->stats_map_fd, &key, &value) == 0) {
        stats->delays_injected = value;
    }
    
    // Errors injected
    key = 2;
    if (bpf_map_lookup_elem(handle->stats_map_fd, &key, &value) == 0) {
        stats->errors_injected = value;
    }
    
    // Total delay time
    key = 4;  // Often at index 4
    if (bpf_map_lookup_elem(handle->stats_map_fd, &key, &value) == 0) {
        stats->total_delay_ns = value;
    }
    
    return 0;
}

void controller_unload(controller_handle_t *handle)
{
    if (!handle) return;
    
    // Detach all links
    for (int i = 0; i < handle->num_links; i++) {
        if (handle->links[i]) {
            bpf_link__destroy(handle->links[i]);
        }
    }
    
    free(handle->links);
    
    // Close BPF object
    if (handle->obj) {
        bpf_object__close(handle->obj);
    }
    
    free(handle);
}

void controller_print_stats(controller_handle_t *handle, FILE *out)
{
    if (!handle || !out) return;
    
    injector_stats_t stats = {0};
    if (controller_get_stats(handle, &stats) < 0) {
        fprintf(out, "Failed to get statistics\n");
        return;
    }
    
    fprintf(out, "eBPF Injector Statistics (%s):\n", handle->injector->name);
    fprintf(out, "  Total calls:       %lu\n", stats.total_calls);
    fprintf(out, "  Delays injected:   %lu\n", stats.delays_injected);
    fprintf(out, "  Errors injected:   %lu\n", stats.errors_injected);
    fprintf(out, "  Total delay:       %.2f ms\n",
            (double)stats.total_delay_ns / 1000000.0);
    
    if (stats.total_calls > 0) {
        fprintf(out, "  Injection rate:    %.2f%%\n",
                (double)(stats.delays_injected + stats.errors_injected) * 100.0 /
                (double)stats.total_calls);
    }
    
    fprintf(out, "\n");
    fprintf(out, "Configuration:\n");
    fprintf(out, "  Probability:       %u%%\n", handle->config.probability_pct);
    fprintf(out, "  Delay:             %u μs\n", handle->config.delay_us);
    fprintf(out, "  Enabled:           %s\n",
            handle->config.enabled ? "yes" : "no");
}

