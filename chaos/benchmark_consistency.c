/* Consistency Model Benchmark
 *
 * Quantitatively measures bug detection rates across different consistency models.
 * 
 * Runs the same workload with STRICT, WEAK_POSIX, and EVENTUAL models,
 * reporting bug counts for each to validate our claims about model sensitivity.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/wait.h>

typedef struct {
    const char *model_name;
    const char *model_flag;
    int iterations;
    uint64_t total_bugs;
    uint64_t duplicate_bugs;
    uint64_t missing_bugs;
    uint64_t phantom_bugs;
} benchmark_result_t;

static int run_test(const char *test_binary, const char *test_dir, const char *model_flag) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child: run test
        execl(test_binary, test_binary, model_flag, test_dir, NULL);
        exit(1);  // If exec fails
    } else if (pid > 0) {
        // Parent: wait for result
        int status;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <test_directory> [iterations]\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Benchmark consistency model sensitivity.\n");
        fprintf(stderr, "Runs tests with --strict, --weak, and --eventual models\n");
        fprintf(stderr, "to quantitatively measure bug detection rates.\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Example:\n");
        fprintf(stderr, "  mkdir -p /tmp/benchmark\n");
        fprintf(stderr, "  %s /tmp/benchmark 10\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Output: CSV data for analysis\n");
        return 1;
    }
    
    const char *test_dir = argv[1];
    int iterations = argc >= 3 ? atoi(argv[2]) : 5;
    
    if (iterations < 1 || iterations > 100) {
        fprintf(stderr, "Error: Iterations must be 1-100\n");
        return 1;
    }
    
    // Find test binary (assume it's in same directory as benchmark)
    char test_binary[512];
    const char *pwd = getenv("PWD");
    snprintf(test_binary, sizeof(test_binary), "%s/simple_chaos_test", 
             pwd ? pwd : ".");
    
    // Check if test binary exists
    if (access(test_binary, X_OK) != 0) {
        fprintf(stderr, "Error: Cannot find test binary at %s\n", test_binary);
        fprintf(stderr, "Run from bazel-bin/chaos/ directory\n");
        return 1;
    }
    
    printf("=== Xibalba Consistency Model Benchmark ===\n");
    printf("\n");
    printf("Test directory: %s\n", test_dir);
    printf("Iterations: %d per model\n", iterations);
    printf("Test binary: %s\n", test_binary);
    printf("\n");
    printf("This will run %d tests to measure bug detection rates:\n", iterations * 3);
    printf("  - %d tests with STRICT model\n", iterations);
    printf("  - %d tests with WEAK_POSIX model\n", iterations);
    printf("  - %d tests with EVENTUAL model\n", iterations);
    printf("\n");
    printf("Estimated time: %d seconds\n", iterations * 3 * 7);
    printf("\n");
    
    benchmark_result_t results[3] = {
        {"STRICT (Linearizable)", "--strict", iterations, 0, 0, 0, 0},
        {"WEAK_POSIX (Default)", "--weak", iterations, 0, 0, 0, 0},
        {"EVENTUAL (Permissive)", "--eventual", iterations, 0, 0, 0, 0},
    };
    
    // Run benchmarks
    for (int model_idx = 0; model_idx < 3; model_idx++) {
        benchmark_result_t *result = &results[model_idx];
        
        printf("Testing %s model...\n", result->model_name);
        
        for (int iter = 0; iter < iterations; iter++) {
            printf("  Iteration %d/%d... ", iter + 1, iterations);
            fflush(stdout);
            
            int exit_code = run_test(test_binary, test_dir, result->model_flag);
            
            if (exit_code == 0) {
                printf("0 bugs\n");
            } else if (exit_code == 1) {
                printf("bugs found\n");
                result->total_bugs++;
                // TODO: Parse output to get detailed bug counts
            } else {
                printf("ERROR (exit %d)\n", exit_code);
            }
        }
        
        printf("\n");
    }
    
    // Display results
    printf("=== Benchmark Results ===\n");
    printf("\n");
    printf("| Model | Iterations | Bugs Found | Bug Rate | Relative Sensitivity |\n");
    printf("|-------|------------|------------|----------|---------------------|\n");
    
    double baseline_rate = (double)results[2].total_bugs / (double)results[2].iterations;  // EVENTUAL
    
    for (int i = 0; i < 3; i++) {
        benchmark_result_t *r = &results[i];
        double bug_rate = (double)r->total_bugs / (double)r->iterations;
        double relative = baseline_rate > 0 ? bug_rate / baseline_rate : 0.0;
        
        printf("| %s | %d | %lu | %.2f | %.1fx |\n",
               r->model_name, r->iterations, r->total_bugs, bug_rate, relative);
    }
    
    printf("\n");
    
    // CSV output for analysis
    printf("=== CSV Output (for graphing) ===\n");
    printf("model,iterations,total_bugs,bug_rate\n");
    for (int i = 0; i < 3; i++) {
        benchmark_result_t *r = &results[i];
        double bug_rate = (double)r->total_bugs / (double)r->iterations;
        printf("%s,%d,%lu,%.4f\n", r->model_flag, r->iterations, r->total_bugs, bug_rate);
    }
    
    printf("\n");
    
    // Interpretation
    printf("=== Interpretation ===\n");
    printf("\n");
    
    if (results[0].total_bugs > results[1].total_bugs && 
        results[1].total_bugs > results[2].total_bugs) {
        printf("✅ Expected ordering: STRICT > WEAK > EVENTUAL\n");
        printf("   This validates that consistency models work as designed.\n");
    } else {
        printf("⚠️  Unexpected ordering of bug detection rates!\n");
        printf("   This may indicate:\n");
        printf("   - Validation logic bug\n");
        printf("   - Extremely stable filesystem\n");
        printf("   - Insufficient race windows (try with eBPF delays)\n");
    }
    
    printf("\n");
    
    if (results[2].total_bugs == 0) {
        printf("✅ EVENTUAL model found 0 bugs (expected on stable kernel)\n");
    } else {
        printf("🐛 EVENTUAL model found bugs (duplicate entries on stable kernel!)\n");
    }
    
    printf("\n");
    printf("To increase bug detection, run with eBPF delays:\n");
    printf("  bazel run //chaos:pause_controller -- 50 500\n");
    printf("\n");
    
    return 0;
}

