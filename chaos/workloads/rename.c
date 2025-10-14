/*
 * Rename Workload
 * 
 * Rename-heavy workload designed to expose race conditions during rename operations.
 * - 30% Create operations
 * - 30% Delete operations
 * - 40% Rename operations
 * 
 * Targets:
 * - File visibility during rename (missing files)
 * - Lost rename operations
 * - Duplicate files (visible at both locations)
 * 
 * Best used with: rename_window eBPF injector
 */

#include "workload.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include "../../common/dir_reader.h"

#define MAX_FILES_PER_WRITER 100

// Workload-specific data
typedef struct {
    int files_created;
    int files_deleted;
    int files_renamed;
    int rename_failures;
} rename_data_t;

// ============================================================================
// Writer Thread (Rename-Heavy)
// ============================================================================

static void *rename_writer(void *arg) {
    workload_state_t *state = (workload_state_t *)arg;
    rename_data_t *data = (rename_data_t *)state->workload_data;
    uint64_t thread_id = (uint64_t)pthread_self();
    char filepath[512], new_filepath[512];
    int file_num = 0;
    
    while (!atomic_load(&state->stop)) {
        int operation = rand() % 10;
        
        if (operation < 3) {
            // 30%: Create a file
            snprintf(filepath, sizeof(filepath), "%s/rnw_%lu_f%d",
                     state->test_dir, thread_id, file_num);
            
            int fd = open(filepath, O_CREAT | O_WRONLY, 0644);
            if (fd >= 0) {
                close(fd);
                
                char filename[256];
                snprintf(filename, sizeof(filename), "rnw_%lu_f%d",
                         thread_id, file_num);
                tracker_record_create(state->tracker, filename);
                
                atomic_fetch_add(&state->operations, 1);
                __sync_fetch_and_add(&data->files_created, 1);
                
                file_num++;
                if (file_num >= MAX_FILES_PER_WRITER) file_num = 0;
            }
        }
        else if (operation < 6) {
            // 30%: Delete a file
            if (file_num > 0) {
                int target = rand() % file_num;
                snprintf(filepath, sizeof(filepath), "%s/rnw_%lu_f%d",
                         state->test_dir, thread_id, target);
                
                if (unlink(filepath) == 0) {
                    char filename[256];
                    snprintf(filename, sizeof(filename), "rnw_%lu_f%d",
                             thread_id, target);
                    tracker_record_delete(state->tracker, filename);
                    
                    atomic_fetch_add(&state->operations, 1);
                    __sync_fetch_and_add(&data->files_deleted, 1);
                }
            }
        }
        else {
            // 40%: RENAME a file (HIGH RATE!)
            if (file_num > 0) {
                int source = rand() % file_num;
                int dest = rand() % MAX_FILES_PER_WRITER;
                
                snprintf(filepath, sizeof(filepath), "%s/rnw_%lu_f%d",
                         state->test_dir, thread_id, source);
                snprintf(new_filepath, sizeof(new_filepath), "%s/rnw_%lu_f%d_renamed",
                         state->test_dir, thread_id, dest);
                
                if (rename(filepath, new_filepath) == 0) {
                    char old_name[256], new_name[256];
                    snprintf(old_name, sizeof(old_name), "rnw_%lu_f%d",
                             thread_id, source);
                    snprintf(new_name, sizeof(new_name), "rnw_%lu_f%d_renamed",
                             thread_id, dest);
                    
                    // Track rename in state tracker
                    tracker_record_delete(state->tracker, old_name);
                    tracker_record_create(state->tracker, new_name);
                    
                    atomic_fetch_add(&state->operations, 1);
                    __sync_fetch_and_add(&data->files_renamed, 1);
                } else {
                    __sync_fetch_and_add(&data->rename_failures, 1);
                }
            }
        }
        
        usleep(10000);  // 10ms between operations
    }
    
    return NULL;
}

// ============================================================================
// Workload Operations
// ============================================================================

static int rename_init(workload_state_t *state, const char *test_dir) {
    (void)test_dir;  // Unused
    rename_data_t *data = calloc(1, sizeof(rename_data_t));
    if (!data) return -1;
    
    state->workload_data = data;
    return 0;
}

static void rename_cleanup(workload_state_t *state) {
    if (state->workload_data) {
        free(state->workload_data);
        state->workload_data = NULL;
    }
}

static int get_default_readers(void) {
    return 10;
}

static int get_default_writers(void) {
    return 4;  // Slightly more writers for rename workload
}

static void get_stats(workload_state_t *state, char *buf, size_t len) {
    rename_data_t *data = (rename_data_t *)state->workload_data;
    snprintf(buf, len, "Created: %d, Deleted: %d, Renamed: %d, Rename failures: %d",
             data->files_created, data->files_deleted, 
             data->files_renamed, data->rename_failures);
}

// Forward declaration - we'll share the common reader
void *create_delete_reader(void *arg);

// Export workload descriptor
const workload_ops_t workload_rename = {
    .name = "rename",
    .description = "Rename-heavy workload (40% renames) - targets file visibility races",
    .init = rename_init,
    .reader_fn = create_delete_reader,  // Reuse reader from create_delete
    .writer_fn = rename_writer,
    .cleanup = rename_cleanup,
    .get_default_readers = get_default_readers,
    .get_default_writers = get_default_writers,
    .get_stats = get_stats,
};

