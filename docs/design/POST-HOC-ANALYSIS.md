# Post-Hoc Multi-Model Analysis Design

**Goal**: Run test once, analyze with multiple consistency models afterward

## Problem

Currently, testing multiple consistency models requires multiple runs:
```bash
# 3 models × 3 filesystems × 60 seconds = 9 minutes
bazel run //vm:qemu_test_runner -- --model posix ...
bazel run //vm:qemu_test_runner -- --model weak ...  
bazel run //vm:qemu_test_runner -- --model strict ...
```

**Issues**:
- Slow (N × runtime)
- Different timing/workloads per run
- Can't compare apples-to-apples
- Wastes resources

## Solution

**Run once, export all validation data, analyze multiple ways**:
```bash
# Single run (60 seconds)
bazel run //vm:qemu_test_runner -- --export-scans ...

# Analyze with different models (instant!)
bazel run //tools:analyze_scans -- --model posix scans.jsonl
bazel run //tools:analyze_scans -- --model weak scans.jsonl
bazel run //tools:analyze_scans -- --model strict scans.jsonl
```

## Data Export Format

### Scan Event (JSONL)

Each directory scan exports one JSON line:

```json
{
  "scan_id": 12345,
  "thread_id": 139847234,
  "timestamp_ns": 1234567890,
  "vc": [1,2,0,5,3,0,...],  // Vector clock at scan time
  
  "expected": {
    "file1.txt": {
      "create_vc": [1,0,0,0,...],
      "delete_vc": null
    },
    "file2.txt": {
      "create_vc": [1,1,0,0,...],
      "delete_vc": [1,2,0,4,...]  
    }
  },
  
  "actual": ["file1.txt", "file3.txt", "file1.txt"],  // What was actually read
  
  "metadata": {
    "duplicates": ["file1.txt"],  // Quick check
    "missing": ["file2.txt"],
    "phantoms": ["file3.txt"]
  }
}
```

**Size**: ~1-10KB per scan (depends on file count)

**Benefits**:
- Contains all causality information
- Can replay validation with different rules
- Compressed for efficient storage

### Alternative: Lightweight Export

If full export is too large, export only anomalies:

```json
{
  "scan_id": 12345,
  "timestamp_ns": 1234567890,
  "vc": [1,2,0,5,3,0,...],
  "duplicates": [
    {"file": "file1.txt", "positions": [5, 12]}
  ],
  "missing": [
    {
      "file": "file2.txt",
      "create_vc": [1,1,0,0,...],
      "delete_vc": [1,2,0,4,...]
    }
  ],
  "phantoms": [
    {"file": "file3.txt", "first_seen_at": 7}
  ]
}
```

**Size**: ~100-500 bytes per scan (only if has issues)

## Implementation Plan

### Phase 1: Export Scan Data

**File**: `common/state_tracker.c`

Modify `tracker_validate_read()`:

```c
validation_result_t tracker_validate_read(
    state_tracker_t *tracker,
    char **actual_entries,
    int num_actual_entries,
    uint64_t read_start_ns,
    uint64_t read_end_ns,
    consistency_model_t model,
    FILE *scan_export_file)  // NEW: export file
{
    // ... existing validation ...
    
    // Export scan data for post-hoc analysis
    if (scan_export_file) {
        export_scan_result(scan_export_file, tracker, actual_entries, 
                          num_actual_entries, read_vc);
    }
    
    // ... return results ...
}
```

### Phase 2: Create Analyzer Tool

**File**: `tools/analyze-scans.c`

```c
int main(int argc, char **argv) {
    const char *scan_file = argv[1];
    const char *model = argv[2];  // "posix", "weak", "strict"
    
    FILE *f = fopen(scan_file, "r");
    char line[MAX_LINE];
    
    validation_result_t results = {0};
    
    while (fgets(line, sizeof(line), f)) {
        scan_result_t scan = parse_scan_json(line);
        
        // Apply consistency model
        if (strcmp(model, "posix") == 0) {
            check_posix_violations(&scan, &results);
        } else if (strcmp(model, "weak") == 0) {
            check_weak_violations(&scan, &results);
        } else if (strcmp(model, "strict") == 0) {
            check_strict_violations(&scan, &results);
        }
    }
    
    print_results(&results);
}
```

### Phase 3: Update Test Runner

**File**: `chaos/simple_chaos_test.c`

```c
// Open scan export file
FILE *scan_export = fopen("xibalba-scans.jsonl", "w");

// During validation
validation_result_t result = tracker_validate_read(
    tracker, actual_entries, num_entries,
    read_start, read_end, 
    CONSISTENCY_NONE,  // Don't validate yet!
    scan_export);       // Just export data

fclose(scan_export);

// At end of test
printf("Scan data exported to xibalba-scans.jsonl\n");
printf("Analyze with:\n");
printf("  tools/analyze-scans xibalba-scans.jsonl posix\n");
printf("  tools/analyze-scans xibalba-scans.jsonl weak\n");
```

## Benefits

### 1. Speed
- **Before**: 3 models × 60s = 180s
- **After**: 1 × 60s + 3 × 0.1s = 60.3s
- **Speedup**: 3x faster!

### 2. Consistency
- Same workload for all models
- Same timing/race conditions
- Truly apples-to-apples comparison

### 3. Retroactive Analysis
- Apply new models to old data
- Tune consistency thresholds
- Research without re-running tests

### 4. Debugging
- Examine specific scans
- Understand why bugs happen
- Replay validation logic

## Example Usage

### Run Test with Export
```bash
bazel run //chaos:simple_chaos_test -- \
    --export-scans \
    --duration 60 \
    /test
```

### Analyze with Different Models
```bash
# POSIX (duplicates only)
bazel run //tools:analyze_scans -- \
    --model posix \
    /test/.output/xibalba-scans.jsonl

# Weak (stale/phantom/missing)
bazel run //tools:analyze_scans -- \
    --model weak \
    /test/.output/xibalba-scans.jsonl

# Strict (everything must be current)
bazel run //tools:analyze_scans -- \
    --model strict \
    /test/.output/xibalba-scans.jsonl

# Compare all at once
bazel run //tools:compare_models -- \
    /test/.output/xibalba-scans.jsonl
```

### Output

```
Model: POSIX
  Bugs: 0/1000 scans ✅
  Violations: 0 duplicate entries

Model: Weak  
  Bugs: 245/1000 scans ⚠️
  Missing: 120, Phantoms: 95, Stale: 30

Model: Strict
  Bugs: 987/1000 scans ⚠️
  Not current: 980, Missing: 5, Phantoms: 2
```

## Implementation Estimate

**Effort**: 4-6 hours
- 2 hours: Modify state_tracker to export scan data
- 1 hour: Create JSONL export format
- 2 hours: Create analyzer tool
- 1 hour: Testing and validation

**Complexity**: Medium
- Refactor validation logic (separate collection from analysis)
- JSON export (use existing jq patterns)
- C parsing of JSON (simple with existing libraries)

**Value**: High
- 3x faster testing
- Better comparisons
- Retroactive analysis
- Research-quality data

## Open Questions

1. **Export size**: How large will scan files be?
   - Answer: ~100KB-1MB for 1000 scans (acceptable)

2. **Memory usage**: Can we fit all scans in memory?
   - Answer: Use streaming (one scan at a time)

3. **Vector clock arrays**: Too large?
   - Answer: Only export non-zero entries (sparse format)

## Next Steps

1. ✅ Design complete (this document)
2. Implement scan export in state_tracker.c
3. Create analyzer tool (tools/analyze-scans.c)
4. Update simple_chaos_test.c
5. Create compare_models tool
6. Documentation

## Success Criteria

- [ ] Single test run completes
- [ ] xibalba-scans.jsonl is created
- [ ] Analyzer can process scans.jsonl
- [ ] Results match live validation
- [ ] All 4 consistency models analyzable
- [ ] 3x+ speedup achieved

---

**Status**: Ready to implement  
**Priority**: High (makes testing much more efficient)  
**Risk**: Low (additive feature, doesn't break existing)

