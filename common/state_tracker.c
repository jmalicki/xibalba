/* State Tracker Implementation
 *
 * Maintains ground truth of directory state for validation
 */

#include "state_tracker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t get_timestamp_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

state_tracker_t* tracker_init(void) {
    state_tracker_t *tracker = calloc(1, sizeof(state_tracker_t));
    if (!tracker) {
        return NULL;
    }
    
    pthread_mutex_init(&tracker->lock, NULL);
    tracker->file_count = 0;
    tracker->history_count = 0;
    
    return tracker;
}

void tracker_record_create(state_tracker_t *tracker, const char *filename) {
    pthread_mutex_lock(&tracker->lock);
    
    // Find or create file entry
    file_state_t *file = NULL;
    for (uint64_t i = 0; i < tracker->file_count; i++) {
        if (strcmp(tracker->files[i].filename, filename) == 0) {
            file = &tracker->files[i];
            break;
        }
    }
    
    if (!file && tracker->file_count < MAX_ENTRIES) {
        file = &tracker->files[tracker->file_count++];
        strncpy(file->filename, filename, sizeof(file->filename) - 1);
    }
    
    if (file) {
        file->exists = true;
        file->create_time = get_timestamp_ns();
        file->delete_time = 0;
    }
    
    // Record in history
    if (tracker->history_count < MAX_ENTRIES * 10) {
        operation_t *op = &tracker->history[tracker->history_count++];
        op->timestamp_ns = get_timestamp_ns();
        op->type = OP_CREATE;
        strncpy(op->filename, filename, sizeof(op->filename) - 1);
        op->thread_id = (uint64_t)pthread_self();
    }
    
    pthread_mutex_unlock(&tracker->lock);
}

void tracker_record_delete(state_tracker_t *tracker, const char *filename) {
    pthread_mutex_lock(&tracker->lock);
    
    // Mark file as deleted
    for (uint64_t i = 0; i < tracker->file_count; i++) {
        if (strcmp(tracker->files[i].filename, filename) == 0) {
            tracker->files[i].exists = false;
            tracker->files[i].delete_time = get_timestamp_ns();
            break;
        }
    }
    
    // Record in history
    if (tracker->history_count < MAX_ENTRIES * 10) {
        operation_t *op = &tracker->history[tracker->history_count++];
        op->timestamp_ns = get_timestamp_ns();
        op->type = OP_DELETE;
        strncpy(op->filename, filename, sizeof(op->filename) - 1);
        op->thread_id = (uint64_t)pthread_self();
    }
    
    pthread_mutex_unlock(&tracker->lock);
}

void tracker_record_read_start(state_tracker_t *tracker, uint64_t thread_id) {
    pthread_mutex_lock(&tracker->lock);
    
    if (tracker->history_count < MAX_ENTRIES * 10) {
        operation_t *op = &tracker->history[tracker->history_count++];
        op->timestamp_ns = get_timestamp_ns();
        op->type = OP_READ_START;
        op->filename[0] = '\0';
        op->thread_id = thread_id;
    }
    
    pthread_mutex_unlock(&tracker->lock);
}

void tracker_record_read_entry(state_tracker_t *tracker, const char *filename, uint64_t thread_id) {
    pthread_mutex_lock(&tracker->lock);
    
    if (tracker->history_count < MAX_ENTRIES * 10) {
        operation_t *op = &tracker->history[tracker->history_count++];
        op->timestamp_ns = get_timestamp_ns();
        op->type = OP_READ_ENTRY;
        strncpy(op->filename, filename, sizeof(op->filename) - 1);
        op->thread_id = thread_id;
    }
    
    pthread_mutex_unlock(&tracker->lock);
}

void tracker_record_read_end(state_tracker_t *tracker, uint64_t thread_id) {
    pthread_mutex_lock(&tracker->lock);
    
    if (tracker->history_count < MAX_ENTRIES * 10) {
        operation_t *op = &tracker->history[tracker->history_count++];
        op->timestamp_ns = get_timestamp_ns();
        op->type = OP_READ_END;
        op->filename[0] = '\0';
        op->thread_id = thread_id;
    }
    
    pthread_mutex_unlock(&tracker->lock);
}

int tracker_get_expected_entries(state_tracker_t *tracker, uint64_t timestamp_ns,
                                   char **entries, int max_entries) {
    pthread_mutex_lock(&tracker->lock);
    
    int count = 0;
    for (uint64_t i = 0; i < tracker->file_count && count < max_entries; i++) {
        file_state_t *file = &tracker->files[i];
        
        // File should be visible if:
        // 1. Created before this timestamp
        // 2. Either not deleted, or deleted after this timestamp
        bool created_before = file->create_time <= timestamp_ns;
        bool not_deleted = file->delete_time == 0 || file->delete_time > timestamp_ns;
        
        if (file->exists && created_before && not_deleted) {
            entries[count] = strdup(file->filename);
            count++;
        }
    }
    
    pthread_mutex_unlock(&tracker->lock);
    return count;
}

validation_result_t tracker_validate_read(state_tracker_t *tracker,
                                           char **actual_entries,
                                           int num_actual,
                                           uint64_t read_timestamp_ns) {
    validation_result_t result = {0};
    result.total_operations = 1;
    
    // Get expected entries at this timestamp
    char *expected[MAX_ENTRIES];
    int num_expected = tracker_get_expected_entries(
        tracker, read_timestamp_ns, expected, MAX_ENTRIES
    );
    
    // Check for duplicates
    if (tracker_has_duplicates(actual_entries, num_actual)) {
        result.duplicate_entries++;
        result.total_bugs_found++;
    }
    
    // Check for missing entries
    for (int i = 0; i < num_expected; i++) {
        bool found = false;
        for (int j = 0; j < num_actual; j++) {
            if (strcmp(expected[i], actual_entries[j]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            result.missing_entries++;
            result.total_bugs_found++;
        }
    }
    
    // Check for phantom entries
    for (int i = 0; i < num_actual; i++) {
        if (strcmp(actual_entries[i], ".") == 0 || 
            strcmp(actual_entries[i], "..") == 0) {
            continue;  // Skip . and ..
        }
        
        bool found = false;
        for (int j = 0; j < num_expected; j++) {
            if (strcmp(actual_entries[i], expected[j]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            result.phantom_entries++;
            result.total_bugs_found++;
        }
    }
    
    // Cleanup expected entries
    for (int i = 0; i < num_expected; i++) {
        free(expected[i]);
    }
    
    return result;
}

bool tracker_has_duplicates(char **entries, int num_entries) {
    for (int i = 0; i < num_entries; i++) {
        for (int j = i + 1; j < num_entries; j++) {
            if (strcmp(entries[i], entries[j]) == 0) {
                return true;  // Found duplicate!
            }
        }
    }
    return false;
}

void tracker_export_history(state_tracker_t *tracker, const char *output_file) {
    pthread_mutex_lock(&tracker->lock);
    
    FILE *f = fopen(output_file, "w");
    if (!f) {
        pthread_mutex_unlock(&tracker->lock);
        return;
    }
    
    fprintf(f, "{\n");
    fprintf(f, "  \"operations\": [\n");
    
    for (uint64_t i = 0; i < tracker->history_count; i++) {
        operation_t *op = &tracker->history[i];
        const char *type_str = 
            op->type == OP_CREATE ? "CREATE" :
            op->type == OP_DELETE ? "DELETE" :
            op->type == OP_READ_START ? "READ_START" :
            op->type == OP_READ_ENTRY ? "READ_ENTRY" : "READ_END";
        
        fprintf(f, "    {\"ts\": %lu, \"type\": \"%s\", \"file\": \"%s\", \"thread\": %lu}",
                op->timestamp_ns, type_str, op->filename, op->thread_id);
        
        if (i < tracker->history_count - 1) {
            fprintf(f, ",\n");
        } else {
            fprintf(f, "\n");
        }
    }
    
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    
    fclose(f);
    pthread_mutex_unlock(&tracker->lock);
}

void tracker_cleanup(state_tracker_t *tracker) {
    if (tracker) {
        pthread_mutex_destroy(&tracker->lock);
        free(tracker);
    }
}

