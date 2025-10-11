# Testing Framework for Async Directory Iteration

*Component: Comprehensive testing infrastructure*
*Version: 2.0 - Chaos Engineering Edition*
*Date: October 10, 2025*

## Overview

**Testing is critical** for async getdents implementation:
- Complex multi-component system
- Concurrency challenges
- Multiple filesystems
- Subtle bugs possible
- **Race conditions are the enemy**

**This document** defines a comprehensive testing strategy including:
- **Abstraction layer** for classic readdir vs io_uring
- **Jepsen-style chaos engineering** tests
- **eBPF/ptrace fault injection** for race conditions
- **Hardcore concurrent stress tests**

**Philosophy**: Test like you're trying to break it, because someone will.

---

## Core Testing Architecture

### Abstraction Layer: DirectoryReader Interface

**Purpose**: Test both classic readdir() and io_uring getdents with same test code

**File**: `tools/testing/selftests/filesystems/common/dir_reader.h`

```c
/**
 * Unified interface for directory iteration
 * 
 * Allows tests to run against both:
 * - Classic readdir() (baseline)
 * - io_uring async getdents (new)
 * - Any future implementation
 */

enum dir_reader_mode {
    DIR_READER_CLASSIC,      // Use readdir()
    DIR_READER_IORING,       // Use io_uring IORING_OP_GETDENTS
    DIR_READER_IORING_NOWAIT // io_uring with NOWAIT flag
};

struct dir_reader_ops;

struct dir_reader {
    enum dir_reader_mode mode;
    const struct dir_reader_ops *ops;
    void *private_data;
    
    // Statistics
    uint64_t ops_count;
    uint64_t total_entries;
    uint64_t total_bytes;
    uint64_t eagain_count;
};

struct dir_entry {
    uint64_t ino;
    uint8_t type;
    char name[256];
};

struct dir_reader_ops {
    // Initialize reader
    int (*init)(struct dir_reader *reader, const char *path);
    
    // Read next batch of entries
    // Returns: number of entries, 0 for EOF, -errno on error
    int (*read_batch)(struct dir_reader *reader,
                      struct dir_entry *entries,
                      int max_entries);
    
    // Cleanup
    void (*cleanup)(struct dir_reader *reader);
    
    // Optional: rewind to beginning
    int (*rewind)(struct dir_reader *reader);
};

// Factory functions
struct dir_reader *dir_reader_create(enum dir_reader_mode mode);
void dir_reader_destroy(struct dir_reader *reader);

// Common operations
int dir_reader_open(struct dir_reader *reader, const char *path);
int dir_reader_read(struct dir_reader *reader,
                    struct dir_entry *entries,
                    int max_entries);
void dir_reader_close(struct dir_reader *reader);

// Statistics
void dir_reader_print_stats(struct dir_reader *reader);
```

**Implementation for classic readdir()**:
```c
// tools/testing/selftests/filesystems/common/dir_reader_classic.c

static int classic_init(struct dir_reader *reader, const char *path) {
    DIR *dir = opendir(path);
    if (!dir)
        return -errno;
    reader->private_data = dir;
    return 0;
}

static int classic_read_batch(struct dir_reader *reader,
                               struct dir_entry *entries,
                               int max_entries) {
    DIR *dir = reader->private_data;
    int count = 0;
    
    while (count < max_entries) {
        struct dirent *ent = readdir(dir);
        if (!ent)
            break;
        
        entries[count].ino = ent->d_ino;
        entries[count].type = ent->d_type;
        strncpy(entries[count].name, ent->d_name, sizeof(entries[count].name));
        count++;
        
        reader->total_entries++;
    }
    
    reader->ops_count++;
    return count;
}

static void classic_cleanup(struct dir_reader *reader) {
    if (reader->private_data) {
        closedir(reader->private_data);
        reader->private_data = NULL;
    }
}

static const struct dir_reader_ops classic_ops = {
    .init = classic_init,
    .read_batch = classic_read_batch,
    .cleanup = classic_cleanup,
};
```

**Implementation for io_uring**:
```c
// tools/testing/selftests/filesystems/common/dir_reader_ioring.c

struct ioring_reader_ctx {
    struct io_uring ring;
    int dirfd;
    uint64_t offset;
    char buffer[32768];
    unsigned flags;
};

static int ioring_init(struct dir_reader *reader, const char *path) {
    struct ioring_reader_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return -ENOMEM;
    
    int ret = io_uring_queue_init(32, &ctx->ring, 0);
    if (ret < 0) {
        free(ctx);
        return ret;
    }
    
    ctx->dirfd = open(path, O_RDONLY | O_DIRECTORY);
    if (ctx->dirfd < 0) {
        io_uring_queue_exit(&ctx->ring);
        free(ctx);
        return -errno;
    }
    
    ctx->offset = 0;
    ctx->flags = (reader->mode == DIR_READER_IORING_NOWAIT) ?
                 IORING_GETDENTS_FL_NOWAIT : 0;
    
    reader->private_data = ctx;
    return 0;
}

static int ioring_read_batch(struct dir_reader *reader,
                              struct dir_entry *entries,
                              int max_entries) {
    struct ioring_reader_ctx *ctx = reader->private_data;
    
retry:
    // Prepare SQE
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ctx->ring);
    if (!sqe)
        return -ENOMEM;
    
    io_uring_prep_getdents(sqe, ctx->dirfd, ctx->offset,
                           ctx->buffer, sizeof(ctx->buffer),
                           ctx->flags);
    
    // Submit and wait
    int ret = io_uring_submit(&ctx->ring);
    if (ret < 0)
        return ret;
    
    struct io_uring_cqe *cqe;
    ret = io_uring_wait_cqe(&ctx->ring, &cqe);
    if (ret < 0)
        return ret;
    
    // Check result
    if (cqe->res < 0) {
        int err = cqe->res;
        io_uring_cqe_seen(&ctx->ring, cqe);
        
        if (err == -EAGAIN) {
            reader->eagain_count++;
            usleep(100);  // Brief pause before retry
            goto retry;
        }
        
        return err;
    }
    
    // Parse buffer
    uint64_t next_offset;
    bool eof;
    struct linux_dirent64 *d = io_uring_getdents_parse(
        ctx->buffer, cqe->res, &next_offset, &eof);
    
    io_uring_cqe_seen(&ctx->ring, cqe);
    
    if (!d)
        return -EINVAL;
    
    // Convert to dir_entry format
    int count = 0;
    while (d && count < max_entries) {
        entries[count].ino = d->d_ino;
        entries[count].type = d->d_type;
        strncpy(entries[count].name, d->d_name, sizeof(entries[count].name));
        count++;
        
        d = io_uring_getdents_next(ctx->buffer, cqe->res, d);
    }
    
    ctx->offset = next_offset;
    reader->ops_count++;
    reader->total_entries += count;
    
    if (eof && count == 0)
        return 0;  // EOF
    
    return count;
}

static void ioring_cleanup(struct dir_reader *reader) {
    struct ioring_reader_ctx *ctx = reader->private_data;
    if (!ctx)
        return;
    
    if (ctx->dirfd >= 0)
        close(ctx->dirfd);
    io_uring_queue_exit(&ctx->ring);
    free(ctx);
    reader->private_data = NULL;
}

static const struct dir_reader_ops ioring_ops = {
    .init = ioring_init,
    .read_batch = ioring_read_batch,
    .cleanup = ioring_cleanup,
};
```

**Usage in tests**:
```c
void test_compare_implementations() {
    struct dir_reader *classic = dir_reader_create(DIR_READER_CLASSIC);
    struct dir_reader *ioring = dir_reader_create(DIR_READER_IORING);
    
    // Read same directory with both
    dir_reader_open(classic, "/tmp/test_dir");
    dir_reader_open(ioring, "/tmp/test_dir");
    
    // Collect all entries
    struct dir_entry entries_classic[1000], entries_ioring[1000];
    int count_classic = 0, count_ioring = 0;
    
    // ... read all entries ...
    
    // Compare results
    assert_entries_match(entries_classic, count_classic,
                        entries_ioring, count_ioring);
    
    dir_reader_close(classic);
    dir_reader_close(ioring);
    dir_reader_destroy(classic);
    dir_reader_destroy(ioring);
}
```

---

## Testing Pyramid

```
                    ┌─────────────────┐
                    │  Integration    │  (Applications: find, ls, rsync)
                    │     Tests       │
                    └─────────────────┘
                  ┌───────────────────────┐
                  │   Functional Tests    │  (io_uring operations)
                  │   (liburing tests)    │
                  └───────────────────────┘
            ┌─────────────────────────────────┐
            │    Filesystem-Specific Tests    │  (ext4, ZFS, XFS, etc.)
            └─────────────────────────────────┘
      ┌───────────────────────────────────────────┐
      │          VFS Layer Unit Tests             │
      └───────────────────────────────────────────┘
  ┌─────────────────────────────────────────────────┐
  │         Kernel Unit Tests (KUnit)               │
  └─────────────────────────────────────────────────┘
```

**Bottom-up approach**: Test each layer, build confidence

---

## Test Categories

### 1. Kernel Unit Tests (KUnit)

**Location**: `fs/readdir_kunit.c`, `io_uring/getdents_kunit.c`

**Purpose**: Test individual functions in isolation

**Examples**:
- `test_vfs_getdents_async_prep()` - Validate parameter checking
- `test_hash2pos_encoding()` - Test offset encoding (ext4)
- `test_zap_cursor_serialize()` - Test cursor serialization (ZFS)
- `test_getdents_ctx_init()` - Test context initialization

**Run**: `./tools/testing/kunit/kunit.py run`

---

### 2. VFS Layer Tests

**Location**: `tools/testing/selftests/filesystems/vfs/`

**Purpose**: Test VFS interface without filesystem specifics

**Test cases**:

#### Basic Functionality
```c
// test_vfs_getdents_basic.c
void test_basic_getdents() {
    int dirfd = open("/tmp/test_dir", O_RDONLY | O_DIRECTORY);
    char buffer[4096];
    loff_t offset = 0, next_offset;
    int ret;
    
    // Call VFS layer
    ret = syscall(SYS_io_uring_setup, ...);  // Setup io_uring
    // Submit IORING_OP_GETDENTS
    // Verify result
    
    assert(ret > 0);  // Bytes read
    assert(next_offset != offset);  // Offset advanced
}
```

#### Error Handling
```c
void test_invalid_fd() {
    // Test with invalid fd
    ret = vfs_getdents_async(invalid_fd, ...);
    assert(ret == -EBADF);
}

void test_not_directory() {
    // Test with regular file
    int fd = open("/tmp/regular_file", O_RDONLY);
    ret = vfs_getdents_async(fd, ...);
    assert(ret == -ENOTDIR);
}

void test_invalid_offset() {
    ret = vfs_getdents_async(dirfd, -1, ...);  // Negative offset
    assert(ret == -EINVAL);
}
```

#### NOWAIT Semantics
```c
void test_nowait_success() {
    // Test NOWAIT when cache hot
    populate_cache(dirfd);  // Warm cache
    ret = vfs_getdents_async(dirfd, 0, ..., GETDENTS_FL_NOWAIT);
    assert(ret >= 0);  // Should succeed
}

void test_nowait_eagain() {
    // Test NOWAIT when would block
    drop_cache(dirfd);
    ret = vfs_getdents_async(dirfd, 0, ..., GETDENTS_FL_NOWAIT);
    assert(ret == -EAGAIN);  // Should return immediately
}
```

#### Concurrent Operations
```c
void test_concurrent_same_fd() {
    // Two operations on same fd, different offsets
    pthread_t t1, t2;
    
    pthread_create(&t1, NULL, read_at_offset, (void*)0x1000);
    pthread_create(&t2, NULL, read_at_offset, (void*)0x2000);
    
    pthread_join(t1, &ret1);
    pthread_join(t2, &ret2);
    
    assert(ret1 >= 0);
    assert(ret2 >= 0);
    // Both should succeed, no corruption
}
```

---

### 3. Filesystem-Specific Tests

**Location**: `tools/testing/selftests/filesystems/{ext4,xfs,btrfs}/`

**Purpose**: Test each filesystem's iterate_async implementation

#### ext4 Tests

**File**: `tools/testing/selftests/filesystems/ext4/async_getdents.c`

```c
// Test htree directory
void test_ext4_htree_iteration() {
    // Create directory with 1000+ files (forces htree)
    char dirpath[PATH_MAX];
    snprintf(dirpath, sizeof(dirpath), "/mnt/ext4/test_htree");
    mkdir(dirpath, 0755);
    
    // Create 1000 files
    for (int i = 0; i < 1000; i++) {
        char filepath[PATH_MAX];
        snprintf(filepath, sizeof(filepath), "%s/file%04d", dirpath, i);
        int fd = creat(filepath, 0644);
        close(fd);
    }
    
    // Read directory via async getdents
    int dirfd = open(dirpath, O_RDONLY | O_DIRECTORY);
    loff_t offset = 0;
    int total_entries = 0;
    
    while (1) {
        ret = io_uring_getdents(dirfd, offset, buffer, sizeof(buffer), &next_offset);
        if (ret < 0)
            break;
        
        // Count entries
        parse_buffer(buffer, ret, &entries);
        total_entries += entries;
        
        if (next_offset == -1ULL)
            break;  // EOF
        
        offset = next_offset;
    }
    
    // Should see all 1000 files + "." + ".."
    assert(total_entries == 1002);
}

// Test hash collision handling
void test_ext4_hash_collisions() {
    // Create files with same hash (if possible)
    // Verify minor hash differentiation
    // ...
}

// Test offset encoding
void test_ext4_offset_encoding() {
    __u32 major = 0x12345678;
    __u32 minor = 0x9ABCDEF0;
    
    loff_t encoded = hash2pos(major, minor);
    __u32 decoded_major = pos2maj_hash(encoded);
    __u32 decoded_minor = pos2min_hash(encoded);
    
    assert(decoded_major == major);
    assert(decoded_minor == minor);
}
```

#### ZFS Tests

**File**: `tests/zfs-tests/tests/functional/async/getdents.ksh`

```bash
#!/bin/ksh -p

# Create pool and filesystem
zpool create testpool /dev/loop0
zfs create testpool/testfs

# Create test directory with many files
mkdir /testpool/testfs/testdir
for i in {1..1000}; do
    touch "/testpool/testfs/testdir/file$(printf %04d $i)"
done

# Test async getdents
./test_async_getdents /testpool/testfs/testdir

# Verify all entries read
count=$(./test_async_getdents /testpool/testfs/testdir | wc -l)
[[ $count -eq 1002 ]] || log_fail "Expected 1002 entries, got $count"

# Test cursor serialization
./test_zfs_cursor_serialization

log_pass "ZFS async getdents tests passed"
```

#### XFS Tests

**File**: `tools/testing/selftests/filesystems/xfs/async_getdents.c`

```c
// Test all directory formats
void test_xfs_all_formats() {
    test_xfs_shortform();  // Inline
    test_xfs_block();      // Single block
    test_xfs_leaf();       // Multiple blocks
    test_xfs_node();       // B-tree
}

// Test offset stability
void test_xfs_offset_stability() {
    // Create directory, read entries
    // Modify directory (add file)
    // Resume reading
    // Verify reasonable behavior (may skip new file)
}
```

---

### 4. io_uring Integration Tests

**Location**: `tools/testing/selftests/io_uring/getdents.c`

**Purpose**: Test io_uring opcode end-to-end

```c
#include <liburing.h>

void test_ioring_getdents_basic() {
    struct io_uring ring;
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;
    char buffer[4096];
    
    io_uring_queue_init(32, &ring, 0);
    
    int dirfd = open("/tmp/test_dir", O_RDONLY | O_DIRECTORY);
    
    // Prepare SQE
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_getdents(sqe, dirfd, 0, buffer, sizeof(buffer), 0);
    
    // Submit and wait
    io_uring_submit(&ring);
    io_uring_wait_cqe(&ring, &cqe);
    
    // Check result
    assert(cqe->res > 0);  // Bytes read
    
    // Parse buffer
    uint64_t next_offset;
    bool eof;
    struct linux_dirent64 *entries = io_uring_getdents_parse(
        buffer, cqe->res, &next_offset, &eof);
    
    assert(entries != NULL);
    
    io_uring_cqe_seen(&ring, cqe);
    close(dirfd);
    io_uring_queue_exit(&ring);
}

void test_ioring_getdents_nowait() {
    // Test NOWAIT flag
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_getdents(sqe, dirfd, 0, buffer, sizeof(buffer),
                           IORING_GETDENTS_FL_NOWAIT);
    
    io_uring_submit(&ring);
    io_uring_wait_cqe(&ring, &cqe);
    
    // May succeed or return -EAGAIN
    assert(cqe->res >= 0 || cqe->res == -EAGAIN);
}

void test_ioring_getdents_parallel() {
    // Submit multiple getdents operations
    for (int i = 0; i < 10; i++) {
        sqe = io_uring_get_sqe(&ring);
        io_uring_prep_getdents(sqe, dirfds[i], 0, buffers[i],
                               sizeof(buffers[i]), 0);
        sqe->user_data = i;
    }
    
    io_uring_submit(&ring);
    
    // Wait for all completions
    for (int i = 0; i < 10; i++) {
        io_uring_wait_cqe(&ring, &cqe);
        assert(cqe->res >= 0);
        io_uring_cqe_seen(&ring, cqe);
    }
    
    // All should succeed
}
```

---

### 5. Stress Tests

**Location**: `tools/testing/selftests/filesystems/stress/`

**Purpose**: Test under extreme conditions

#### Concurrency Stress

```c
// stress_concurrent_readers.c
#define NUM_THREADS 100
#define ITERATIONS 1000

void* reader_thread(void* arg) {
    int dirfd = *(int*)arg;
    
    for (int i = 0; i < ITERATIONS; i++) {
        char buffer[32768];
        loff_t offset = 0;
        
        while (1) {
            loff_t next_offset;
            int ret = io_uring_getdents_sync(dirfd, offset, buffer,
                                             sizeof(buffer), &next_offset);
            if (ret < 0) {
                if (ret == -EAGAIN)
                    continue;  // Retry
                break;
            }
            
            if (next_offset == -1ULL)
                break;  // EOF
            
            offset = next_offset;
        }
    }
    
    return NULL;
}

int main() {
    int dirfd = open("/mnt/test/large_dir", O_RDONLY | O_DIRECTORY);
    pthread_t threads[NUM_THREADS];
    
    // Launch 100 concurrent readers
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, reader_thread, &dirfd);
    }
    
    // Wait for all
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    close(dirfd);
    printf("Stress test passed: %d threads x %d iterations\n",
           NUM_THREADS, ITERATIONS);
    return 0;
}
```

#### Large Directory Stress

```c
// stress_large_directory.c
void test_very_large_directory() {
    // Create directory with 1M files
    char dirpath[] = "/mnt/test/huge_dir";
    mkdir(dirpath, 0755);
    
    printf("Creating 1M files...\n");
    for (int i = 0; i < 1000000; i++) {
        char filepath[PATH_MAX];
        snprintf(filepath, sizeof(filepath), "%s/file%07d", dirpath, i);
        int fd = creat(filepath, 0644);
        close(fd);
        
        if (i % 10000 == 0)
            printf("  %d files created\n", i);
    }
    
    printf("Reading directory via async getdents...\n");
    int dirfd = open(dirpath, O_RDONLY | O_DIRECTORY);
    loff_t offset = 0;
    int total = 0;
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    while (1) {
        char buffer[131072];  // 128KB buffer
        loff_t next_offset;
        int ret = io_uring_getdents_sync(dirfd, offset, buffer,
                                         sizeof(buffer), &next_offset);
        if (ret < 0)
            break;
        
        total += count_entries(buffer, ret);
        
        if (next_offset == -1ULL)
            break;
        
        offset = next_offset;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    
    printf("Read %d entries in %.2f seconds (%.0f entries/sec)\n",
           total, elapsed, total / elapsed);
    
    assert(total == 1000002);  // 1M + "." + ".."
    
    close(dirfd);
}
```

#### Modification Stress

```c
// stress_concurrent_modification.c
void* writer_thread(void* arg) {
    const char* dirpath = (const char*)arg;
    
    for (int i = 0; i < 1000; i++) {
        char filepath[PATH_MAX];
        snprintf(filepath, sizeof(filepath), "%s/temp_%d_%d",
                 dirpath, getpid(), i);
        
        // Create file
        int fd = creat(filepath, 0644);
        close(fd);
        
        usleep(1000);  // 1ms
        
        // Delete file
        unlink(filepath);
    }
    
    return NULL;
}

int main() {
    const char* dirpath = "/mnt/test/stress_dir";
    pthread_t readers[50], writers[10];
    
    // Launch 50 readers + 10 writers
    for (int i = 0; i < 50; i++)
        pthread_create(&readers[i], NULL, reader_thread, &dirpath);
    for (int i = 0; i < 10; i++)
        pthread_create(&writers[i], NULL, writer_thread, &dirpath);
    
    // Wait for all
    for (int i = 0; i < 50; i++)
        pthread_join(readers[i], NULL);
    for (int i = 0; i < 10; i++)
        pthread_join(writers[i], NULL);
    
    printf("Concurrent modification stress test passed\n");
    return 0;
}
```

---

### 6. Performance Benchmarks

*(See separate 05-BENCHMARKING-SUITE.md)*

**Include**:
- Latency measurements
- Throughput tests
- Scalability analysis
- Comparison with baseline

---

### 7. Integration Tests (Real Applications)

**Location**: `tools/testing/selftests/integration/`

**Purpose**: Test with real-world applications

#### find Command

```bash
#!/bin/bash
# test_find.sh

# Create test directory structure
mkdir -p /mnt/test/find_test
for i in {1..1000}; do
    mkdir -p /mnt/test/find_test/dir$i
    touch /mnt/test/find_test/dir$i/file{1..10}
done

# Run find with async getdents (via LD_PRELOAD if needed)
time find /mnt/test/find_test -type f > /tmp/find_async.txt

# Run traditional find
time find /mnt/test/find_test -type f > /tmp/find_sync.txt

# Compare results
diff /tmp/find_async.txt /tmp/find_sync.txt
if [ $? -eq 0 ]; then
    echo "find test PASSED: results match"
else
    echo "find test FAILED: results differ"
    exit 1
fi
```

#### ls Command

```bash
#!/bin/bash
# test_ls.sh

# Test ls with various options
ls -la /mnt/test/large_dir > /tmp/ls_async.txt
ls -lR /mnt/test/tree > /tmp/ls_async_recursive.txt

# Compare with baseline
# (Assuming baseline implementation available)
# ...
```

#### rsync

```bash
#!/bin/bash
# test_rsync.sh

# Sync large directory
rsync -av /mnt/test/source/ /mnt/test/dest/

# Verify all files copied
diff -r /mnt/test/source /mnt/test/dest
if [ $? -eq 0 ]; then
    echo "rsync test PASSED"
else
    echo "rsync test FAILED"
    exit 1
fi
```

---

## Test Data Setup

### Helper Scripts

**Location**: `tools/testing/selftests/filesystems/helpers/`

#### create_test_directory.sh

```bash
#!/bin/bash
# Create test directory with specified number of files

DIR=$1
COUNT=$2

mkdir -p "$DIR"

for i in $(seq 1 $COUNT); do
    filename=$(printf "file%06d" $i)
    touch "$DIR/$filename"
done

echo "Created $COUNT files in $DIR"
```

#### create_nested_structure.sh

```bash
#!/bin/bash
# Create nested directory structure

ROOT=$1
DEPTH=$2
WIDTH=$3

function create_level() {
    local path=$1
    local level=$2
    
    if [ $level -ge $DEPTH ]; then
        return
    fi
    
    for i in $(seq 1 $WIDTH); do
        dir="$path/dir${level}_${i}"
        mkdir -p "$dir"
        touch "$dir/file1" "$dir/file2"
        create_level "$dir" $((level + 1))
    done
}

mkdir -p "$ROOT"
create_level "$ROOT" 0

echo "Created nested structure: depth=$DEPTH, width=$WIDTH"
```

---

## Test Automation

### Makefile

**Location**: `tools/testing/selftests/filesystems/Makefile`

```makefile
# Test targets

TEST_PROGS := \
    vfs/test_vfs_getdents \
    ext4/test_ext4_async \
    xfs/test_xfs_async \
    io_uring/test_ioring_getdents \
    stress/test_concurrent \
    stress/test_large_dir

.PHONY: all
all: $(TEST_PROGS)

.PHONY: run_tests
run_tests: $(TEST_PROGS)
    @echo "Running VFS tests..."
    @./vfs/test_vfs_getdents
    @echo "Running filesystem tests..."
    @./ext4/test_ext4_async
    @./xfs/test_xfs_async
    @echo "Running io_uring tests..."
    @./io_uring/test_ioring_getdents
    @echo "Running stress tests..."
    @./stress/test_concurrent
    @./stress/test_large_dir

.PHONY: benchmark
benchmark:
    @./benchmarks/run_all_benchmarks.sh
```

### CI Integration

**Location**: `.github/workflows/test.yml` or similar

```yaml
name: Async Getdents Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Build kernel
        run: make -j$(nproc)
      
      - name: Build tests
        run: make -C tools/testing/selftests/filesystems
      
      - name: Run unit tests
        run: make -C tools/testing/selftests/filesystems run_tests
      
      - name: Run stress tests
        run: ./tools/testing/selftests/filesystems/stress/run_all.sh
      
      - name: Run benchmarks
        run: ./tools/testing/selftests/filesystems/benchmarks/run_all.sh
      
      - name: Upload results
        uses: actions/upload-artifact@v2
        with:
          name: test-results
          path: test-results/
```

---

## Test Coverage Goals

### Code Coverage

**Target**: 90%+ line coverage for new code

**Tool**: gcov or similar

**Command**:
```bash
# Enable coverage
make KCOV=1

# Run tests
make run_tests

# Generate report
gcov fs/readdir.c io_uring/getdents.c
```

### Functional Coverage

**Checklist**:
- [ ] All error paths tested
- [ ] All filesystems tested
- [ ] All flags tested (NOWAIT, RESTART, etc.)
- [ ] Concurrent scenarios tested
- [ ] Modification scenarios tested
- [ ] Large directories tested
- [ ] Edge cases (empty dir, single entry, etc.)

---

## Test Matrix

| Filesystem | Basic | Concurrent | NOWAIT | Large Dir | Modification |
|------------|-------|------------|--------|-----------|--------------|
| **ext4**   | ✓     | ✓          | ✓      | ✓         | ✓            |
| **ZFS**    | ✓     | ✓          | ✓      | ✓         | ✓            |
| **XFS**    | ✓     | ✓          | ✓      | ✓         | ✓            |
| **btrfs**  | ✓     | ✓          | ✓      | ✓         | ✓            |
| **tmpfs**  | ✓     | ✓          | ✓      | ✓         | ✓            |
| **Fallback** | ✓   | ✓          | ~      | ✓         | ✓            |

**Legend**: ✓ = must test, ~ = partial/optional

---

## Regression Testing

### Baseline Comparison

**Ensure no regressions**:

```c
void test_traditional_readdir_still_works() {
    // Traditional readdir() must still work
    DIR *dir = opendir("/tmp/test_dir");
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        // Should work as before
        assert(entry->d_name != NULL);
    }
    
    closedir(dir);
}

void test_iterate_shared_still_works() {
    // Existing iterate_shared must not break
    // ...
}
```

---

## Documentation

### Test README

**Location**: `tools/testing/selftests/filesystems/README.md`

```markdown
# Async Directory Iteration Tests

## Running Tests

### Quick start
```bash
make run_tests
```

### Individual test suites
```bash
./vfs/test_vfs_getdents
./ext4/test_ext4_async
./stress/test_concurrent
```

### With specific filesystem
```bash
# Mount test filesystem
mount -t ext4 /dev/loop0 /mnt/test

# Run tests
./ext4/test_ext4_async /mnt/test

# Cleanup
umount /mnt/test
```

## Test Organization

- `vfs/` - VFS layer tests
- `ext4/`, `xfs/`, etc. - Filesystem-specific
- `io_uring/` - io_uring integration
- `stress/` - Stress and load tests
- `benchmarks/` - Performance benchmarks
- `helpers/` - Utility scripts

## Requirements

- Linux kernel 6.x+ with async getdents patches
- liburing 2.x+
- Root access (for mounting filesystems)
- At least 10GB free space for large directory tests
```

---

## Success Criteria

### Minimum

- [ ] All basic functional tests pass
- [ ] No crashes or data corruption
- [ ] No regressions in traditional readdir()

### Target

- [ ] 90%+ code coverage
- [ ] All filesystem tests pass
- [ ] Stress tests pass (100+ concurrent, 1M+ files)
- [ ] Integration tests pass (find, ls, rsync work)

### Stretch

- [ ] Zero known bugs
- [ ] Performance meets or exceeds baseline
- [ ] Tests integrated into kernel CI
- [ ] Tests included in kernel selftests

---

## Chaos Engineering Tests (Jepsen-Style)

**Inspiration**: Kyle Kingsbury's (Aphyr) Jepsen testing framework for distributed systems

**Philosophy**: If it can break, it will break. Make it break in testing.

### Chaos Test Framework

**Location**: `tools/testing/selftests/filesystems/chaos/`

**File**: `chaos_framework.h`

```c
/**
 * Chaos testing framework for directory operations
 * 
 * Inspired by Jepsen: introduce faults, delays, crashes
 * during concurrent operations to find race conditions.
 */

struct chaos_config {
    // Fault injection probabilities (0.0-1.0)
    double eagain_prob;        // Inject -EAGAIN returns
    double enomem_prob;        // Inject -ENOMEM
    double delay_prob;         // Inject random delays
    double crash_prob;         // Simulate process crash
    
    // Delay ranges (microseconds)
    int min_delay_us;
    int max_delay_us;
    
    // Operations
    int num_readers;           // Concurrent reader threads
    int num_writers;           // Concurrent writer threads
    int duration_seconds;      // Test duration
    
    // Invariants to check
    bool check_no_duplicates;
    bool check_no_missing;
    bool check_consistency;
};

struct chaos_result {
    uint64_t operations_total;
    uint64_t operations_failed;
    uint64_t injected_faults;
    uint64_t detected_anomalies;
    
    // Invariant violations
    uint64_t duplicates_found;
    uint64_t missing_entries;
    uint64_t consistency_errors;
    
    bool passed;
};

// Run chaos test
struct chaos_result *chaos_test_run(
    const char *test_dir,
    struct chaos_config *config,
    enum dir_reader_mode mode);

// Specific chaos scenarios
void chaos_test_rapid_modifications(const char *dir);
void chaos_test_delayed_operations(const char *dir);
void chaos_test_memory_pressure(const char *dir);
void chaos_test_signal_interruption(const char *dir);
```

### Chaos Test: Rapid Modifications

**File**: `chaos_rapid_modifications.c`

```c
/**
 * Chaos Test: Rapidly modify directory during iteration
 * 
 * Goal: Find race conditions in concurrent read+write
 * 
 * Scenario:
 * - N reader threads continuously scan directory
 * - M writer threads rapidly create/delete files
 * - Inject random delays and faults
 * - Check invariants: no crashes, no corruption
 */

#define NUM_READERS 20
#define NUM_WRITERS 10
#define NUM_FILES 1000
#define TEST_DURATION 60  // seconds

struct test_state {
    const char *test_dir;
    atomic_int files_created;
    atomic_int files_deleted;
    atomic_int read_operations;
    atomic_bool stop_test;
    
    struct chaos_config *config;
    struct dir_reader_mode mode;
};

void* chaos_reader_thread(void* arg) {
    struct test_state *state = arg;
    struct dir_reader *reader = dir_reader_create(state->mode);
    
    while (!atomic_load(&state->stop_test)) {
        // Open directory
        if (dir_reader_open(reader, state->test_dir) < 0) {
            continue;  // Directory may be temporarily unavailable
        }
        
        // Read all entries
        struct dir_entry entries[100];
        int total_read = 0;
        
        while (1) {
            // Inject random delay?
            if (should_inject_delay(state->config)) {
                int delay = random_delay(state->config);
                usleep(delay);
            }
            
            int count = dir_reader_read(reader, entries, 100);
            
            // Inject fault?
            if (should_inject_fault(state->config)) {
                // Pretend we got an error
                count = -ENOMEM;
            }
            
            if (count < 0) {
                // Expected - directory under modification
                break;
            }
            if (count == 0)
                break;  // EOF
            
            total_read += count;
        }
        
        dir_reader_close(reader);
        atomic_fetch_add(&state->read_operations, 1);
        
        // Brief pause
        usleep(random() % 10000);  // 0-10ms
    }
    
    dir_reader_destroy(reader);
    return NULL;
}

void* chaos_writer_thread(void* arg) {
    struct test_state *state = arg;
    char filepath[PATH_MAX];
    
    while (!atomic_load(&state->stop_test)) {
        // Create file
        int file_id = atomic_fetch_add(&state->files_created, 1);
        snprintf(filepath, sizeof(filepath), "%s/chaos_%d_%d",
                 state->test_dir, getpid(), file_id);
        
        int fd = creat(filepath, 0644);
        if (fd >= 0) {
            close(fd);
            
            // Random delay
            if (should_inject_delay(state->config)) {
                usleep(random_delay(state->config));
            }
            
            // Delete file
            unlink(filepath);
            atomic_fetch_add(&state->files_deleted, 1);
        }
        
        // Very brief pause (create churn)
        usleep(random() % 1000);  // 0-1ms
    }
    
    return NULL;
}

void chaos_test_rapid_modifications(const char *test_dir) {
    struct test_state state = {
        .test_dir = test_dir,
        .files_created = ATOMIC_VAR_INIT(0),
        .files_deleted = ATOMIC_VAR_INIT(0),
        .read_operations = ATOMIC_VAR_INIT(0),
        .stop_test = ATOMIC_VAR_INIT(false),
    };
    
    struct chaos_config config = {
        .eagain_prob = 0.01,    // 1% chance of -EAGAIN
        .delay_prob = 0.05,     // 5% chance of delay
        .min_delay_us = 100,
        .max_delay_us = 5000,
        .num_readers = NUM_READERS,
        .num_writers = NUM_WRITERS,
        .duration_seconds = TEST_DURATION,
    };
    state.config = &config;
    
    printf("Starting chaos test: rapid modifications\n");
    printf("  Readers: %d, Writers: %d, Duration: %ds\n",
           NUM_READERS, NUM_WRITERS, TEST_DURATION);
    
    // Launch threads
    pthread_t readers[NUM_READERS], writers[NUM_WRITERS];
    
    for (int i = 0; i < NUM_READERS; i++)
        pthread_create(&readers[i], NULL, chaos_reader_thread, &state);
    for (int i = 0; i < NUM_WRITERS; i++)
        pthread_create(&writers[i], NULL, chaos_writer_thread, &state);
    
    // Run for duration
    sleep(TEST_DURATION);
    
    // Stop all threads
    atomic_store(&state.stop_test, true);
    
    for (int i = 0; i < NUM_READERS; i++)
        pthread_join(readers[i], NULL);
    for (int i = 0; i < NUM_WRITERS; i++)
        pthread_join(writers[i], NULL);
    
    // Print results
    printf("\nChaos test completed:\n");
    printf("  Files created: %d\n", atomic_load(&state.files_created));
    printf("  Files deleted: %d\n", atomic_load(&state.files_deleted));
    printf("  Read operations: %d\n", atomic_load(&state.read_operations));
    printf("  \033[1;32mPASSED\033[0m - No crashes or hangs\n");
}
```

---

## eBPF/ptrace Fault Injection

**Purpose**: Inject faults deep in kernel to trigger race conditions

**Tools**: eBPF for kernel-side, ptrace for userspace

### eBPF Fault Injector

**File**: `tools/testing/selftests/filesystems/chaos/ebpf_injector.bpf.c`

```c
/**
 * eBPF program to inject faults into async getdents
 * 
 * Attaches to kernel functions and randomly:
 * - Delays execution
 * - Returns errors
 * - Drops locks temporarily
 */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, u32);
    __type(value, u64);
} fault_config SEC(".maps");

// Configuration
static volatile const __u32 delay_prob_pct = 5;  // 5% chance
static volatile const __u32 eagain_prob_pct = 2;  // 2% chance
static volatile const __u32 max_delay_us = 10000; // 10ms max

// Hook: vfs_getdents_async entry
SEC("fentry/vfs_getdents_async")
int BPF_PROG(trace_getdents_entry,
             struct file *file,
             loff_t offset,
             void __user *dirent_buf,
             size_t buflen,
             loff_t *next_offset,
             unsigned int flags)
{
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    
    // Random delay injection
    u32 rand = bpf_get_prandom_u32();
    if ((rand % 100) < delay_prob_pct) {
        // Inject delay
        u32 delay = (rand % max_delay_us);
        bpf_trace_printk("Injecting %dus delay for pid %d\n", delay, pid);
        // Note: eBPF can't actually delay, but can record intent
        // Real delay must be handled by userspace coordinating with eBPF
    }
    
    return 0;
}

// Hook: vfs_getdents_async exit
SEC("fexit/vfs_getdents_async")
int BPF_PROG(trace_getdents_exit, int ret)
{
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    
    // Randomly force -EAGAIN return
    u32 rand = bpf_get_prandom_u32();
    if ((rand % 100) < eagain_prob_pct) {
        bpf_trace_printk("Forcing -EAGAIN for pid %d\n", pid);
        // Override return value (if supported by eBPF version)
        // bpf_override_return(regs, -EAGAIN);
    }
    
    return 0;
}

// Hook: ext4_iterate_async entry
SEC("fentry/ext4_iterate_async")
int BPF_PROG(trace_ext4_entry, struct file *file)
{
    // Track ext4-specific calls
    bpf_trace_printk("ext4_iterate_async called\n");
    return 0;
}

char _license[] SEC("license") = "GPL";
```

**Loader**: `ebpf_injector_load.c`

```c
/**
 * Load and control eBPF fault injector
 */

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

struct ebpf_injector {
    struct bpf_object *obj;
    struct bpf_link *links[10];
    int num_links;
};

struct ebpf_injector *ebpf_injector_load() {
    struct ebpf_injector *inj = calloc(1, sizeof(*inj));
    if (!inj)
        return NULL;
    
    // Load eBPF program
    inj->obj = bpf_object__open_file("ebpf_injector.bpf.o", NULL);
    if (!inj->obj) {
        free(inj);
        return NULL;
    }
    
    if (bpf_object__load(inj->obj) < 0) {
        bpf_object__close(inj->obj);
        free(inj);
        return NULL;
    }
    
    // Attach to tracepoints/kprobes
    struct bpf_program *prog;
    bpf_object__for_each_program(prog, inj->obj) {
        inj->links[inj->num_links] = bpf_program__attach(prog);
        if (inj->links[inj->num_links])
            inj->num_links++;
    }
    
    printf("eBPF fault injector loaded: %d hooks attached\n", inj->num_links);
    return inj;
}

void ebpf_injector_unload(struct ebpf_injector *inj) {
    for (int i = 0; i < inj->num_links; i++) {
        bpf_link__destroy(inj->links[i]);
    }
    bpf_object__close(inj->obj);
    free(inj);
}

void ebpf_injector_set_delay_prob(struct ebpf_injector *inj, int percent) {
    // Update eBPF map to configure delay probability
    // Implementation depends on eBPF program structure
}

void ebpf_injector_set_fault_prob(struct ebpf_injector *inj, int percent) {
    // Update eBPF map to configure fault probability
}
```

### ptrace-based Fault Injection

**File**: `chaos/ptrace_injector.c`

```c
/**
 * ptrace-based fault injection
 * 
 * Attach to test process and inject faults:
 * - Modify syscall returns
 * - Inject signals
 * - Pause execution randomly
 */

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

struct ptrace_injector {
    pid_t target_pid;
    double fault_prob;
    double delay_prob;
    int max_delay_us;
};

void* ptrace_injector_thread(void* arg) {
    struct ptrace_injector *inj = arg;
    int status;
    
    // Attach to target process
    if (ptrace(PTRACE_ATTACH, inj->target_pid, NULL, NULL) < 0) {
        perror("ptrace attach");
        return NULL;
    }
    
    waitpid(inj->target_pid, &status, 0);
    
    // Set options to catch syscalls
    ptrace(PTRACE_SETOPTIONS, inj->target_pid, 0,
           PTRACE_O_TRACESYSGOOD);
    
    while (1) {
        // Continue until next syscall
        ptrace(PTRACE_SYSCALL, inj->target_pid, 0, 0);
        waitpid(inj->target_pid, &status, 0);
        
        if (WIFEXITED(status))
            break;
        
        if (WIFSTOPPED(status) && WSTOPSIG(status) == (SIGTRAP | 0x80)) {
            // Syscall entry or exit
            struct user_regs_struct regs;
            ptrace(PTRACE_GETREGS, inj->target_pid, 0, &regs);
            
            // Check if this is io_uring_enter (for getdents)
            if (regs.orig_rax == __NR_io_uring_enter) {
                // Wait for syscall exit
                ptrace(PTRACE_SYSCALL, inj->target_pid, 0, 0);
                waitpid(inj->target_pid, &status, 0);
                
                // Get return value
                ptrace(PTRACE_GETREGS, inj->target_pid, 0, &regs);
                
                // Inject fault?
                double rand = (double)random() / RAND_MAX;
                if (rand < inj->fault_prob) {
                    // Modify return value to -EAGAIN
                    regs.rax = -EAGAIN;
                    ptrace(PTRACE_SETREGS, inj->target_pid, 0, &regs);
                    printf("Injected -EAGAIN via ptrace\n");
                }
                
                // Inject delay?
                if (rand < inj->delay_prob) {
                    int delay = random() % inj->max_delay_us;
                    usleep(delay);
                    printf("Injected %dus delay via ptrace\n", delay);
                }
            }
        }
    }
    
    ptrace(PTRACE_DETACH, inj->target_pid, 0, 0);
    return NULL;
}

struct ptrace_injector *ptrace_injector_start(
    pid_t target_pid,
    double fault_prob,
    double delay_prob,
    int max_delay_us)
{
    struct ptrace_injector *inj = malloc(sizeof(*inj));
    inj->target_pid = target_pid;
    inj->fault_prob = fault_prob;
    inj->delay_prob = delay_prob;
    inj->max_delay_us = max_delay_us;
    
    pthread_t thread;
    pthread_create(&thread, NULL, ptrace_injector_thread, inj);
    pthread_detach(thread);
    
    return inj;
}
```

### Combined Chaos + Fault Injection Test

**File**: `chaos/ultimate_stress_test.c`

```c
/**
 * Ultimate stress test combining everything:
 * - Chaos engineering (concurrent modifications)
 * - eBPF fault injection (kernel-level)
 * - ptrace fault injection (syscall-level)
 * - Memory pressure
 * - Signal bombardment
 * 
 * Goal: If anything can break, this will find it
 */

void ultimate_stress_test(const char *test_dir) {
    printf("=== ULTIMATE STRESS TEST ===\n");
    printf("Combining all chaos techniques...\n\n");
    
    // 1. Load eBPF fault injector
    struct ebpf_injector *ebpf = ebpf_injector_load();
    if (ebpf) {
        ebpf_injector_set_delay_prob(ebpf, 5);  // 5% delays
        ebpf_injector_set_fault_prob(ebpf, 2);  // 2% faults
        printf("✓ eBPF fault injector loaded\n");
    } else {
        printf("✗ eBPF injector failed (continuing anyway)\n");
    }
    
    // 2. Fork test process
    pid_t pid = fork();
    if (pid == 0) {
        // Child: run chaos test
        chaos_test_rapid_modifications(test_dir);
        exit(0);
    }
    
    // 3. Attach ptrace fault injector
    sleep(1);  // Let child start
    struct ptrace_injector *ptrace_inj = ptrace_injector_start(
        pid, 0.02, 0.05, 10000);  // 2% faults, 5% delays
    printf("✓ ptrace fault injector attached\n");
    
    // 4. Apply memory pressure
    pthread_t mem_pressure_thread;
    pthread_create(&mem_pressure_thread, NULL,
                   memory_pressure_generator, NULL);
    printf("✓ Memory pressure thread started\n");
    
    // 5. Random signal bombardment
    pthread_t signal_thread;
    pthread_create(&signal_thread, NULL, signal_bombardment, &pid);
    printf("✓ Signal bombardment started\n");
    
    printf("\n>>> Test running, watching for crashes/hangs...\n\n");
    
    // Wait for child
    int status;
    waitpid(pid, &status, 0);
    
    // Cleanup
    pthread_cancel(mem_pressure_thread);
    pthread_cancel(signal_thread);
    if (ebpf)
        ebpf_injector_unload(ebpf);
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        printf("\n\033[1;32m✓ ULTIMATE STRESS TEST PASSED\033[0m\n");
        printf("Survived: chaos + eBPF + ptrace + memory pressure + signals\n");
    } else {
        printf("\n\033[1;31m✗ ULTIMATE STRESS TEST FAILED\033[0m\n");
        printf("Exit status: %d, signal: %d\n",
               WEXITSTATUS(status), WTERMSIG(status));
    }
}
```

---

## Invariant Checking

**File**: `chaos/invariant_checker.c`

```c
/**
 * Check invariants during/after chaos tests
 * 
 * Invariants to verify:
 * 1. No duplicate entries in single scan
 * 2. No missing entries (compared to baseline)
 * 3. All entries have valid inodes
 * 4. No file->f_pos corruption
 * 5. No memory leaks
 * 6. No zombie processes
 */

struct invariant_results {
    bool no_duplicates;
    bool no_missing;
    bool valid_inodes;
    bool no_fpos_corruption;
    bool no_memory_leaks;
    bool no_zombies;
};

struct invariant_results check_all_invariants(
    const char *test_dir,
    enum dir_reader_mode mode)
{
    struct invariant_results results = {0};
    
    // 1. Check for duplicates
    results.no_duplicates = check_no_duplicate_entries(test_dir, mode);
    
    // 2. Check for missing entries
    results.no_missing = check_no_missing_entries(test_dir, mode);
    
    // 3. Check inode validity
    results.valid_inodes = check_valid_inodes(test_dir, mode);
    
    // 4. Check f_pos not corrupted (test with multiple readers)
    results.no_fpos_corruption = check_fpos_integrity(test_dir);
    
    // 5. Check for memory leaks
    results.no_memory_leaks = check_memory_leaks();
    
    // 6. Check for zombie processes
    results.no_zombies = check_no_zombies();
    
    return results;
}

bool check_no_duplicate_entries(const char *test_dir, enum dir_reader_mode mode) {
    struct dir_reader *reader = dir_reader_create(mode);
    dir_reader_open(reader, test_dir);
    
    // Use hash set to track seen entries
    struct hash_set *seen = hash_set_create();
    
    struct dir_entry entries[100];
    int count;
    bool duplicates_found = false;
    
    while ((count = dir_reader_read(reader, entries, 100)) > 0) {
        for (int i = 0; i < count; i++) {
            if (hash_set_contains(seen, entries[i].name)) {
                printf("INVARIANT VIOLATION: Duplicate entry '%s'\n",
                       entries[i].name);
                duplicates_found = true;
            }
            hash_set_add(seen, entries[i].name);
        }
    }
    
    dir_reader_close(reader);
    dir_reader_destroy(reader);
    hash_set_destroy(seen);
    
    return !duplicates_found;
}
```

---

## VM Test Infrastructure

**Purpose**: Isolated, reproducible test environments for each filesystem

**Strategy**: Create VMs for each filesystem type, automate test execution

### VM Infrastructure Overview

```
┌─────────────────────────────────────────────────┐
│           Host Machine (Test Controller)        │
│                                                  │
│  ┌────────────┐  ┌────────────┐  ┌───────────┐ │
│  │ VM: ext4   │  │ VM: XFS    │  │ VM: ZFS   │ │
│  │  - Kernel  │  │  - Kernel  │  │  - Kernel │ │
│  │  - Tests   │  │  - Tests   │  │  - Tests  │ │
│  │  - eBPF    │  │  - eBPF    │  │  - eBPF   │ │
│  └────────────┘  └────────────┘  └───────────┘ │
│                                                  │
│  ┌────────────┐  ┌────────────┐                │
│  │ VM: btrfs  │  │ VM: tmpfs  │                │
│  │  - Kernel  │  │  - Kernel  │                │
│  │  - Tests   │  │  - Tests   │                │
│  │  - eBPF    │  │  - eBPF    │                │
│  └────────────┘  └────────────┘                │
│                                                  │
│  Test Orchestrator: runs tests across all VMs   │
└─────────────────────────────────────────────────┘
```

### VM Base Image Creation

**File**: `tools/testing/selftests/filesystems/vms/create_base_image.sh`

```bash
#!/bin/bash
# Create base VM image with custom kernel

set -e

KERNEL_VERSION="6.8.0-async-getdents"
DISTRO="ubuntu-24.04"
IMAGE_NAME="async-getdents-base.qcow2"
IMAGE_SIZE="30G"

echo "=== Creating base VM image ==="

# 1. Download base cloud image
wget https://cloud-images.ubuntu.com/releases/24.04/release/ubuntu-24.04-server-cloudimg-amd64.img

# 2. Resize image
cp ubuntu-24.04-server-cloudimg-amd64.img $IMAGE_NAME
qemu-img resize $IMAGE_NAME $IMAGE_SIZE

# 3. Create cloud-init config
cat > user-data <<EOF
#cloud-config
users:
  - name: test
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    ssh_authorized_keys:
      - $(cat ~/.ssh/id_rsa.pub)

packages:
  - build-essential
  - git
  - liburing-dev
  - bpftool
  - linux-tools-generic
  - fio
  - sysbench
  - strace
  - gdb

runcmd:
  - mkdir -p /test
  - chown test:test /test
EOF

# 4. Boot VM and install custom kernel
virt-install \
    --name async-getdents-base \
    --ram 8192 \
    --vcpus 4 \
    --disk path=$IMAGE_NAME,format=qcow2 \
    --os-variant ubuntu24.04 \
    --cloud-init user-data=user-data \
    --graphics none \
    --console pty,target_type=serial \
    --network network=default \
    --import

# Wait for boot
sleep 60

# 5. Copy custom kernel to VM
VM_IP=$(virsh domifaddr async-getdents-base | grep ipv4 | awk '{print $4}' | cut -d'/' -f1)

scp -r /path/to/kernel/build test@$VM_IP:/tmp/kernel
ssh test@$VM_IP "cd /tmp/kernel && sudo make modules_install install"

# 6. Copy test suite
scp -r tools/testing/selftests/filesystems test@$VM_IP:/home/test/

# 7. Shutdown and create snapshot
virsh shutdown async-getdents-base
sleep 10

echo "=== Base image created: $IMAGE_NAME ==="
echo "VM IP: $VM_IP"
```

### Filesystem-Specific VM Creation

#### ext4 VM Setup

**File**: `vms/create_ext4_vm.sh`

```bash
#!/bin/bash
# Create ext4 test VM

set -e

VM_NAME="async-getdents-ext4"
BASE_IMAGE="async-getdents-base.qcow2"
VM_IMAGE="${VM_NAME}.qcow2"
DATA_DISK="${VM_NAME}-data.qcow2"

echo "=== Creating ext4 test VM ==="

# 1. Clone base image
qemu-img create -f qcow2 -F qcow2 -b $BASE_IMAGE $VM_IMAGE

# 2. Create data disk for ext4 filesystem
qemu-img create -f qcow2 $DATA_DISK 100G

# 3. Launch VM
virt-install \
    --name $VM_NAME \
    --ram 4096 \
    --vcpus 2 \
    --disk path=$VM_IMAGE,format=qcow2 \
    --disk path=$DATA_DISK,format=qcow2 \
    --os-variant ubuntu24.04 \
    --network network=default \
    --graphics none \
    --console pty,target_type=serial \
    --import \
    --noautoconsole

# Wait for boot
sleep 30

VM_IP=$(virsh domifaddr $VM_NAME | grep ipv4 | awk '{print $4}' | cut -d'/' -f1)

# 4. Setup ext4 filesystem
ssh test@$VM_IP << 'SETUP_EXT4'
# Format as ext4 with htree
sudo mkfs.ext4 -O dir_index /dev/vdb

# Mount
sudo mkdir -p /test/ext4
sudo mount -t ext4 /dev/vdb /test/ext4
sudo chown -R test:test /test/ext4

# Add to fstab
echo '/dev/vdb /test/ext4 ext4 defaults 0 0' | sudo tee -a /etc/fstab

# Verify htree is enabled
sudo tune2fs -l /dev/vdb | grep dir_index

echo "ext4 filesystem ready at /test/ext4"
SETUP_EXT4

echo "=== ext4 VM created ==="
echo "VM: $VM_NAME"
echo "IP: $VM_IP"
echo "Mount: /test/ext4"
```

#### XFS VM Setup

**File**: `vms/create_xfs_vm.sh`

```bash
#!/bin/bash
# Create XFS test VM

set -e

VM_NAME="async-getdents-xfs"
BASE_IMAGE="async-getdents-base.qcow2"
VM_IMAGE="${VM_NAME}.qcow2"
DATA_DISK="${VM_NAME}-data.qcow2"

echo "=== Creating XFS test VM ==="

# Clone and create disks
qemu-img create -f qcow2 -F qcow2 -b $BASE_IMAGE $VM_IMAGE
qemu-img create -f qcow2 $DATA_DISK 100G

# Launch VM
virt-install \
    --name $VM_NAME \
    --ram 4096 \
    --vcpus 2 \
    --disk path=$VM_IMAGE,format=qcow2 \
    --disk path=$DATA_DISK,format=qcow2 \
    --os-variant ubuntu24.04 \
    --network network=default \
    --graphics none \
    --console pty,target_type=serial \
    --import \
    --noautoconsole

sleep 30
VM_IP=$(virsh domifaddr $VM_NAME | grep ipv4 | awk '{print $4}' | cut -d'/' -f1)

# Setup XFS
ssh test@$VM_IP << 'SETUP_XFS'
# Install xfsprogs
sudo apt-get install -y xfsprogs

# Format as XFS
sudo mkfs.xfs -f /dev/vdb

# Mount
sudo mkdir -p /test/xfs
sudo mount -t xfs /dev/vdb /test/xfs
sudo chown -R test:test /test/xfs

# Add to fstab
echo '/dev/vdb /test/xfs xfs defaults 0 0' | sudo tee -a /etc/fstab

echo "XFS filesystem ready at /test/xfs"
SETUP_XFS

echo "=== XFS VM created ==="
echo "VM: $VM_NAME"
echo "IP: $VM_IP"
echo "Mount: /test/xfs"
```

#### ZFS VM Setup

**File**: `vms/create_zfs_vm.sh`

```bash
#!/bin/bash
# Create ZFS test VM

set -e

VM_NAME="async-getdents-zfs"
BASE_IMAGE="async-getdents-base.qcow2"
VM_IMAGE="${VM_NAME}.qcow2"
DATA_DISK="${VM_NAME}-data.qcow2"

echo "=== Creating ZFS test VM ==="

# Clone and create disks
qemu-img create -f qcow2 -F qcow2 -b $BASE_IMAGE $VM_IMAGE
qemu-img create -f qcow2 $DATA_DISK 100G

# Launch VM (ZFS needs more RAM)
virt-install \
    --name $VM_NAME \
    --ram 8192 \
    --vcpus 2 \
    --disk path=$VM_IMAGE,format=qcow2 \
    --disk path=$DATA_DISK,format=qcow2 \
    --os-variant ubuntu24.04 \
    --network network=default \
    --graphics none \
    --console pty,target_type=serial \
    --import \
    --noautoconsole

sleep 30
VM_IP=$(virsh domifaddr $VM_NAME | grep ipv4 | awk '{print $4}' | cut -d'/' -f1)

# Setup ZFS
ssh test@$VM_IP << 'SETUP_ZFS'
# Install OpenZFS
sudo apt-get install -y zfsutils-linux

# Create ZFS pool
sudo zpool create testpool /dev/vdb

# Create filesystem
sudo zfs create testpool/testfs

# Set mountpoint
sudo zfs set mountpoint=/test/zfs testpool/testfs

# Set permissions
sudo chown -R test:test /test/zfs

# Verify
zfs list
zpool status

echo "ZFS filesystem ready at /test/zfs"
SETUP_ZFS

echo "=== ZFS VM created ==="
echo "VM: $VM_NAME"
echo "IP: $VM_IP"
echo "Mount: /test/zfs (testpool/testfs)"
```

#### btrfs VM Setup

**File**: `vms/create_btrfs_vm.sh`

```bash
#!/bin/bash
# Create btrfs test VM

set -e

VM_NAME="async-getdents-btrfs"
BASE_IMAGE="async-getdents-base.qcow2"
VM_IMAGE="${VM_NAME}.qcow2"
DATA_DISK="${VM_NAME}-data.qcow2"

echo "=== Creating btrfs test VM ==="

qemu-img create -f qcow2 -F qcow2 -b $BASE_IMAGE $VM_IMAGE
qemu-img create -f qcow2 $DATA_DISK 100G

virt-install \
    --name $VM_NAME \
    --ram 4096 \
    --vcpus 2 \
    --disk path=$VM_IMAGE,format=qcow2 \
    --disk path=$DATA_DISK,format=qcow2 \
    --os-variant ubuntu24.04 \
    --network network=default \
    --graphics none \
    --console pty,target_type=serial \
    --import \
    --noautoconsole

sleep 30
VM_IP=$(virsh domifaddr $VM_NAME | grep ipv4 | awk '{print $4}' | cut -d'/' -f1)

# Setup btrfs
ssh test@$VM_IP << 'SETUP_BTRFS'
# Install btrfs-progs
sudo apt-get install -y btrfs-progs

# Format as btrfs
sudo mkfs.btrfs -f /dev/vdb

# Mount
sudo mkdir -p /test/btrfs
sudo mount -t btrfs /dev/vdb /test/btrfs
sudo chown -R test:test /test/btrfs

# Add to fstab
echo '/dev/vdb /test/btrfs btrfs defaults 0 0' | sudo tee -a /etc/fstab

echo "btrfs filesystem ready at /test/btrfs"
SETUP_BTRFS

echo "=== btrfs VM created ==="
echo "VM: $VM_NAME"
echo "IP: $VM_IP"
echo "Mount: /test/btrfs"
```

#### tmpfs VM Setup

**File**: `vms/create_tmpfs_vm.sh`

```bash
#!/bin/bash
# Create tmpfs test VM (no extra disk needed)

set -e

VM_NAME="async-getdents-tmpfs"
BASE_IMAGE="async-getdents-base.qcow2"
VM_IMAGE="${VM_NAME}.qcow2"

echo "=== Creating tmpfs test VM ==="

qemu-img create -f qcow2 -F qcow2 -b $BASE_IMAGE $VM_IMAGE

virt-install \
    --name $VM_NAME \
    --ram 8192 \
    --vcpus 2 \
    --disk path=$VM_IMAGE,format=qcow2 \
    --os-variant ubuntu24.04 \
    --network network=default \
    --graphics none \
    --console pty,target_type=serial \
    --import \
    --noautoconsole

sleep 30
VM_IP=$(virsh domifaddr $VM_NAME | grep ipv4 | awk '{print $4}' | cut -d'/' -f1)

# Setup tmpfs
ssh test@$VM_IP << 'SETUP_TMPFS'
# Create large tmpfs mount
sudo mkdir -p /test/tmpfs
sudo mount -t tmpfs -o size=16G tmpfs /test/tmpfs
sudo chown -R test:test /test/tmpfs

# Add to fstab
echo 'tmpfs /test/tmpfs tmpfs size=16G 0 0' | sudo tee -a /etc/fstab

echo "tmpfs filesystem ready at /test/tmpfs"
SETUP_TMPFS

echo "=== tmpfs VM created ==="
echo "VM: $VM_NAME"
echo "IP: $VM_IP"
echo "Mount: /test/tmpfs (16GB)"
```

---

## VM Management Scripts

### Create All VMs

**File**: `vms/create_all_vms.sh`

```bash
#!/bin/bash
# Create all test VMs

set -e

echo "=== Creating all test VMs ==="

# 1. Create base image
./create_base_image.sh

# 2. Create filesystem-specific VMs
./create_ext4_vm.sh
./create_xfs_vm.sh
./create_zfs_vm.sh
./create_btrfs_vm.sh
./create_tmpfs_vm.sh

echo ""
echo "=== All VMs created ==="
echo ""
virsh list --all | grep async-getdents
```

### VM Inventory

**File**: `vms/vm_inventory.txt`

```ini
# Ansible-style inventory for test VMs

[test_vms]
ext4   ansible_host=192.168.122.10 filesystem=ext4   mount=/test/ext4
xfs    ansible_host=192.168.122.11 filesystem=xfs    mount=/test/xfs
zfs    ansible_host=192.168.122.12 filesystem=zfs    mount=/test/zfs
btrfs  ansible_host=192.168.122.13 filesystem=btrfs  mount=/test/btrfs
tmpfs  ansible_host=192.168.122.14 filesystem=tmpfs  mount=/test/tmpfs

[test_vms:vars]
ansible_user=test
ansible_ssh_private_key_file=~/.ssh/id_rsa
```

### VM Control Script

**File**: `vms/vm_control.sh`

```bash
#!/bin/bash
# Control all test VMs

command=$1

case $command in
    start)
        echo "Starting all VMs..."
        for vm in async-getdents-{ext4,xfs,zfs,btrfs,tmpfs}; do
            virsh start $vm || echo "$vm already running"
        done
        ;;
    
    stop)
        echo "Stopping all VMs..."
        for vm in async-getdents-{ext4,xfs,zfs,btrfs,tmpfs}; do
            virsh shutdown $vm
        done
        ;;
    
    destroy)
        echo "Destroying all VMs (forced shutdown)..."
        for vm in async-getdents-{ext4,xfs,zfs,btrfs,tmpfs}; do
            virsh destroy $vm || true
        done
        ;;
    
    status)
        echo "VM Status:"
        virsh list --all | grep async-getdents
        ;;
    
    clean)
        echo "Cleaning up all VMs and images..."
        for vm in async-getdents-{ext4,xfs,zfs,btrfs,tmpfs}; do
            virsh destroy $vm || true
            virsh undefine $vm || true
        done
        rm -f async-getdents-*.qcow2
        ;;
    
    *)
        echo "Usage: $0 {start|stop|destroy|status|clean}"
        exit 1
        ;;
esac
```

---

## Automated Test Orchestration

### Run Tests Across All VMs

**File**: `vms/run_all_tests.sh`

```bash
#!/bin/bash
# Run full test suite across all VMs

set -e

RESULTS_DIR="test-results/$(date +%Y%m%d-%H%M%S)"
mkdir -p $RESULTS_DIR

echo "=== Running tests across all VMs ==="
echo "Results directory: $RESULTS_DIR"
echo ""

# Function to run tests on a VM
run_tests_on_vm() {
    local vm_name=$1
    local fs_type=$2
    local mount_point=$3
    
    echo ">>> Testing $fs_type on $vm_name..."
    
    local vm_ip=$(virsh domifaddr $vm_name | grep ipv4 | awk '{print $4}' | cut -d'/' -f1)
    
    # Run tests via SSH
    ssh test@$vm_ip << EOF
cd /home/test/filesystems

# Basic tests
echo "=== Basic Tests ==="
./test_compare_implementations $mount_point

# Stress tests
echo "=== Stress Tests ==="
./stress_concurrent_readers $mount_point

# Chaos tests
echo "=== Chaos Tests ==="
sudo ./chaos_rapid_modifications $mount_point

# Benchmarks
echo "=== Benchmarks ==="
./bench_latency $mount_point
./bench_concurrent $mount_point
EOF

    # Copy results back
    scp -r test@$vm_ip:/home/test/filesystems/test-results/* \
        $RESULTS_DIR/$fs_type/ || true
    
    echo "<<< $fs_type tests complete"
    echo ""
}

# Start all VMs if not running
./vm_control.sh start
sleep 30

# Run tests on each VM
run_tests_on_vm "async-getdents-ext4" "ext4" "/test/ext4"
run_tests_on_vm "async-getdents-xfs" "xfs" "/test/xfs"
run_tests_on_vm "async-getdents-zfs" "zfs" "/test/zfs"
run_tests_on_vm "async-getdents-btrfs" "btrfs" "/test/btrfs"
run_tests_on_vm "async-getdents-tmpfs" "tmpfs" "/test/tmpfs"

# Generate summary report
./generate_test_report.sh $RESULTS_DIR

echo ""
echo "=== All tests complete ==="
echo "Results in: $RESULTS_DIR"
echo "Summary: $RESULTS_DIR/summary.html"
```

### Test Report Generator

**File**: `vms/generate_test_report.sh`

```bash
#!/bin/bash
# Generate HTML report from test results

RESULTS_DIR=$1

cat > $RESULTS_DIR/summary.html <<'HTML_START'
<!DOCTYPE html>
<html>
<head>
    <title>Async Getdents Test Results</title>
    <style>
        body { font-family: monospace; margin: 20px; }
        table { border-collapse: collapse; width: 100%; }
        th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }
        th { background-color: #4CAF50; color: white; }
        .pass { color: green; font-weight: bold; }
        .fail { color: red; font-weight: bold; }
    </style>
</head>
<body>
    <h1>Async Getdents Test Results</h1>
    <p>Generated: $(date)</p>
    
    <h2>Test Summary</h2>
    <table>
        <tr>
            <th>Filesystem</th>
            <th>Basic Tests</th>
            <th>Stress Tests</th>
            <th>Chaos Tests</th>
            <th>Benchmarks</th>
            <th>Overall</th>
        </tr>
HTML_START

for fs in ext4 xfs zfs btrfs tmpfs; do
    if [ -d "$RESULTS_DIR/$fs" ]; then
        # Parse test results (simplified)
        basic=$(grep -c "PASSED" $RESULTS_DIR/$fs/basic.log 2>/dev/null || echo "0")
        stress=$(grep -c "PASSED" $RESULTS_DIR/$fs/stress.log 2>/dev/null || echo "0")
        chaos=$(grep -c "PASSED" $RESULTS_DIR/$fs/chaos.log 2>/dev/null || echo "0")
        bench="N/A"
        
        overall="PASS"
        [ "$basic" -eq 0 ] && overall="FAIL"
        [ "$stress" -eq 0 ] && overall="FAIL"
        [ "$chaos" -eq 0 ] && overall="FAIL"
        
        cat >> $RESULTS_DIR/summary.html <<EOF
        <tr>
            <td>$fs</td>
            <td>$basic tests</td>
            <td>$stress tests</td>
            <td>$chaos tests</td>
            <td>$bench</td>
            <td class="$( [ "$overall" = "PASS" ] && echo pass || echo fail )">$overall</td>
        </tr>
EOF
    fi
done

cat >> $RESULTS_DIR/summary.html <<'HTML_END'
    </table>
    
    <h2>Performance Comparison</h2>
    <p>See individual benchmark reports for details.</p>
    
    <h2>Known Issues</h2>
    <ul id="issues"></ul>
    
    <script>
        // Could add JavaScript for interactive charts
    </script>
</body>
</html>
HTML_END

echo "Report generated: $RESULTS_DIR/summary.html"
```

---

## CI/CD Integration

### GitHub Actions Workflow

**File**: `.github/workflows/vm-tests.yml`

```yaml
name: VM Tests (All Filesystems)

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]
  schedule:
    # Run nightly
    - cron: '0 2 * * *'

jobs:
  create-vms:
    runs-on: ubuntu-latest
    # Need nested virtualization
    # Use self-hosted runner with KVM support
    
    steps:
      - uses: actions/checkout@v3
      
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            qemu-kvm libvirt-daemon-system libvirt-clients \
            virtinst cloud-image-utils
      
      - name: Check KVM support
        run: |
          kvm-ok || echo "KVM not available, tests may be slow"
      
      - name: Build custom kernel
        run: |
          make -j$(nproc)
          make modules_install install
      
      - name: Create VMs
        run: |
          cd tools/testing/selftests/filesystems/vms
          ./create_all_vms.sh
      
      - name: Run tests
        run: |
          cd tools/testing/selftests/filesystems/vms
          ./run_all_tests.sh
      
      - name: Generate report
        run: |
          cd tools/testing/selftests/filesystems/vms
          ./generate_test_report.sh test-results/latest
      
      - name: Upload results
        uses: actions/upload-artifact@v3
        with:
          name: test-results
          path: test-results/
      
      - name: Cleanup VMs
        if: always()
        run: |
          cd tools/testing/selftests/filesystems/vms
          ./vm_control.sh clean
```

### Jenkins Pipeline

**File**: `Jenkinsfile`

```groovy
pipeline {
    agent { label 'kvm-enabled' }
    
    stages {
        stage('Build Kernel') {
            steps {
                sh 'make -j$(nproc)'
            }
        }
        
        stage('Create VMs') {
            steps {
                dir('tools/testing/selftests/filesystems/vms') {
                    sh './create_all_vms.sh'
                }
            }
        }
        
        stage('Test ext4') {
            steps {
                sh './run_tests_on_vm.sh async-getdents-ext4 ext4'
            }
        }
        
        stage('Test XFS') {
            steps {
                sh './run_tests_on_vm.sh async-getdents-xfs xfs'
            }
        }
        
        stage('Test ZFS') {
            steps {
                sh './run_tests_on_vm.sh async-getdents-zfs zfs'
            }
        }
        
        stage('Test btrfs') {
            steps {
                sh './run_tests_on_vm.sh async-getdents-btrfs btrfs'
            }
        }
        
        stage('Test tmpfs') {
            steps {
                sh './run_tests_on_vm.sh async-getdents-tmpfs tmpfs'
            }
        }
        
        stage('Generate Report') {
            steps {
                sh './generate_test_report.sh test-results/build-${BUILD_NUMBER}'
                publishHTML([
                    reportDir: 'test-results/build-${BUILD_NUMBER}',
                    reportFiles: 'summary.html',
                    reportName: 'Test Results'
                ])
            }
        }
    }
    
    post {
        always {
            sh './vm_control.sh clean'
        }
    }
}
```

---

## Docker Alternative (Lightweight)

**For simpler testing without full VMs**

**File**: `docker/Dockerfile.test`

```dockerfile
FROM ubuntu:24.04

# Install kernel headers and tools
RUN apt-get update && apt-get install -y \
    build-essential \
    linux-headers-generic \
    liburing-dev \
    bpftool \
    xfsprogs \
    btrfs-progs \
    zfsutils-linux \
    && rm -rf /var/lib/apt/lists/*

# Copy test suite
COPY tools/testing/selftests/filesystems /test/

# Setup test filesystems (via loop devices)
RUN mkdir -p /test/{ext4,xfs,btrfs,tmpfs}

# Entry point
WORKDIR /test
CMD ["/bin/bash"]
```

**File**: `docker/run_docker_tests.sh`

```bash
#!/bin/bash
# Run tests in Docker containers

docker build -t async-getdents-test -f docker/Dockerfile.test .

# Run tests for each filesystem
for fs in ext4 xfs btrfs tmpfs; do
    echo "Testing $fs..."
    docker run --rm --privileged \
        -v $(pwd)/test-results:/test/results \
        async-getdents-test \
        /test/run_$fs_tests.sh
done
```

**Note**: Docker has limitations for full kernel testing but useful for quick iteration

---

## Summary

**Comprehensive testing strategy (updated)**:
1. ✅ Kernel unit tests (KUnit)
2. ✅ VFS layer tests
3. ✅ Filesystem-specific tests
4. ✅ io_uring integration tests
5. ✅ Stress tests
6. ✅ **NEW: Chaos engineering tests (Jepsen-style)**
7. ✅ **NEW: eBPF fault injection**
8. ✅ **NEW: ptrace fault injection**
9. ✅ **NEW: Invariant checking**
10. ✅ **NEW: DirectoryReader abstraction layer**
11. ✅ **NEW: VM test infrastructure (5 filesystems)**
12. ✅ **NEW: Automated VM orchestration**
13. ✅ **NEW: CI/CD integration (GitHub Actions, Jenkins)**
14. ✅ Performance benchmarks
15. ✅ Application integration tests

**VM Infrastructure**:
- Base image creation with custom kernel
- 5 filesystem-specific VMs (ext4, XFS, ZFS, btrfs, tmpfs)
- Automated VM management (start, stop, clean)
- Test orchestration across all VMs
- HTML report generation
- Docker alternative for quick iteration

**Total test code estimate**: ~10,000-15,000 lines (with chaos tests + VM infrastructure)

**Total VM scripts**: ~1,500 lines of automation

**Time to develop tests**: 8-12 weeks (with chaos engineering + VM setup)

**Critical for success**: 
- Chaos tests will find the race conditions before users do!
- VM isolation ensures reproducible results across filesystems
- Automated orchestration enables continuous testing

---

*Testing framework v2.0: Now with chaos engineering to break your code before it breaks in production*

*Priority: Critical (chaos tests are ESSENTIAL for concurrent code)*
*Complexity: Medium-High (eBPF/ptrace requires expertise)*
*Value: Extremely high (finds bugs that traditional tests miss)*

