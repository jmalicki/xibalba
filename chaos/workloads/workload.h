/*
 * Xibalba Workload Interface
 * 
 * Defines the interface for pluggable workload modules.
 * Each workload implements reader and writer threads that perform
 * different patterns of filesystem operations.
 */

#ifndef XIBALBA_WORKLOAD_H
#define XIBALBA_WORKLOAD_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include "../../common/state_tracker.h"

// Forward declaration
typedef struct workload_state workload_state_t;

// Workload operations vtable
typedef struct {
    const char *name;
    const char *description;
    
    // Initialize workload-specific state
    // Returns 0 on success, -1 on error
    int (*init)(workload_state_t *state, const char *test_dir);
    
    // Reader thread function (validates directory contents)
    // arg: workload_state_t*
    void *(*reader_fn)(void *arg);
    
    // Writer thread function (modifies directory)
    // arg: workload_state_t*
    void *(*writer_fn)(void *arg);
    
    // Cleanup workload-specific state
    void (*cleanup)(workload_state_t *state);
    
    // Get recommended thread counts
    int (*get_default_readers)(void);
    int (*get_default_writers)(void);
    
    // Get workload-specific statistics
    // buf: Output buffer, len: Buffer size
    void (*get_stats)(workload_state_t *state, char *buf, size_t len);
    
} workload_ops_t;

// Bug event for incremental logging
typedef struct {
    uint64_t timestamp_ns;
    uint64_t thread_id;
    uint64_t read_start_ns;
    uint64_t read_end_ns;
    const char *model_name;
    uint64_t total_bugs;
    uint64_t missing;
    uint64_t duplicates;
    uint64_t phantoms;
    int files_read;
} bug_event_t;

// Simple lock-free ring buffer for bug events
#define BUG_QUEUE_SIZE 10000
typedef struct {
    bug_event_t events[BUG_QUEUE_SIZE];
    _Atomic uint64_t write_idx;
    _Atomic uint64_t read_idx;
} bug_queue_t;

// Common state shared across all workloads
struct workload_state {
    const char *test_dir;
    state_tracker_t *tracker;
    consistency_model_t model;
    atomic_bool stop;
    _Atomic uint64_t operations;
    _Atomic uint64_t bugs_found;
    _Atomic uint64_t reads_completed;
    bug_queue_t *bug_queue;
    FILE *scan_export;
    
    // Workload-specific data (opaque to framework)
    void *workload_data;
    
    // Workload operations
    const workload_ops_t *ops;
};

// Built-in workloads (defined in respective .c files)
extern const workload_ops_t workload_create_delete;
extern const workload_ops_t workload_rename;
extern const workload_ops_t workload_hardlink;
extern const workload_ops_t workload_mixed;
extern const workload_ops_t workload_btrfs_torture;

// Get workload by name
const workload_ops_t *get_workload(const char *name);

// List all available workloads
void list_workloads(FILE *out);

#endif /* XIBALBA_WORKLOAD_H */

