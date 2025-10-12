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

/* State Tracker for Xibalba Chaos Testing
 *
 * Tracks the expected state of a directory based on operations performed.
 * This is the "ground truth" used to validate directory reads for correctness.
 *
 * Jepsen-inspired approach with VECTOR CLOCKS:
 *   1. Track all CREATE operations with vector clocks (not timestamps!)
 *   2. Track all DELETE operations with vector clocks
 *   3. Use happens-before relationships (causality)
 *   4. Compare actual directory reads with expected state
 *   5. Find bugs: missing entries, duplicates, phantom entries
 * 
 * Why vector clocks?
 *   - Timestamps can't detect causality (clock skew, concurrent events)
 *   - Vector clocks capture true happens-before relationships
 *   - Example: If read's VC shows it happened-after create's VC,
 *     then file MUST be visible (not "probably should be")
 *   - This is how Jepsen detects linearizability violations!
 */

#ifndef STATE_TRACKER_H
#define STATE_TRACKER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "vector_clock.h"

#define MAX_ENTRIES 10000

/* Consistency models for validation */
typedef enum {
    CONSISTENCY_POSIX_MINIMAL,  // POSIX minimum: only duplicates are bugs (what POSIX actually forbids)
    CONSISTENCY_WEAK_POSIX,     // POSIX weak: causally-ordered ops should be visible (stronger than POSIX)
    CONSISTENCY_STRICT,         // Linearizable: all ops instantly visible (academic/research)
    CONSISTENCY_EVENTUAL,       // Eventual: operations may take time to appear (distributed FS)
} consistency_model_t;

/* Operation types for history tracking */
typedef enum {
    OP_CREATE,
    OP_DELETE,
    OP_READ_START,
    OP_READ_ENTRY,
    OP_READ_END,
} operation_type_t;

/* Single operation in history */
typedef struct {
    uint64_t timestamp_ns;
    operation_type_t type;
    char filename[256];
    uint64_t thread_id;
} operation_t;

/* Expected file state with causality tracking */
typedef struct {
    char filename[256];
    bool exists;            // Currently exists
    
    // Timestamps (for JSON export and debugging)
    uint64_t create_time;   // Wall-clock when created
    uint64_t delete_time;   // Wall-clock when deleted (0 if not deleted)
    
    // Vector clocks (for causality-based validation!)
    uint64_t create_vc[MAX_THREADS];  // Vector clock snapshot at creation
    uint64_t delete_vc[MAX_THREADS];  // Vector clock snapshot at deletion
    bool has_create_vc;     // Whether create_vc is valid
    bool has_delete_vc;     // Whether delete_vc is valid
} file_state_t;

/* Validation results */
typedef struct {
    uint64_t missing_entries;    // Files that should exist but weren't read
    uint64_t duplicate_entries;  // Files read multiple times in one scan
    uint64_t phantom_entries;    // Files read that shouldn't exist
    uint64_t total_operations;
    uint64_t total_bugs_found;
} validation_result_t;

/* State tracker with vector clock for causality */
typedef struct {
    file_state_t files[MAX_ENTRIES];
    uint64_t file_count;
    
    operation_t history[MAX_ENTRIES * 10];
    uint64_t history_count;
    
    vector_clock_t *vclock;  // Global vector clock for all operations
    
    pthread_mutex_t lock;
} state_tracker_t;

/* Initialize tracker */
state_tracker_t* tracker_init(void);

/* Record operations */
void tracker_record_create(state_tracker_t *tracker, const char *filename);
void tracker_record_delete(state_tracker_t *tracker, const char *filename);
void tracker_record_read_start(state_tracker_t *tracker, uint64_t thread_id);
void tracker_record_read_entry(state_tracker_t *tracker, const char *filename, uint64_t thread_id);
void tracker_record_read_end(state_tracker_t *tracker, uint64_t thread_id);

/* Get expected state at a point in time */
int tracker_get_expected_entries(state_tracker_t *tracker, uint64_t timestamp_ns, 
                                   char **entries, int max_entries);

/* Validate a directory read using CAUSALITY (vector clocks!)
 * 
 * Uses happens-before relationships instead of timestamps for precision.
 * 
 * Parameters:
 *   model: Consistency model to use for validation
 *   read_start_ns: Timestamp for JSON export (not used for validation!)
 *   read_end_ns: Timestamp for JSON export (not used for validation!)
 * 
 * Causality-based validation (Jepsen-style):
 *   - Each operation captures a vector clock snapshot
 *   - Validation uses happens-before relationships
 *   - If create_vc happens-before read_vc: file MUST be visible
 *   - If no causality: concurrent operations, either outcome valid
 * 
 * Consistency models:
 *   STRICT: If create happens-before read → file MUST be visible
 *           If delete happens-before read → file MUST NOT be visible
 *   
 *   WEAK_POSIX: Same as STRICT but with causality (not timestamp windows)
 *           
 *   EVENTUAL: Only duplicates are bugs (missing/phantom allowed)
 */
validation_result_t tracker_validate_read(state_tracker_t *tracker,
                                           char **actual_entries,
                                           int num_actual,
                                           uint64_t read_start_ns,
                                           uint64_t read_end_ns,
                                           consistency_model_t model);

/* Check for duplicate entries in a single read */
bool tracker_has_duplicates(char **entries, int num_entries);

/* Export history to JSON for analysis */
void tracker_export_history(state_tracker_t *tracker, const char *output_file);

/* Cleanup */
void tracker_cleanup(state_tracker_t *tracker);

#endif // STATE_TRACKER_H

