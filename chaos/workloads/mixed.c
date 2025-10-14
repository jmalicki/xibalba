/*
 * Mixed Workload
 * 
 * Balanced mix of all directory operations:
 * - 25% Create
 * - 20% Delete
 * - 25% Rename
 * - 20% Hard link
 * - 10% Symlink (TODO)
 * 
 * This is the most comprehensive workload, testing all types of
 * directory modifications simultaneously.
 * 
 * Best used with: multi_hook or vfs_delay eBPF injector
 */

#include "workload.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include "../../common/dir_reader.h"

#define MAX_FILES 100

// Workload-specific data
typedef struct {
    int creates;
    int deletes;
    int renames;
    int links;
    int symlinks;
    int total_ops;
} mixed_data_t;

// ============================================================================
// Writer Thread (Mixed Operations)
// ============================================================================

static void *mixed_writer(void *arg) {
    workload_state_t *state = (workload_state_t *)arg;
    mixed_data_t *data = (mixed_data_t *)state->workload_data;
    uint64_t thread_id = (uint64_t)pthread_self();
    char path1[512], path2[512];
    int file_counter = 0;
    
    while (!atomic_load(&state->stop)) {
        int operation = rand() % 100;
        __sync_fetch_and_add(&data->total_ops, 1);
        
        if (operation < 25) {
            // 25%: Create
            snprintf(path1, sizeof(path1), "%s/mix_%lu_%d",
                     state->test_dir, thread_id, file_counter++);
            
            int fd = open(path1, O_CREAT | O_WRONLY, 0644);
            if (fd >= 0) {
                ssize_t written = write(fd, &thread_id, sizeof(thread_id));
                (void)written;  // Ignore errors
                close(fd);
                
                char filename[256];
                snprintf(filename, sizeof(filename), "mix_%lu_%d",
                         thread_id, file_counter - 1);
                tracker_record_create(state->tracker, filename);
                
                atomic_fetch_add(&state->operations, 1);
                __sync_fetch_and_add(&data->creates, 1);
            }
        }
        else if (operation < 45) {
            // 20%: Delete
            if (file_counter > 0) {
                int target = rand() % file_counter;
                snprintf(path1, sizeof(path1), "%s/mix_%lu_%d",
                         state->test_dir, thread_id, target);
                
                if (unlink(path1) == 0) {
                    char filename[256];
                    snprintf(filename, sizeof(filename), "mix_%lu_%d",
                             thread_id, target);
                    tracker_record_delete(state->tracker, filename);
                    
                    atomic_fetch_add(&state->operations, 1);
                    __sync_fetch_and_add(&data->deletes, 1);
                }
            }
        }
        else if (operation < 70) {
            // 25%: Rename
            if (file_counter > 0) {
                int source_num = rand() % file_counter;
                int dest_num = rand() % MAX_FILES;
                
                snprintf(path1, sizeof(path1), "%s/mix_%lu_%d",
                         state->test_dir, thread_id, source_num);
                snprintf(path2, sizeof(path2), "%s/mix_%lu_r%d",
                         state->test_dir, thread_id, dest_num);
                
                if (rename(path1, path2) == 0) {
                    char old_name[256], new_name[256];
                    snprintf(old_name, sizeof(old_name), "mix_%lu_%d",
                             thread_id, source_num);
                    snprintf(new_name, sizeof(new_name), "mix_%lu_r%d",
                             thread_id, dest_num);
                    
                    tracker_record_delete(state->tracker, old_name);
                    tracker_record_create(state->tracker, new_name);
                    
                    atomic_fetch_add(&state->operations, 1);
                    __sync_fetch_and_add(&data->renames, 1);
                }
            }
        }
        else if (operation < 90) {
            // 20%: Hard link
            if (file_counter > 0) {
                int source_num = rand() % file_counter;
                int link_num = rand() % (MAX_FILES * 2);
                
                snprintf(path1, sizeof(path1), "%s/mix_%lu_%d",
                         state->test_dir, thread_id, source_num);
                snprintf(path2, sizeof(path2), "%s/mix_%lu_l%d",
                         state->test_dir, thread_id, link_num);
                
                if (link(path1, path2) == 0) {
                    char linkname[256];
                    snprintf(linkname, sizeof(linkname), "mix_%lu_l%d",
                             thread_id, link_num);
                    tracker_record_create(state->tracker, linkname);
                    
                    atomic_fetch_add(&state->operations, 1);
                    __sync_fetch_and_add(&data->links, 1);
                }
            }
        }
        else {
            // 10%: Symlink (TODO: Implement symlink tracking in state_tracker)
            if (file_counter > 0) {
                int source_num = rand() % file_counter;
                int link_num = rand() % MAX_FILES;
                
                snprintf(path1, sizeof(path1), "mix_%lu_%d",
                         thread_id, source_num);
                snprintf(path2, sizeof(path2), "%s/mix_%lu_s%d",
                         state->test_dir, thread_id, link_num);
                
                if (symlink(path1, path2) == 0) {
                    // Note: Symlinks are files themselves, so track as create
                    char linkname[256];
                    snprintf(linkname, sizeof(linkname), "mix_%lu_s%d",
                             thread_id, link_num);
                    tracker_record_create(state->tracker, linkname);
                    
                    atomic_fetch_add(&state->operations, 1);
                    __sync_fetch_and_add(&data->symlinks, 1);
                }
            }
        }
        
        if (file_counter >= MAX_FILES) file_counter = 0;
        
        usleep(12000);  // 12ms (slower due to more complex operations)
    }
    
    return NULL;
}

// ============================================================================
// Workload Operations
// ============================================================================

static int mixed_init(workload_state_t *state, const char *test_dir) {
    (void)test_dir;  // Unused
    mixed_data_t *data = calloc(1, sizeof(mixed_data_t));
    if (!data) return -1;
    
    state->workload_data = data;
    return 0;
}

static void mixed_cleanup(workload_state_t *state) {
    if (state->workload_data) {
        free(state->workload_data);
        state->workload_data = NULL;
    }
}

static int get_default_readers(void) {
    return 8;
}

static int get_default_writers(void) {
    return 6;  // More writers for comprehensive testing
}

static void get_stats(workload_state_t *state, char *buf, size_t len) {
    mixed_data_t *data = (mixed_data_t *)state->workload_data;
    snprintf(buf, len,
             "Creates: %d, Deletes: %d, Renames: %d, Links: %d, Symlinks: %d (Total: %d)",
             data->creates, data->deletes, data->renames,
             data->links, data->symlinks, data->total_ops);
}

// Forward declaration
void *hardlink_reader(void *arg);

// Export workload descriptor
const workload_ops_t workload_mixed = {
    .name = "mixed",
    .description = "Mixed operations (create, delete, rename, link, symlink)",
    .init = mixed_init,
    .reader_fn = hardlink_reader,  // Reuse hardlink reader (handles more files)
    .writer_fn = mixed_writer,
    .cleanup = mixed_cleanup,
    .get_default_readers = get_default_readers,
    .get_default_writers = get_default_writers,
    .get_stats = get_stats,
};

