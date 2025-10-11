/* State Tracker for Xibalba Chaos Testing
 *
 * Tracks the expected state of a directory based on operations performed.
 * This is the "ground truth" used to validate directory reads for correctness.
 *
 * Jepsen-inspired approach:
 *   1. Track all CREATE operations (files we added)
 *   2. Track all DELETE operations (files we removed)
 *   3. Compute expected state at any point in time
 *   4. Compare actual directory reads with expected state
 *   5. Find bugs: missing entries, duplicates, phantom entries
 */

#ifndef STATE_TRACKER_H
#define STATE_TRACKER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#define MAX_ENTRIES 10000

/* Consistency models for validation */
typedef enum {
    CONSISTENCY_STRICT,       // Linearizable: all ops instantly visible (strict ordering)
    CONSISTENCY_WEAK_POSIX,   // POSIX weak: snapshot at read start, ops during read may/may not appear
    CONSISTENCY_EVENTUAL,     // Eventual: operations may take time to appear
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

/* Expected file state */
typedef struct {
    char filename[256];
    bool exists;            // Currently exists
    uint64_t create_time;   // When created
    uint64_t delete_time;   // When deleted (0 if not deleted)
} file_state_t;

/* Validation results */
typedef struct {
    uint64_t missing_entries;    // Files that should exist but weren't read
    uint64_t duplicate_entries;  // Files read multiple times in one scan
    uint64_t phantom_entries;    // Files read that shouldn't exist
    uint64_t total_operations;
    uint64_t total_bugs_found;
} validation_result_t;

/* State tracker */
typedef struct {
    file_state_t files[MAX_ENTRIES];
    uint64_t file_count;
    
    operation_t history[MAX_ENTRIES * 10];
    uint64_t history_count;
    
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

/* Validate a directory read against expected state
 * 
 * model: Consistency model to use for validation
 * read_start_ns: When read started (snapshot point)
 * read_end_ns: When read completed
 * 
 * Consistency models:
 *   STRICT: All operations before read_end MUST be visible
 *           (Linearizable - strictest, catches most bugs)
 *   
 *   WEAK_POSIX: Snapshot at read_start
 *           - Created BEFORE read: MUST appear (if not deleted)
 *           - Deleted BEFORE read: MUST NOT appear  
 *           - Created/deleted DURING: MAY appear (either valid)
 *           
 *   EVENTUAL: Operations may take time to propagate
 *           - Only duplicates are bugs
 *           - Missing/phantom entries may be propagation delays
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

