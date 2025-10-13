/*
 * Xibalba eBPF Controller Interface
 * 
 * Userspace controller for loading and managing eBPF programs.
 * Handles loading .bpf.o files, configuring maps, and collecting statistics.
 */

#ifndef XIBALBA_CONTROLLER_H
#define XIBALBA_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include "../injectors/injector.h"

// Controller handle (opaque)
typedef struct controller_handle controller_handle_t;

// Configuration for injector
typedef struct {
    uint32_t probability_pct;  // 0-100: Probability of injecting fault
    uint32_t delay_us;         // Microseconds to delay (if applicable)
    uint32_t error_code;       // Error code to inject (if applicable, e.g., -ENOSPC)
    bool enabled;              // Enable/disable injection at runtime
} injector_config_t;

// Statistics from injector
typedef struct {
    uint64_t total_calls;      // Total times hook was called
    uint64_t delays_injected;  // Times delay was injected
    uint64_t errors_injected;  // Times error was injected
    uint64_t total_delay_ns;   // Cumulative delay time
} injector_stats_t;

// Load eBPF injector
// Returns handle on success, NULL on error
controller_handle_t *controller_load_injector(
    const injector_descriptor_t *injector,
    const injector_config_t *config,
    char *error_buf,
    size_t error_len
);

// Update injector configuration at runtime
int controller_update_config(
    controller_handle_t *handle,
    const injector_config_t *config
);

// Get injector statistics
int controller_get_stats(
    controller_handle_t *handle,
    injector_stats_t *stats
);

// Unload eBPF injector
void controller_unload(controller_handle_t *handle);

// Print injector stats to file
void controller_print_stats(
    controller_handle_t *handle,
    FILE *out
);

#endif /* XIBALBA_CONTROLLER_H */

