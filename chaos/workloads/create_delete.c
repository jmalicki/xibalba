/*
 * Create/Delete Workload
 * 
 * Original workload from simple_chaos_test.c
 * - Writers: Create files, delete every 3rd file
 * - Readers: Scan directory and validate
 * 
 * This is the baseline workload for testing directory scan consistency.
 */

#include "workload.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include "../../common/dir_reader.h"

#define FILES_PER_WRITER 50

// Workload-specific data
typedef struct {
    int files_created;
    int files_deleted;
} create_delete_data_t;

// ============================================================================
// Reader Thread
// ============================================================================

void *create_delete_reader(void *arg) {
    workload_state_t *state = (workload_state_t *)arg;
    struct dir_reader *reader = dir_reader_create_classic();
    uint64_t thread_id = (uint64_t)pthread_self();
    
    while (!atomic_load(&state->stop)) {
        // Open directory
        if (dir_reader_open(reader, state->test_dir) < 0) {
            usleep(1000);
            continue;
        }
        
        // Record read start time
        struct timespec ts_start, ts_end;
        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        uint64_t read_start_ns = (uint64_t)ts_start.tv_sec * 1000000000ULL + 
                                  (uint64_t)ts_start.tv_nsec;
        tracker_record_read_start(state->tracker, thread_id);
        
        // Read all entries
        struct dir_entry entries[200];
        char *entry_names[200];
        int total_read = 0;
        int count;
        
        while ((count = dir_reader_read(reader, entries, 100)) > 0) {
            for (int i = 0; i < count && total_read < 200; i++) {
                // Skip . and ..
                if (strcmp(entries[i].name, ".") == 0 || 
                    strcmp(entries[i].name, "..") == 0) {
                    continue;
                }
                
                tracker_record_read_entry(state->tracker, entries[i].name, thread_id);
                entry_names[total_read] = strdup(entries[i].name);
                total_read++;
            }
        }
        
        // Record read end time
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        uint64_t read_end_ns = (uint64_t)ts_end.tv_sec * 1000000000ULL + 
                                (uint64_t)ts_end.tv_nsec;
        tracker_record_read_end(state->tracker, thread_id);
        
        // Validate
        validation_result_t result = tracker_validate_read(
            state->tracker, entry_names, total_read,
            read_start_ns, read_end_ns, state->model, state->scan_export
        );
        
        // Report bugs
        if (result.total_bugs_found > 0) {
            atomic_fetch_add(&state->bugs_found, result.total_bugs_found);
            
            printf("🐛 BUG FOUND (thread %lu):\n", thread_id);
            if (result.missing_entries > 0)
                printf("   Missing: %lu\n", result.missing_entries);
            if (result.duplicate_entries > 0)
                printf("   Duplicates: %lu\n", result.duplicate_entries);
            if (result.phantom_entries > 0)
                printf("   Phantoms: %lu\n", result.phantom_entries);
            
            // Queue bug event
            if (state->bug_queue) {
                bug_queue_t *queue = (bug_queue_t *)state->bug_queue;
                uint64_t write_pos = atomic_fetch_add(&queue->write_idx, 1);
                uint64_t slot = write_pos % BUG_QUEUE_SIZE;
                
                bug_event_t *event = &queue->events[slot];
                const char *model_name = 
                    state->model == CONSISTENCY_POSIX ? "posix" :
                    state->model == CONSISTENCY_WEAK_POSIX ? "weak" :
                    state->model == CONSISTENCY_STRICT ? "strict" : "eventual";
                
                event->timestamp_ns = read_end_ns;
                event->thread_id = thread_id;
                event->read_start_ns = read_start_ns;
                event->read_end_ns = read_end_ns;
                event->model_name = model_name;
                event->total_bugs = result.total_bugs_found;
                event->missing = result.missing_entries;
                event->duplicates = result.duplicate_entries;
                event->phantoms = result.phantom_entries;
                event->files_read = total_read;
            }
        }
        
        // Cleanup
        for (int i = 0; i < total_read; i++) {
            free(entry_names[i]);
        }
        
        dir_reader_close(reader);
        atomic_fetch_add(&state->operations, 1);
        atomic_fetch_add(&state->reads_completed, 1);
        
        usleep(100);
    }
    
    dir_reader_destroy(reader);
    return NULL;
}

// ============================================================================
// Writer Thread
// ============================================================================

static void *create_delete_writer(void *arg) {
    workload_state_t *state = (workload_state_t *)arg;
    create_delete_data_t *data = (create_delete_data_t *)state->workload_data;
    uint64_t thread_id = (uint64_t)pthread_self();
    char filepath[512];
    int file_num = 0;
    
    while (!atomic_load(&state->stop)) {
        // Create a file
        snprintf(filepath, sizeof(filepath), "%s/writer_%lu_file_%d",
                 state->test_dir, thread_id, file_num);
        
        int fd = open(filepath, O_CREAT | O_WRONLY, 0644);
        if (fd >= 0) {
            close(fd);
            
            char filename[256];
            snprintf(filename, sizeof(filename), "writer_%lu_file_%d",
                     thread_id, file_num);
            tracker_record_create(state->tracker, filename);
            
            atomic_fetch_add(&state->operations, 1);
            __sync_fetch_and_add(&data->files_created, 1);
        }
        
        // Sometimes delete previous file (every 3rd)
        if (file_num > 0 && (file_num % 3) == 0) {
            snprintf(filepath, sizeof(filepath), "%s/writer_%lu_file_%d",
                     state->test_dir, thread_id, file_num - 1);
            
            if (unlink(filepath) == 0) {
                char filename[256];
                snprintf(filename, sizeof(filename), "writer_%lu_file_%d",
                         thread_id, file_num - 1);
                tracker_record_delete(state->tracker, filename);
                
                atomic_fetch_add(&state->operations, 1);
                __sync_fetch_and_add(&data->files_deleted, 1);
            }
        }
        
        file_num++;
        if (file_num >= FILES_PER_WRITER) {
            file_num = 0;
        }
        
        usleep(10000);  // 10ms between operations
    }
    
    return NULL;
}

// ============================================================================
// Workload Operations
// ============================================================================

static int create_delete_init(workload_state_t *state, const char *test_dir) {
    (void)test_dir;  // Unused
    create_delete_data_t *data = calloc(1, sizeof(create_delete_data_t));
    if (!data) return -1;
    
    state->workload_data = data;
    return 0;
}

static void create_delete_cleanup(workload_state_t *state) {
    if (state->workload_data) {
        free(state->workload_data);
        state->workload_data = NULL;
    }
}

static int get_default_readers(void) {
    return 10;  // Original default
}

static int get_default_writers(void) {
    return 3;  // Original default
}

static void get_stats(workload_state_t *state, char *buf, size_t len) {
    create_delete_data_t *data = (create_delete_data_t *)state->workload_data;
    snprintf(buf, len, "Files created: %d, Files deleted: %d",
             data->files_created, data->files_deleted);
}

// Export workload descriptor
const workload_ops_t workload_create_delete = {
    .name = "create_delete",
    .description = "Create and delete files (baseline workload)",
    .init = create_delete_init,
    .reader_fn = create_delete_reader,
    .writer_fn = create_delete_writer,
    .cleanup = create_delete_cleanup,
    .get_default_readers = get_default_readers,
    .get_default_writers = get_default_writers,
    .get_stats = get_stats,
};

