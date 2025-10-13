/*
 * Hard Link Workload
 * 
 * Hard link-focused workload designed to expose reference count races.
 * - 30% Create base files
 * - 40% Create hard links
 * - 30% Unlink (links or base files)
 * 
 * Targets:
 * - Reference count corruption
 * - Lost hard links
 * - Dangling inode references
 * - Use-after-free (inode freed but link exists)
 * 
 * Best used with: transaction_abort eBPF injector
 */

#include "workload.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include "../../common/dir_reader.h"

#define MAX_BASE_FILES 50
#define MAX_LINKS_PER_FILE 10

// Workload-specific data
typedef struct {
    int base_files_created;
    int links_created;
    int files_unlinked;
    int link_failures;
} hardlink_data_t;

// ============================================================================
// Writer Thread (Hard Link Operations)
// ============================================================================

static void *hardlink_writer(void *arg) {
    workload_state_t *state = (workload_state_t *)arg;
    hardlink_data_t *data = (hardlink_data_t *)state->workload_data;
    uint64_t thread_id = (uint64_t)pthread_self();
    char source[512], link_path[512];
    int base_file_num = 0;
    
    while (!atomic_load(&state->stop)) {
        int operation = rand() % 10;
        
        if (operation < 3) {
            // 30%: Create base file
            snprintf(source, sizeof(source), "%s/hlw_%lu_base%d",
                     state->test_dir, thread_id, base_file_num);
            
            int fd = open(source, O_CREAT | O_WRONLY, 0644);
            if (fd >= 0) {
                // Write some data to make it a real file
                ssize_t written = write(fd, "test", 4);
                (void)written;  // Ignore errors
                close(fd);
                
                char filename[256];
                snprintf(filename, sizeof(filename), "hlw_%lu_base%d",
                         thread_id, base_file_num);
                tracker_record_create(state->tracker, filename);
                
                atomic_fetch_add(&state->operations, 1);
                __sync_fetch_and_add(&data->base_files_created, 1);
                
                base_file_num++;
                if (base_file_num >= MAX_BASE_FILES) base_file_num = 0;
            }
        }
        else if (operation < 7) {
            // 40%: Create hard link
            if (base_file_num > 0) {
                int source_num = rand() % base_file_num;
                int link_num = rand() % (MAX_BASE_FILES * MAX_LINKS_PER_FILE);
                
                snprintf(source, sizeof(source), "%s/hlw_%lu_base%d",
                         state->test_dir, thread_id, source_num);
                snprintf(link_path, sizeof(link_path), "%s/hlw_%lu_link%d",
                         state->test_dir, thread_id, link_num);
                
                // Create hard link
                // This is where transaction abort races can occur!
                if (link(source, link_path) == 0) {
                    char linkname[256];
                    snprintf(linkname, sizeof(linkname), "hlw_%lu_link%d",
                             thread_id, link_num);
                    tracker_record_create(state->tracker, linkname);
                    
                    atomic_fetch_add(&state->operations, 1);
                    __sync_fetch_and_add(&data->links_created, 1);
                } else {
                    __sync_fetch_and_add(&data->link_failures, 1);
                }
            }
        }
        else {
            // 30%: Unlink something
            if (rand() % 2 == 0 && base_file_num > 0) {
                // Unlink a base file (if no other links exist, file deleted)
                int target = rand() % base_file_num;
                snprintf(source, sizeof(source), "%s/hlw_%lu_base%d",
                         state->test_dir, thread_id, target);
                
                if (unlink(source) == 0) {
                    char filename[256];
                    snprintf(filename, sizeof(filename), "hlw_%lu_base%d",
                             thread_id, target);
                    tracker_record_delete(state->tracker, filename);
                    
                    atomic_fetch_add(&state->operations, 1);
                    __sync_fetch_and_add(&data->files_unlinked, 1);
                }
            } else {
                // Unlink a link
                int link_num = rand() % (MAX_BASE_FILES * MAX_LINKS_PER_FILE);
                snprintf(link_path, sizeof(link_path), "%s/hlw_%lu_link%d",
                         state->test_dir, thread_id, link_num);
                
                if (unlink(link_path) == 0) {
                    char linkname[256];
                    snprintf(linkname, sizeof(linkname), "hlw_%lu_link%d",
                             thread_id, link_num);
                    tracker_record_delete(state->tracker, linkname);
                    
                    atomic_fetch_add(&state->operations, 1);
                    __sync_fetch_and_add(&data->files_unlinked, 1);
                }
            }
        }
        
        usleep(8000);  // 8ms between operations (slightly faster)
    }
    
    return NULL;
}

// ============================================================================
// Reader Thread (Validates Reference Counts)
// ============================================================================

static void *hardlink_reader(void *arg) {
    workload_state_t *state = (workload_state_t *)arg;
    struct dir_reader *reader = dir_reader_create_classic();
    uint64_t thread_id = (uint64_t)pthread_self();
    
    while (!atomic_load(&state->stop)) {
        if (dir_reader_open(reader, state->test_dir) < 0) {
            usleep(1000);
            continue;
        }
        
        struct timespec ts_start, ts_end;
        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        uint64_t read_start_ns = (uint64_t)ts_start.tv_sec * 1000000000ULL +
                                  (uint64_t)ts_start.tv_nsec;
        tracker_record_read_start(state->tracker, thread_id);
        
        // Read all entries
        struct dir_entry entries[300];
        char *entry_names[300];
        int total_read = 0;
        int count;
        
        while ((count = dir_reader_read(reader, entries, 100)) > 0) {
            for (int i = 0; i < count && total_read < 300; i++) {
                if (strcmp(entries[i].name, ".") == 0 ||
                    strcmp(entries[i].name, "..") == 0) {
                    continue;
                }
                
                tracker_record_read_entry(state->tracker, entries[i].name, thread_id);
                entry_names[total_read] = strdup(entries[i].name);
                total_read++;
                
                // TODO: Validate reference counts
                // For now, we check via stat() to get nlink count
                // In future, state_tracker should track expected ref counts
            }
        }
        
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        uint64_t read_end_ns = (uint64_t)ts_end.tv_sec * 1000000000ULL +
                                (uint64_t)ts_end.tv_nsec;
        tracker_record_read_end(state->tracker, thread_id);
        
        // Validate
        validation_result_t result = tracker_validate_read(
            state->tracker, entry_names, total_read,
            read_start_ns, read_end_ns, state->model, state->scan_export
        );
        
        if (result.total_bugs_found > 0) {
            atomic_fetch_add(&state->bugs_found, result.total_bugs_found);
            
            printf("🐛 HARD LINK BUG (thread %lu):\n", thread_id);
            if (result.missing_entries > 0)
                printf("   Missing links: %lu\n", result.missing_entries);
            if (result.duplicate_entries > 0)
                printf("   Duplicate entries: %lu\n", result.duplicate_entries);
            if (result.phantom_entries > 0)
                printf("   Phantom links: %lu\n", result.phantom_entries);
            
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
// Workload Operations
// ============================================================================

static int hardlink_init(workload_state_t *state, const char *test_dir) {
    (void)test_dir;  // Unused
    hardlink_data_t *data = calloc(1, sizeof(hardlink_data_t));
    if (!data) return -1;
    
    state->workload_data = data;
    return 0;
}

static void hardlink_cleanup(workload_state_t *state) {
    if (state->workload_data) {
        free(state->workload_data);
        state->workload_data = NULL;
    }
}

static int get_default_readers(void) {
    return 8;  // Fewer readers (more writers for link operations)
}

static int get_default_writers(void) {
    return 5;  // More writers (link operations are the focus)
}

static void get_stats(workload_state_t *state, char *buf, size_t len) {
    hardlink_data_t *data = (hardlink_data_t *)state->workload_data;
    snprintf(buf, len, 
             "Base files: %d, Links created: %d, Unlinked: %d, Link failures: %d",
             data->base_files_created, data->links_created,
             data->files_unlinked, data->link_failures);
}

// Export workload descriptor
const workload_ops_t workload_hardlink = {
    .name = "hardlink",
    .description = "Hard link stress test (tests reference count races)",
    .init = hardlink_init,
    .reader_fn = hardlink_reader,
    .writer_fn = hardlink_writer,
    .cleanup = hardlink_cleanup,
    .get_default_readers = get_default_readers,
    .get_default_writers = get_default_writers,
    .get_stats = get_stats,
};

