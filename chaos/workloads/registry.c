/*
 * Xibalba Workload Registry
 * 
 * Central registry of all available workload modules.
 */

#include "workload.h"
#include <string.h>
#include <stdio.h>

// Forward declarations (implemented in respective .c files)
extern const workload_ops_t workload_create_delete;
extern const workload_ops_t workload_rename;
extern const workload_ops_t workload_hardlink;
extern const workload_ops_t workload_mixed;
// extern const workload_ops_t workload_btrfs_torture;  // TODO: Implement

// All available workloads
static const workload_ops_t *all_workloads[] = {
    &workload_create_delete,
    &workload_rename,
    &workload_hardlink,
    &workload_mixed,
    // &workload_btrfs_torture,  // TODO: Uncomment when implemented
    NULL  // Sentinel
};

const workload_ops_t *get_workload(const char *name) {
    if (!name) return NULL;
    
    for (int i = 0; all_workloads[i] != NULL; i++) {
        if (strcmp(all_workloads[i]->name, name) == 0) {
            return all_workloads[i];
        }
    }
    return NULL;
}

void list_workloads(FILE *out) {
    fprintf(out, "Available Workloads:\n\n");
    
    for (int i = 0; all_workloads[i] != NULL; i++) {
        const workload_ops_t *wl = all_workloads[i];
        
        fprintf(out, "  %s\n", wl->name);
        fprintf(out, "    Description: %s\n", wl->description);
        fprintf(out, "    Default readers: %d\n", wl->get_default_readers());
        fprintf(out, "    Default writers: %d\n", wl->get_default_writers());
        fprintf(out, "\n");
    }
}

