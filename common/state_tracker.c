/*
 * Copyright (c) 2025 Joseph Malicki
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* State Tracker Implementation with Vector Clocks
 *
 * Maintains ground truth of directory state for validation.
 * Uses vector clocks for precise causality tracking (Jepsen-style!)
 */

#include "state_tracker.h"
#include "vector_clock.h"
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
    
    // Initialize vector clock for causality tracking
    tracker->vclock = vclock_init();
    if (!tracker->vclock) {
        free(tracker);
        return NULL;
    }
    
    pthread_mutex_init(&tracker->lock, NULL);
    tracker->file_count = 0;
    tracker->history_count = 0;
    
    return tracker;
}

void tracker_record_create(state_tracker_t *tracker, const char *filename) {
    pthread_mutex_lock(&tracker->lock);
    
    // Tick vector clock (this operation happens NOW)
    vclock_tick(tracker->vclock, pthread_self());
    
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
        file->create_time = get_timestamp_ns();  // Keep timestamp for JSON
        file->delete_time = 0;
        
        // Snapshot vector clock at creation (for causality!)
        vclock_snapshot(tracker->vclock, file->create_vc);
        file->has_create_vc = true;
        file->has_delete_vc = false;
        memset(file->delete_vc, 0, sizeof(file->delete_vc));
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
    
    // Tick vector clock (deletion operation happens NOW)
    vclock_tick(tracker->vclock, pthread_self());
    
    // Mark file as deleted
    for (uint64_t i = 0; i < tracker->file_count; i++) {
        if (strcmp(tracker->files[i].filename, filename) == 0) {
            tracker->files[i].exists = false;
            tracker->files[i].delete_time = get_timestamp_ns();  // Keep timestamp for JSON
            
            // Snapshot vector clock at deletion (for causality!)
            vclock_snapshot(tracker->vclock, tracker->files[i].delete_vc);
            tracker->files[i].has_delete_vc = true;
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
    
    // Tick vector clock (read operation starts NOW)
    vclock_tick(tracker->vclock, (pthread_t)thread_id);
    
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
                                           uint64_t read_start_ns,
                                           uint64_t read_end_ns,
                                           consistency_model_t model) {
    validation_result_t result = {0};
    result.total_operations = 1;
    
    pthread_mutex_lock(&tracker->lock);
    
    // Tick vector clock for this read operation
    vclock_tick(tracker->vclock, pthread_self());
    
    // Snapshot vector clock at read time (establishes causality barrier!)
    uint64_t read_vc[MAX_THREADS];
    vclock_snapshot(tracker->vclock, read_vc);
    
    // Note: Timestamps kept for API compatibility and JSON export
    // but NOT used for validation - we use vector clocks for causality!
    (void)read_start_ns;
    (void)read_end_ns;
    
    // Check for duplicates (ALWAYS a bug in all consistency models)
    if (tracker_has_duplicates(actual_entries, num_actual)) {
        result.duplicate_entries++;
        result.total_bugs_found++;
    }
    
    // Validate based on consistency model using CAUSALITY (not timestamps!)
    for (uint64_t i = 0; i < tracker->file_count; i++) {
        file_state_t *file = &tracker->files[i];
        
        // Skip if file never existed
        if (!file->has_create_vc) {
            continue;
        }
        
        // ========================================================================
        // CAUSALITY-BASED VALIDATION (Jepsen-style!)
        // ========================================================================
        // Instead of comparing timestamps, we use happens-before relationships:
        //   - If create_vc happens-before read_vc: file DEFINITELY existed before read
        //   - Otherwise: concurrent or read-before-create (no established causality)
        //
        // This is MUCH more precise than timestamps!
        // ========================================================================
        
        // Use vector clocks to establish causality (happens-before relationships)
        bool create_happens_before_read = vclock_happens_before(
            file->create_vc, read_vc, tracker->vclock->num_registered);
        
        bool delete_happens_before_read = file->has_delete_vc && vclock_happens_before(
            file->delete_vc, read_vc, tracker->vclock->num_registered);
        
        // Check if file was actually read
        bool was_read = false;
        for (int j = 0; j < num_actual; j++) {
            if (strcmp(actual_entries[j], file->filename) == 0) {
                was_read = true;
                break;
            }
        }
        
        // Apply consistency model rules using CAUSALITY
        switch (model) {
            case CONSISTENCY_STRICT:
                // STRICT/Linearizable: If create happens-before read, file MUST be visible
                if (create_happens_before_read) {
                    // File DEFINITELY existed before read started
                    if (delete_happens_before_read) {
                        // File was deleted before read - MUST NOT appear
                        if (was_read) {
                            result.phantom_entries++;
                            result.total_bugs_found++;
                        }
                    } else {
                        // File still exists - MUST appear
                        if (!was_read) {
                            result.missing_entries++;
                            result.total_bugs_found++;
                        }
                    }
                }
                // If no happens-before relationship: concurrent, either outcome valid
                break;
                
            case CONSISTENCY_WEAK_POSIX:
                // POSIX weak: If create happens-before read, file MUST appear
                if (create_happens_before_read && !delete_happens_before_read) {
                    if (!was_read) {
                        result.missing_entries++;
                        result.total_bugs_found++;
                    }
                }
                
                // If delete happens-before read: MUST NOT appear
                if (delete_happens_before_read && was_read) {
                    result.phantom_entries++;
                    result.total_bugs_found++;
                }
                
                // No causality established: concurrent operations, either outcome valid
                break;
                
            case CONSISTENCY_EVENTUAL:
                // Eventual: Only duplicates are bugs
                // Missing/phantom may be propagation delays - not counted
                // (Duplicates already checked above)
                break;
        }
    }
    
    // Check for phantom entries: files in actual_entries that don't exist in ground truth
    // This detects completely unknown files (never created)
    for (int i = 0; i < num_actual; i++) {
        bool found_in_tracker = false;
        for (uint64_t j = 0; j < tracker->file_count; j++) {
            if (strcmp(actual_entries[i], tracker->files[j].filename) == 0) {
                found_in_tracker = true;
                break;
            }
        }
        
        // File was read but never created = phantom!
        if (!found_in_tracker && model != CONSISTENCY_EVENTUAL) {
            result.phantom_entries++;
            result.total_bugs_found++;
        }
    }
    
    pthread_mutex_unlock(&tracker->lock);
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
        if (tracker->vclock) {
            vclock_cleanup(tracker->vclock);
        }
        pthread_mutex_destroy(&tracker->lock);
        free(tracker);
    }
}

