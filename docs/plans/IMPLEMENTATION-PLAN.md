# RUDRA: Chaos Testing Framework & VM Infrastructure Implementation Plan

*Framework: Standalone Testing for Linux Kernel VFS Changes*
*Timeline: 8-12 weeks*
*Date: October 10, 2025*

## Overview

**RUDRA** is a standalone chaos testing framework designed to test Linux kernel VFS changes (async getdents, io_uring operations, etc.) using Jepsen-inspired techniques.

**What this framework does**:
- Tests kernel VFS changes in isolation (VMs with custom kernels)
- Applies chaos engineering to find race conditions
- Uses eBPF/ptrace for deep fault injection
- Validates across multiple filesystems

**What this framework is NOT**:
- Not part of the Linux kernel tree
- Not kernel code itself
- Not integrated into kernel CI (though it could be)

This is a **step-by-step implementation plan** for building:
1. DirectoryReader abstraction layer (C library)
2. Chaos testing framework (Jepsen-style)
3. eBPF fault injection (primary mechanism for kernel-level faults)
4. VM infrastructure for 5 filesystems
5. Test data generation helpers
6. Automated orchestration (Bazel + shell scripts)

**Build system**: Bazel (hermetic, reproducible)

**Fault injection strategy**: eBPF only. While ptrace could theoretically be used for fault injection, eBPF is strictly superior:
- **Lower overhead**: No process stops/starts (ptrace requires SIGSTOP/waitpid loops)
- **Kernel-level hooks**: Can intercept at any kernel function (not just syscall boundaries)
- **Production-like**: Tests run at normal speed with minimal perturbation
- **Modern**: Better tooling, wider kernel support (5.10+)
- **Scalable**: Can inject faults across all VMs simultaneously

**Every checkbox is a discrete task.** Complete them in order for a working test infrastructure.

---

## ⚠️ IMPORTANT: Read First

### Option 1: Tech De-Risking (RECOMMENDED) ⭐

**Start here if**: You want to validate eBPF fault injection works before committing to full implementation

**→ [Tech De-Risking Plan](TECH-DERISKING-PLAN.md)** - 2-3 week focused proof of concept

**What it does**:
- Builds minimal DirectoryReader (just classic readdir)
- Creates simple chaos test
- Implements basic eBPF pause injection
- **Proves the approach works** by finding a race condition
- No VMs needed (uses local filesystem)
- ~500 lines of code vs ~15,000 for full implementation

**After de-risking succeeds**: Return here and continue with full implementation

---

### Option 2: Full Implementation (This Document)

**Start here if**: You're confident in eBPF approach and ready to commit 12-16 weeks

**Prerequisites**:
1. Read [`RISKS-AND-OPEN-QUESTIONS.md`](RISKS-AND-OPEN-QUESTIONS.md) first
2. Ensure you have:
   - 32GB+ RAM ✅ (confirmed)
   - 500GB+ disk ✅ (confirmed)
   - Kernel source tree with patches
   - 12-16 weeks available
3. Answer critical questions:
   - Which kernel version are you testing?
   - Does IORING_OP_GETDENTS exist in your kernel?
   - MVP scope: 3 or 5 filesystems?
   - Full-time or part-time effort?

**Then**: Follow the checkboxes below sequentially

---

## Phase 0: Prerequisites & Environment Setup

**Timeline**: 1-2 days

### 0.1 Host Machine Setup

- [ ] Verify Linux host (Ubuntu 22.04+ or similar)
- [ ] Check CPU has virtualization support: `egrep -c '(vmx|svm)' /proc/cpuinfo`
- [ ] Verify KVM enabled: `kvm-ok` (should say "KVM acceleration can be used")
- [ ] Check available disk space: `df -h` (need 500GB+ recommended)
- [ ] Check available RAM: `free -h` (need 32GB+ recommended)
- [ ] Verify user in kvm and libvirt groups: `groups | grep -E 'kvm|libvirt'`
  - [ ] If not, add user: `sudo usermod -a -G kvm,libvirt $USER`
  - [ ] Log out and back in for group changes to take effect

### 0.2 Install Required Packages

**Note**: RUDRA uses **QEMU/KVM** (hardware-accelerated virtualization) managed by **libvirt**.

- [ ] Update package list: `sudo apt-get update`
- [ ] Install QEMU/KVM and libvirt:
  ```bash
  sudo apt-get install -y \
    qemu-kvm \
    libvirt-daemon-system \
    libvirt-clients \
    virtinst \
    bridge-utils \
    virt-manager \
    cloud-image-utils
  ```
  - **qemu-kvm**: Virtual machine emulator with KVM acceleration
  - **libvirt**: VM management layer (provides virsh, virt-install)
  - **cloud-image-utils**: For preparing cloud-init images
- [ ] Install development tools:
  ```bash
  sudo apt-get install -y \
    build-essential \
    git \
    liburing-dev \
    libbpf-dev \
    bpftool \
    linux-tools-generic \
    linux-tools-common \
    clang \
    llvm
  ```
- [ ] Install testing tools:
  ```bash
  sudo apt-get install -y \
    fio \
    sysbench \
    strace \
    gdb \
    valgrind
  ```
- [ ] Start and enable libvirt: `sudo systemctl enable --now libvirtd`
- [ ] Verify libvirt running: `sudo systemctl status libvirtd`

### 0.3 Setup SSH Keys

- [ ] Check for SSH key: `ls ~/.ssh/id_rsa.pub`
- [ ] If none, generate: `ssh-keygen -t rsa -b 4096 -C "test@async-getdents"`
- [ ] Add key to ssh-agent: `eval $(ssh-agent) && ssh-add ~/.ssh/id_rsa`

### 0.4 Verify Project Directory Structure

The RUDRA project structure should already exist:

```bash
cd /home/jmalicki/src/rudra
tree -L 1
```

Expected structure:
```
rudra/
├── BUILD.bazel          # Root build file
├── WORKSPACE            # Bazel workspace
├── common/              # DirectoryReader abstraction
│   ├── BUILD.bazel
│   └── README.md
├── chaos/               # Chaos testing framework
│   ├── BUILD.bazel
│   └── README.md
├── vms/                 # VM infrastructure scripts
│   └── README.md
├── benchmarks/          # Performance tests
│   └── BUILD.bazel
├── helpers/             # Test data generation
├── test-results/        # Test output
├── docs/                # Documentation (this file)
└── README.md            # Project README
```

- [ ] Verify all directories exist: `ls -la`
- [ ] If missing, create: `mkdir -p common chaos vms benchmarks helpers test-results`

---

## Phase 1: DirectoryReader Abstraction Layer

**Timeline**: 1 week (5-7 days)

### 1.1 Define Core Data Structures

- [ ] Create header file: `touch common/dir_reader.h`
- [ ] Add header guard to `common/dir_reader.h`:
  ```c
  #ifndef DIR_READER_H
  #define DIR_READER_H
  ```
- [ ] Add includes:
  ```c
  #include <stdint.h>
  #include <stdbool.h>
  #include <dirent.h>
  #include <liburing.h>
  ```
- [ ] Define `enum dir_reader_mode`:
  ```c
  enum dir_reader_mode {
      DIR_READER_CLASSIC,
      DIR_READER_IORING,
      DIR_READER_IORING_NOWAIT
  };
  ```
- [ ] Define `struct dir_entry`:
  ```c
  struct dir_entry {
      uint64_t ino;
      uint8_t type;
      char name[256];
  };
  ```
- [ ] Define `struct dir_reader` (see design doc)
- [ ] Define `struct dir_reader_ops` (see design doc)
- [ ] Add function declarations (init, read, close, etc.)
- [ ] Close header guard: `#endif /* DIR_READER_H */`
- [ ] Save file

### 1.2 Implement Classic readdir() Backend

- [ ] Create implementation file: `touch common/dir_reader_classic.c`
- [ ] Add includes:
  ```c
  #include "dir_reader.h"
  #include <stdlib.h>
  #include <string.h>
  #include <errno.h>
  ```
- [ ] Implement `classic_init()` function
  - [ ] Open directory with `opendir()`
  - [ ] Store DIR* in `reader->private_data`
  - [ ] Return 0 on success, -errno on failure
- [ ] Implement `classic_read_batch()` function
  - [ ] Loop up to `max_entries`
  - [ ] Call `readdir()` for each entry
  - [ ] Convert `struct dirent` to `struct dir_entry`
  - [ ] Increment statistics
  - [ ] Return count of entries read
- [ ] Implement `classic_cleanup()` function
  - [ ] Call `closedir()` on private_data
  - [ ] Set private_data to NULL
- [ ] Implement `classic_rewind()` function (optional)
  - [ ] Call `rewinddir()` on private_data
  - [ ] Return 0
- [ ] Define `classic_ops` structure:
  ```c
  static const struct dir_reader_ops classic_ops = {
      .init = classic_init,
      .read_batch = classic_read_batch,
      .cleanup = classic_cleanup,
      .rewind = classic_rewind,
  };
  ```
- [ ] Save file

### 1.3 Implement io_uring Backend

- [ ] Create implementation file: `touch common/dir_reader_ioring.c`
- [ ] Add includes (same as classic + liburing)
- [ ] Define context structure:
  ```c
  struct ioring_reader_ctx {
      struct io_uring ring;
      int dirfd;
      uint64_t offset;
      char buffer[32768];
      unsigned flags;
  };
  ```
- [ ] Implement `ioring_init()` function
  - [ ] Allocate `ioring_reader_ctx`
  - [ ] Initialize io_uring: `io_uring_queue_init(32, &ctx->ring, 0)`
  - [ ] Open directory: `open(path, O_RDONLY | O_DIRECTORY)`
  - [ ] Set initial offset to 0
  - [ ] Set flags based on reader mode
  - [ ] Store ctx in reader->private_data
  - [ ] Return 0 on success, cleanup and return -errno on failure
- [ ] Implement `ioring_read_batch()` function
  - [ ] Get SQE: `io_uring_get_sqe()`
  - [ ] Prepare getdents: `io_uring_prep_getdents()`
  - [ ] Submit: `io_uring_submit()`
  - [ ] Wait for CQE: `io_uring_wait_cqe()`
  - [ ] Check result (handle -EAGAIN if NOWAIT)
  - [ ] Parse buffer: `io_uring_getdents_parse()`
  - [ ] Convert entries to `struct dir_entry` format
  - [ ] Update offset
  - [ ] Increment statistics
  - [ ] Return count
- [ ] Implement `ioring_cleanup()` function
  - [ ] Close dirfd
  - [ ] Exit io_uring: `io_uring_queue_exit()`
  - [ ] Free context
- [ ] Define `ioring_ops` structure (similar to classic)
- [ ] Save file

### 1.4 Implement Factory Functions

- [ ] Create main implementation file: `touch common/dir_reader.c`
- [ ] Add includes
- [ ] Implement `dir_reader_create()`:
  - [ ] Allocate `struct dir_reader`
  - [ ] Set mode
  - [ ] Assign ops based on mode (classic_ops or ioring_ops)
  - [ ] Initialize statistics to 0
  - [ ] Return pointer
- [ ] Implement `dir_reader_destroy()`:
  - [ ] Call cleanup if private_data exists
  - [ ] Free reader structure
- [ ] Implement wrapper functions:
  - [ ] `dir_reader_open()` - calls ops->init()
  - [ ] `dir_reader_read()` - calls ops->read_batch()
  - [ ] `dir_reader_close()` - calls ops->cleanup()
  - [ ] `dir_reader_rewind()` - calls ops->rewind()
- [ ] Implement `dir_reader_print_stats()`:
  - [ ] Print ops_count, total_entries, eagain_count
  - [ ] Calculate averages
- [ ] Save file

### 1.5 Build and Test Abstraction Layer

- [ ] Create Bazel build file: `touch common/BUILD.bazel`
- [ ] Add build rules:
  ```python
  # common/BUILD.bazel
  
  cc_library(
      name = "dir_reader",
      srcs = [
          "dir_reader.c",
          "dir_reader_classic.c",
          "dir_reader_ioring.c",
      ],
      hdrs = ["dir_reader.h"],
      deps = ["@liburing"],
      copts = ["-Wall", "-Wextra", "-O2", "-g"],
      visibility = ["//visibility:public"],
  )
  
  cc_test(
      name = "test_dir_reader",
      srcs = ["test_dir_reader.c"],
      deps = [":dir_reader"],
      data = glob(["testdata/**"]),
  )
  ```
- [ ] Build library: `bazel build //common:dir_reader`
- [ ] Check for compilation errors
- [ ] Fix any errors found
- [ ] Create simple test program: `touch common/test_dir_reader.c`
- [ ] Write test that:
  - [ ] Creates test directory with 100 files
  - [ ] Reads with classic reader
  - [ ] Reads with io_uring reader
  - [ ] Compares results (should match)
  - [ ] Returns 0 for PASS, 1 for FAIL
- [ ] Build test: `bazel build //common:test_dir_reader`
- [ ] Run test: `bazel test //common:test_dir_reader`
- [ ] Verify test passes

---

## Phase 2: Basic Chaos Test Framework

**Timeline**: 1-2 weeks

### 2.1 Create Chaos Framework Header

- [ ] Create header: `touch chaos/chaos_framework.h`
- [ ] Add header guards and includes
- [ ] Define `struct chaos_config`:
  ```c
  struct chaos_config {
      double eagain_prob;
      double enomem_prob;
      double delay_prob;
      int min_delay_us;
      int max_delay_us;
      int num_readers;
      int num_writers;
      int duration_seconds;
      bool check_no_duplicates;
      bool check_no_missing;
  };
  ```
- [ ] Define `struct chaos_result`:
  ```c
  struct chaos_result {
      uint64_t operations_total;
      uint64_t operations_failed;
      uint64_t injected_faults;
      uint64_t duplicates_found;
      uint64_t missing_entries;
      bool passed;
  };
  ```
- [ ] Add function declarations
- [ ] Save file

### 2.2 Implement Utility Functions

- [ ] Create utils file: `touch chaos/chaos_utils.c`
- [ ] Add includes (chaos_framework.h, stdlib, time, etc.)
- [ ] Implement `random_init()`:
  - [ ] Seed random: `srand(time(NULL) ^ getpid())`
- [ ] Implement `should_inject_delay()`:
  - [ ] Return true with delay_prob probability
- [ ] Implement `random_delay()`:
  - [ ] Return random value between min_delay_us and max_delay_us
- [ ] Implement `should_inject_fault()`:
  - [ ] Return true with fault probability
- [ ] Implement `get_random_error()`:
  - [ ] Return -EAGAIN or -ENOMEM randomly
- [ ] Save file

### 2.3 Implement Chaos Reader Thread

- [ ] Create file: `touch chaos/chaos_rapid_modifications.c`
- [ ] Add includes
- [ ] Define test state structure:
  ```c
  struct test_state {
      const char *test_dir;
      atomic_int files_created;
      atomic_int files_deleted;
      atomic_int read_operations;
      atomic_bool stop_test;
      struct chaos_config *config;
      enum dir_reader_mode mode;
  };
  ```
- [ ] Implement `chaos_reader_thread()`:
  - [ ] Loop while !stop_test
  - [ ] Create dir_reader
  - [ ] Open directory
  - [ ] Read entries in loop
  - [ ] Inject delays randomly
  - [ ] Inject faults randomly
  - [ ] Close directory
  - [ ] Increment operation count
  - [ ] Destroy reader
- [ ] Save progress

### 2.4 Implement Chaos Writer Thread

- [ ] In same file (`chaos_rapid_modifications.c`)
- [ ] Implement `chaos_writer_thread()`:
  - [ ] Loop while !stop_test
  - [ ] Generate unique filename (pid + counter)
  - [ ] Create file with `creat()`
  - [ ] Close file
  - [ ] Inject random delay
  - [ ] Delete file with `unlink()`
  - [ ] Increment created/deleted counters
  - [ ] Brief pause (0-1ms)
- [ ] Save progress

### 2.5 Implement Main Chaos Test

- [ ] Still in `chaos_rapid_modifications.c`
- [ ] Implement `chaos_test_rapid_modifications()`:
  - [ ] Initialize test_state
  - [ ] Initialize chaos_config
  - [ ] Print test parameters
  - [ ] Create test directory if needed
  - [ ] Launch reader threads (pthread_create)
  - [ ] Launch writer threads (pthread_create)
  - [ ] Sleep for duration
  - [ ] Set stop_test = true
  - [ ] Join all threads (pthread_join)
  - [ ] Print results
  - [ ] Return success/failure
- [ ] Add main() function for standalone testing
- [ ] Save file

### 2.6 Build and Test Chaos Framework (Basic)

- [ ] Create Bazel build file: `touch chaos/BUILD.bazel`
- [ ] Add build rules:
  ```python
  # chaos/BUILD.bazel
  
  cc_library(
      name = "chaos_utils",
      srcs = ["chaos_utils.c"],
      hdrs = ["chaos_framework.h"],
      copts = ["-Wall", "-Wextra", "-O2", "-g"],
  )
  
  cc_binary(
      name = "chaos_rapid_modifications",
      srcs = ["chaos_rapid_modifications.c"],
      deps = [
          ":chaos_utils",
          "//common:dir_reader",
      ],
      linkopts = ["-pthread", "-latomic"],
  )
  ```
- [ ] Build: `bazel build //chaos:chaos_rapid_modifications`
- [ ] Fix compilation errors
- [ ] Create test directory: `mkdir /tmp/chaos_test`
- [ ] Run basic test: `bazel run //chaos:chaos_rapid_modifications -- /tmp/chaos_test`
  - [ ] Let it run for 10 seconds
  - [ ] Verify no crashes
  - [ ] Check output makes sense
- [ ] If test passes, mark this phase complete

---

## Phase 3: eBPF Fault Injection

**Timeline**: 2-3 weeks (requires eBPF expertise)

### 3.1 Prerequisites Check

- [ ] Verify kernel version: `uname -r` (need 5.10+ for fentry)
- [ ] Check BTF support: `ls /sys/kernel/btf/vmlinux` (should exist)
- [ ] Verify bpftool installed: `which bpftool`
- [ ] Check libbpf installed: `dpkg -l | grep libbpf`
- [ ] Test eBPF compilation:
  ```bash
  echo 'SEC("tracepoint/syscalls/sys_enter_openat") int trace_openat(void *ctx) { return 0; }' > /tmp/test.bpf.c
  clang -O2 -target bpf -c /tmp/test.bpf.c -o /tmp/test.bpf.o
  ```
- [ ] If above works, eBPF toolchain is ready

### 3.2 Create eBPF Program Structure

- [ ] Create eBPF source: `touch chaos/ebpf_injector.bpf.c`
- [ ] Add eBPF includes:
  ```c
  #include <linux/bpf.h>
  #include <bpf/bpf_helpers.h>
  #include <bpf/bpf_tracing.h>
  ```
- [ ] Define license: `char _license[] SEC("license") = "GPL";`
- [ ] Define configuration map:
  ```c
  struct {
      __uint(type, BPF_MAP_TYPE_HASH);
      __uint(max_entries, 1024);
      __type(key, u32);  // PID
      __type(value, u64);  // config flags
  } fault_config SEC(".maps");
  ```
- [ ] Define global config variables:
  ```c
  static volatile const __u32 delay_prob_pct = 5;
  static volatile const __u32 eagain_prob_pct = 2;
  static volatile const __u32 max_delay_us = 10000;
  ```
- [ ] Save file

### 3.3 Implement eBPF Hooks

- [ ] Add fentry hook for vfs_getdents_async:
  ```c
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
      
      // Log entry
      bpf_trace_printk("getdents_async: pid=%d offset=%lld\n", pid, offset);
      
      // Random delay simulation (just log intent)
      u32 rand = bpf_get_prandom_u32();
      if ((rand % 100) < delay_prob_pct) {
          u32 delay = rand % max_delay_us;
          bpf_trace_printk("Would inject %dus delay for pid %d\n", delay, pid);
      }
      
      return 0;
  }
  ```
- [ ] Add fexit hook for vfs_getdents_async:
  ```c
  SEC("fexit/vfs_getdents_async")
  int BPF_PROG(trace_getdents_exit, int ret)
  {
      u32 pid = bpf_get_current_pid_tgid() >> 32;
      
      // Log exit
      bpf_trace_printk("getdents_async exit: pid=%d ret=%d\n", pid, ret);
      
      // Random fault injection (would need bpf_override_return)
      u32 rand = bpf_get_prandom_u32();
      if ((rand % 100) < eagain_prob_pct) {
          bpf_trace_printk("Would force -EAGAIN for pid %d\n", pid);
          // Note: bpf_override_return requires CONFIG_BPF_KPROBE_OVERRIDE
          // and specific kernel support
      }
      
      return 0;
  }
  ```
- [ ] Save file

### 3.4 Create eBPF Loader

- [ ] Create loader: `touch chaos/ebpf_injector_load.c`
- [ ] Add includes:
  ```c
  #include <stdio.h>
  #include <stdlib.h>
  #include <bpf/libbpf.h>
  #include <bpf/bpf.h>
  ```
- [ ] Define injector structure:
  ```c
  struct ebpf_injector {
      struct bpf_object *obj;
      struct bpf_link *links[10];
      int num_links;
  };
  ```
- [ ] Implement `ebpf_injector_load()`:
  - [ ] Open BPF object: `bpf_object__open_file()`
  - [ ] Load BPF object: `bpf_object__load()`
  - [ ] Iterate programs: `bpf_object__for_each_program()`
  - [ ] Attach each program: `bpf_program__attach()`
  - [ ] Store links
  - [ ] Return injector struct
- [ ] Implement `ebpf_injector_unload()`:
  - [ ] Destroy all links
  - [ ] Close object
  - [ ] Free structure
- [ ] Implement `ebpf_injector_set_config()`:
  - [ ] Update map values
- [ ] Add main() for testing
- [ ] Save file

### 3.5 Build eBPF Components

- [ ] Update chaos/BUILD.bazel:
  ```python
  # eBPF compilation requires special handling
  
  # eBPF object file (compiled with clang)
  genrule(
      name = "ebpf_injector_bpf_obj",
      srcs = ["ebpf_injector.bpf.c"],
      outs = ["ebpf_injector.bpf.o"],
      cmd = """
          clang -g -O2 -target bpf -D__TARGET_ARCH_x86_64 \
              -I/usr/include/x86_64-linux-gnu \
              -c $(location ebpf_injector.bpf.c) -o $@
      """,
  )
  
  cc_binary(
      name = "ebpf_injector_load",
      srcs = ["ebpf_injector_load.c"],
      data = [":ebpf_injector_bpf_obj"],
      linkopts = ["-lbpf"],
  )
  ```
- [ ] Build eBPF program: `bazel build //chaos:ebpf_injector_bpf_obj`
- [ ] Fix any compilation errors
- [ ] Build loader: `bazel build //chaos:ebpf_injector_load`
- [ ] Test loader as root: `sudo bazel-bin/chaos/ebpf_injector_load`
- [ ] Check /sys/kernel/debug/tracing/trace for output:
  ```bash
  sudo cat /sys/kernel/debug/tracing/trace_pipe
  ```
- [ ] Run a directory operation in another terminal
- [ ] Verify eBPF traces appear
- [ ] If working, mark phase complete

---

## Phase 4: VM Infrastructure - Base Image

**Timeline**: 2-3 days

### 4.1 Prepare VM Directory

- [ ] Create VM directory: `mkdir -p vms/images`
- [ ] Navigate: `cd vms`
- [ ] Create logs directory: `mkdir logs`

### 4.2 Download Base Cloud Image

- [ ] Download Ubuntu 24.04 cloud image:
  ```bash
  wget https://cloud-images.ubuntu.com/releases/24.04/release/ubuntu-24.04-server-cloudimg-amd64.img
  ```
- [ ] Verify download: `ls -lh ubuntu-24.04-server-cloudimg-amd64.img`
- [ ] Check image format: `qemu-img info ubuntu-24.04-server-cloudimg-amd64.img`

### 4.3 Create Base Image

- [ ] Copy to base image:
  ```bash
  cp ubuntu-24.04-server-cloudimg-amd64.img images/async-getdents-base.qcow2
  ```
- [ ] Resize image to 30GB:
  ```bash
  qemu-img resize images/async-getdents-base.qcow2 30G
  ```
- [ ] Verify resize: `qemu-img info images/async-getdents-base.qcow2`

### 4.4 Create cloud-init Configuration

- [ ] Create cloud-init directory: `mkdir -p cloud-init`
- [ ] Create user-data file: `touch cloud-init/user-data`
- [ ] Add cloud-init config:
  ```yaml
  #cloud-config
  users:
    - name: test
      sudo: ALL=(ALL) NOPASSWD:ALL
      shell: /bin/bash
      ssh_authorized_keys:
        - <YOUR_SSH_PUBLIC_KEY_HERE>
  
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
  ```
- [ ] Replace <YOUR_SSH_PUBLIC_KEY_HERE> with content from:
  ```bash
  cat ~/.ssh/id_rsa.pub
  ```
- [ ] Create meta-data file: `touch cloud-init/meta-data`
- [ ] Add to meta-data:
  ```yaml
  instance-id: async-getdents-base
  local-hostname: async-getdents-base
  ```
- [ ] Save files

### 4.5 Create Base VM Script

- [ ] Create script: `touch create_base_image.sh`
- [ ] Make executable: `chmod +x create_base_image.sh`
- [ ] Add shebang: `#!/bin/bash`
- [ ] Add script content (see design doc)
- [ ] Key steps in script:
  - [ ] Set variables (IMAGE_NAME, etc.)
  - [ ] Check prerequisites
  - [ ] Launch VM with virt-install
  - [ ] Wait for boot
  - [ ] Get VM IP
  - [ ] Copy kernel to VM
  - [ ] Install kernel
  - [ ] Copy test suite
  - [ ] Shutdown VM
- [ ] Save script

### 4.6 Test Base Image Creation

- [ ] Run script: `./create_base_image.sh 2>&1 | tee logs/base-image-creation.log`
- [ ] Watch for errors
- [ ] If VM doesn't boot, check:
  - [ ] libvirt logs: `sudo journalctl -u libvirtd`
  - [ ] VM console: `virsh console async-getdents-base`
- [ ] Once booted, get IP: `virsh domifaddr async-getdents-base`
- [ ] SSH to VM: `ssh test@<VM_IP>`
- [ ] Verify kernel installed: `uname -r`
- [ ] Verify test tools present: `which fio strace`
- [ ] Exit SSH
- [ ] Shutdown VM: `virsh shutdown async-getdents-base`
- [ ] Wait for shutdown: `virsh list --all`
- [ ] Create snapshot:
  ```bash
  virsh snapshot-create-as async-getdents-base base-snapshot "Clean base image"
  ```
- [ ] Verify snapshot: `virsh snapshot-list async-getdents-base`

---

## Phase 5: Filesystem-Specific VMs

**Timeline**: 3-5 days

### 5.1 Create ext4 VM

- [ ] Create script: `touch create_ext4_vm.sh`
- [ ] Make executable: `chmod +x create_ext4_vm.sh`
- [ ] Add script content (see design doc)
- [ ] Key steps:
  - [ ] Clone base image
  - [ ] Create 100GB data disk
  - [ ] Launch VM
  - [ ] Format as ext4 with htree
  - [ ] Mount filesystem
  - [ ] Verify htree enabled
- [ ] Run script: `./create_ext4_vm.sh 2>&1 | tee logs/ext4-vm-creation.log`
- [ ] Wait for completion
- [ ] Verify VM created: `virsh list --all | grep ext4`
- [ ] Get VM IP: `virsh domifaddr async-getdents-ext4`
- [ ] SSH to VM: `ssh test@<EXT4_VM_IP>`
- [ ] Check filesystem: `df -h /test/ext4`
- [ ] Verify ext4: `mount | grep ext4`
- [ ] Check htree: `sudo tune2fs -l /dev/vdb | grep dir_index`
- [ ] Create test file: `touch /test/ext4/testfile && ls -l /test/ext4/`
- [ ] Exit SSH
- [ ] Shutdown VM: `virsh shutdown async-getdents-ext4`

### 5.2 Create XFS VM

- [ ] Create script: `touch create_xfs_vm.sh`
- [ ] Make executable: `chmod +x create_xfs_vm.sh`
- [ ] Add script content (see design doc)
- [ ] Run script: `./create_xfs_vm.sh 2>&1 | tee logs/xfs-vm-creation.log`
- [ ] Wait for completion
- [ ] Verify VM created: `virsh list --all | grep xfs`
- [ ] SSH to VM
- [ ] Check filesystem: `df -h /test/xfs`
- [ ] Verify XFS: `mount | grep xfs`
- [ ] Check XFS info: `sudo xfs_info /test/xfs`
- [ ] Create test file: `touch /test/xfs/testfile`
- [ ] Exit SSH
- [ ] Shutdown VM: `virsh shutdown async-getdents-xfs`

### 5.3 Create ZFS VM

- [ ] Create script: `touch create_zfs_vm.sh`
- [ ] Make executable: `chmod +x create_zfs_vm.sh`
- [ ] Add script content (see design doc)
- [ ] **Note**: ZFS VM needs 8GB RAM
- [ ] Run script: `./create_zfs_vm.sh 2>&1 | tee logs/zfs-vm-creation.log`
- [ ] Wait for completion (ZFS setup takes longer)
- [ ] Verify VM created: `virsh list --all | grep zfs`
- [ ] SSH to VM
- [ ] Check ZFS pool: `zpool status testpool`
- [ ] Check ZFS filesystem: `zfs list`
- [ ] Verify mount: `df -h /test/zfs`
- [ ] Create test file: `touch /test/zfs/testfile`
- [ ] Check ZFS can see it: `zfs list -t all`
- [ ] Exit SSH
- [ ] Shutdown VM: `virsh shutdown async-getdents-zfs`

### 5.4 Create btrfs VM

- [ ] Create script: `touch create_btrfs_vm.sh`
- [ ] Make executable: `chmod +x create_btrfs_vm.sh`
- [ ] Add script content (see design doc)
- [ ] Run script: `./create_btrfs_vm.sh 2>&1 | tee logs/btrfs-vm-creation.log`
- [ ] Wait for completion
- [ ] Verify VM created: `virsh list --all | grep btrfs`
- [ ] SSH to VM
- [ ] Check filesystem: `df -h /test/btrfs`
- [ ] Verify btrfs: `mount | grep btrfs`
- [ ] Check btrfs info: `sudo btrfs filesystem show /test/btrfs`
- [ ] Create test file: `touch /test/btrfs/testfile`
- [ ] Exit SSH
- [ ] Shutdown VM: `virsh shutdown async-getdents-btrfs`

### 5.5 Create tmpfs VM

- [ ] Create script: `touch create_tmpfs_vm.sh`
- [ ] Make executable: `chmod +x create_tmpfs_vm.sh`
- [ ] Add script content (see design doc)
- [ ] **Note**: tmpfs needs 8GB RAM, no extra disk
- [ ] Run script: `./create_tmpfs_vm.sh 2>&1 | tee logs/tmpfs-vm-creation.log`
- [ ] Wait for completion
- [ ] Verify VM created: `virsh list --all | grep tmpfs`
- [ ] SSH to VM
- [ ] Check tmpfs: `df -h /test/tmpfs`
- [ ] Verify tmpfs: `mount | grep tmpfs`
- [ ] Check size: should be 16GB
- [ ] Create test file: `touch /test/tmpfs/testfile`
- [ ] Exit SSH
- [ ] Shutdown VM: `virsh shutdown async-getdents-tmpfs`

### 5.6 Create VM Inventory

- [ ] Create inventory file: `touch vm_inventory.txt`
- [ ] Add inventory content (see design doc)
- [ ] Get actual IPs for each VM:
  ```bash
  virsh list --all | grep async-getdents | while read vm rest; do
      echo "$vm: $(virsh domifaddr $vm | grep ipv4)"
  done
  ```
- [ ] Update inventory with correct IPs
- [ ] Save file

---

## Phase 6: Test Data Generation

**Timeline**: 2-3 days

### 6.1 Create Helper Scripts

- [ ] Create helpers directory if not exists: `mkdir -p ../helpers`
- [ ] Navigate: `cd ../helpers`

### 6.2 Simple File Generator

- [ ] Create script: `touch create_test_files.sh`
- [ ] Make executable: `chmod +x create_test_files.sh`
- [ ] Add content:
  ```bash
  #!/bin/bash
  # Usage: create_test_files.sh <directory> <count>
  
  DIR=$1
  COUNT=$2
  
  if [ -z "$DIR" ] || [ -z "$COUNT" ]; then
      echo "Usage: $0 <directory> <count>"
      exit 1
  fi
  
  mkdir -p "$DIR"
  echo "Creating $COUNT files in $DIR..."
  
  for i in $(seq 1 $COUNT); do
      filename=$(printf "file%06d.txt" $i)
      echo "Test file $i" > "$DIR/$filename"
      
      if [ $((i % 1000)) -eq 0 ]; then
          echo "  Created $i files..."
      fi
  done
  
  echo "Done. Created $COUNT files."
  ```
- [ ] Save file
- [ ] Test locally: `./create_test_files.sh /tmp/testfiles 100`
- [ ] Verify: `ls /tmp/testfiles | wc -l` (should be 100)
- [ ] Clean up: `rm -rf /tmp/testfiles`

### 6.3 Nested Directory Generator

- [ ] Create script: `touch create_nested_structure.sh`
- [ ] Make executable: `chmod +x create_nested_structure.sh`
- [ ] Add content (recursive directory creation)
- [ ] Test with small values: `./create_nested_structure.sh /tmp/nested 2 3`
- [ ] Verify structure: `tree /tmp/nested` or `find /tmp/nested`
- [ ] Clean up: `rm -rf /tmp/nested`

### 6.4 Realistic File Distribution Generator

- [ ] Create script: `touch create_realistic_data.sh`
- [ ] Make executable: `chmod +x create_realistic_data.sh`
- [ ] Add content:
  ```bash
  #!/bin/bash
  # Create realistic file distribution (Zipf-like)
  
  DIR=$1
  
  mkdir -p "$DIR"
  
  # Mix of:
  # - Small files (most common)
  # - Medium files
  # - Large files (rare)
  # - Various extensions
  # - Some with special characters
  
  echo "Creating realistic file distribution..."
  
  # 1000 small text files
  for i in {1..1000}; do
      echo "Small file $i" > "$DIR/doc_$(printf %04d $i).txt"
  done
  
  # 500 config files
  for i in {1..500}; do
      echo "[config]" > "$DIR/config_$i.conf"
      echo "key=value" >> "$DIR/config_$i.conf"
  done
  
  # 100 larger files (1MB each)
  for i in {1..100}; do
      dd if=/dev/urandom of="$DIR/data_$i.bin" bs=1M count=1 2>/dev/null
  done
  
  # Some special filenames
  touch "$DIR/file with spaces.txt"
  touch "$DIR/file-with-dashes.txt"
  touch "$DIR/file_with_underscores.txt"
  touch "$DIR/fileñame_ütf8.txt"
  
  echo "Created $(find $DIR -type f | wc -l) files"
  ```
- [ ] Save file
- [ ] Test: `./create_realistic_data.sh /tmp/realistic`
- [ ] Verify: `du -sh /tmp/realistic`
- [ ] Clean up: `rm -rf /tmp/realistic`

### 7.5 Hash Collision Generator (for ext4 testing)

- [ ] Create script: `touch create_hash_collision_files.sh`
- [ ] Make executable: `chmod +x create_hash_collision_files.sh`
- [ ] Add content:
  ```bash
  #!/bin/bash
  # Try to create files with hash collisions
  # (This is tricky - may need to research ext4 hash function)
  
  DIR=$1
  mkdir -p "$DIR"
  
  # For now, create files with similar patterns
  # that might hash similarly
  
  for i in {1..1000}; do
      # Create variations of same base name
      touch "$DIR/file_${i}"
      touch "$DIR/file_${i}_a"
      touch "$DIR/file_${i}_b"
  done
  
  echo "Created $(ls $DIR | wc -l) files"
  ```
- [ ] Save file

### 7.6 Large Directory Generator

- [ ] Create script: `touch create_large_directory.sh`
- [ ] Make executable: `chmod +x create_large_directory.sh`
- [ ] Add content:
  ```bash
  #!/bin/bash
  # Create very large directory (100K-1M files)
  
  DIR=$1
  COUNT=${2:-100000}  # Default 100K
  
  mkdir -p "$DIR"
  echo "Creating $COUNT files (this will take a while)..."
  
  # Use faster method: parallel creation
  for batch in $(seq 0 99); do
      for i in $(seq $((batch * 1000)) $((batch * 1000 + 999))); do
          touch "$DIR/f_$(printf %08d $i)"
      done &
      
      if [ $((batch % 10)) -eq 0 ]; then
          echo "  Progress: $((batch * 1000)) files..."
          wait  # Wait for batch to complete
      fi
  done
  
  wait  # Wait for all
  
  echo "Done. Created $(ls $DIR | wc -l) files."
  ```
- [ ] Save file
- [ ] **Don't test locally yet** (too many files)

### 7.7 Deploy Helper Scripts to VMs

- [ ] Create deployment script: `touch deploy_helpers_to_vms.sh`
- [ ] Make executable: `chmod +x deploy_helpers_to_vms.sh`
- [ ] Add content:
  ```bash
  #!/bin/bash
  # Deploy helper scripts to all VMs
  
  cd "$(dirname "$0")"
  
  VMS="async-getdents-ext4 async-getdents-xfs async-getdents-zfs async-getdents-btrfs async-getdents-tmpfs"
  
  for vm in $VMS; do
      echo "Deploying to $vm..."
      VM_IP=$(virsh domifaddr $vm | grep ipv4 | awk '{print $4}' | cut -d'/' -f1)
      
      if [ -z "$VM_IP" ]; then
          echo "  Could not get IP for $vm, skipping"
          continue
      fi
      
      # Create directory
      ssh test@$VM_IP "mkdir -p ~/helpers"
      
      # Copy scripts
      scp *.sh test@$VM_IP:~/helpers/
      
      echo "  Deployed to $VM_IP"
  done
  
  echo "Deployment complete"
  ```
- [ ] Save file
- [ ] Start all VMs: `cd ../vms && ./vm_control.sh start`
- [ ] Wait 30 seconds for boot
- [ ] Deploy: `cd ../helpers && ./deploy_helpers_to_vms.sh`
- [ ] Verify deployment by SSH to one VM:
  ```bash
  ssh test@<VM_IP> "ls -l ~/helpers/"
  ```

---

## Phase 8: VM Management & Orchestration

**Timeline**: 2-3 days

### 8.1 Create VM Control Script

- [ ] Navigate back to vms: `cd ../vms`
- [ ] Create script: `touch vm_control.sh`
- [ ] Make executable: `chmod +x vm_control.sh`
- [ ] Add content (see design doc):
  - [ ] start command
  - [ ] stop command
  - [ ] destroy command (force stop)
  - [ ] status command
  - [ ] clean command (remove all)
- [ ] Save file
- [ ] Test each command:
  - [ ] `./vm_control.sh status`
  - [ ] `./vm_control.sh stop`
  - [ ] Wait for shutdown
  - [ ] `./vm_control.sh status` (all should be shut off)
  - [ ] `./vm_control.sh start`
  - [ ] Wait for boot
  - [ ] `./vm_control.sh status` (all should be running)

### 8.2 Create Test Data on All VMs

- [ ] Create script: `touch populate_test_data.sh`
- [ ] Make executable: `chmod +x populate_test_data.sh`
- [ ] Add content:
  ```bash
  #!/bin/bash
  # Populate test data on all VMs
  
  declare -A VMS
  VMS=(
      ["async-getdents-ext4"]="/test/ext4"
      ["async-getdents-xfs"]="/test/xfs"
      ["async-getdents-zfs"]="/test/zfs"
      ["async-getdents-btrfs"]="/test/btrfs"
      ["async-getdents-tmpfs"]="/test/tmpfs"
  )
  
  for vm in "${!VMS[@]}"; do
      mount="${VMS[$vm]}"
      echo "Populating $vm at $mount..."
      
      VM_IP=$(virsh domifaddr $vm | grep ipv4 | awk '{print $4}' | cut -d'/' -f1)
      
      if [ -z "$VM_IP" ]; then
          echo "  Could not get IP for $vm, skipping"
          continue
      fi
      
      # Create various test directories
      ssh test@$VM_IP << EOF
          cd $mount
          
          # Small directory (100 files)
          mkdir -p small_dir
          ~/helpers/create_test_files.sh small_dir 100
          
          # Medium directory (10K files)
          mkdir -p medium_dir
          ~/helpers/create_test_files.sh medium_dir 10000
          
          # Large directory (100K files) - only for non-tmpfs
          if [ "$mount" != "/test/tmpfs" ]; then
              mkdir -p large_dir
              ~/helpers/create_large_directory.sh large_dir 100000
          fi
          
          # Nested structure
          mkdir -p nested
          ~/helpers/create_nested_structure.sh nested 3 5
          
          # Realistic distribution
          mkdir -p realistic
          ~/helpers/create_realistic_data.sh realistic
          
          echo "Data population complete for $mount"
  EOF
      
      echo "  Done for $vm"
  done
  
  echo "All VMs populated with test data"
  ```
- [ ] Save file
- [ ] Run script: `./populate_test_data.sh 2>&1 | tee logs/populate-data.log`
- [ ] **This will take 15-30 minutes** (100K files per VM)
- [ ] Monitor progress in log
- [ ] When complete, verify on one VM:
  ```bash
  ssh test@<VM_IP> "find /test/<fs> -type d | head -20"
  ```

### 8.3 Create Test Runner Script

- [ ] Create script: `touch run_tests_on_vm.sh`
- [ ] Make executable: `chmod +x run_tests_on_vm.sh`
- [ ] Add content:
  ```bash
  #!/bin/bash
  # Run tests on a single VM
  
  VM_NAME=$1
  FS_TYPE=$2
  MOUNT_POINT=$3
  
  if [ -z "$VM_NAME" ] || [ -z "$FS_TYPE" ]; then
      echo "Usage: $0 <vm_name> <fs_type> <mount_point>"
      exit 1
  fi
  
  echo "=== Testing $FS_TYPE on $VM_NAME ==="
  
  VM_IP=$(virsh domifaddr $VM_NAME | grep ipv4 | awk '{print $4}' | cut -d'/' -f1)
  
  if [ -z "$VM_IP" ]; then
      echo "ERROR: Could not get IP for $VM_NAME"
      exit 1
  fi
  
  echo "VM IP: $VM_IP"
  echo "Running tests..."
  
  # Run tests via SSH
  ssh test@$VM_IP << EOF
      cd ~/test-suite
      
      echo "--- Basic Tests ---"
      ./test_compare_implementations $MOUNT_POINT/small_dir || echo "FAILED"
      
      echo "--- Stress Tests ---"
      ./stress_concurrent_readers $MOUNT_POINT/medium_dir || echo "FAILED"
      
      echo "--- Chaos Tests ---"
      sudo ./chaos_rapid_modifications $MOUNT_POINT/realistic || echo "FAILED"
      
      echo "Tests complete for $FS_TYPE"
  EOF
  
  echo "=== Completed $FS_TYPE ==="
  ```
- [ ] Save file

### 8.4 Create Orchestration Script

- [ ] Create script: `touch run_all_tests.sh`
- [ ] Make executable: `chmod +x run_all_tests.sh`
- [ ] Add content (see design doc)
- [ ] Key components:
  - [ ] Create results directory with timestamp
  - [ ] Loop through all VMs
  - [ ] Run tests on each
  - [ ] Collect results
  - [ ] Generate report
- [ ] Save file

### 8.5 Create Report Generator

- [ ] Create script: `touch generate_test_report.sh`
- [ ] Make executable: `chmod +x generate_test_report.sh`
- [ ] Add content (see design doc - HTML generation)
- [ ] Save file

### 8.6 Test Complete Orchestration

- [ ] Ensure all VMs running: `./vm_control.sh status`
- [ ] Run full test suite: `./run_all_tests.sh 2>&1 | tee logs/full-test-run.log`
- [ ] **This will take 30-60 minutes**
- [ ] Monitor progress
- [ ] When complete, check results directory
- [ ] Open HTML report in browser:
  ```bash
  firefox test-results/*/summary.html
  ```
- [ ] Verify report shows all filesystems
- [ ] Check for any failures

---

## Phase 9: Integration & Documentation

**Timeline**: 3-5 days

### 9.1 Create Master Setup Script

- [ ] Create script: `touch SETUP_ALL.sh`
- [ ] Make executable: `chmod +x SETUP_ALL.sh`
- [ ] Add content:
  ```bash
  #!/bin/bash
  # RUDRA Master setup script - run once to set up everything
  
  set -e  # Exit on error
  
  echo "=== RUDRA Chaos Testing Framework Setup ==="
  echo ""
  echo "This will:"
  echo "  1. Build test framework (Bazel)"
  echo "  2. Create VMs (5 filesystems)"
  echo "  3. Deploy test suite to VMs"
  echo "  4. Populate test data"
  echo ""
  echo "Time required: 2-3 hours"
  echo "Disk space required: ~500GB"
  echo "RAM required: 32GB+"
  echo ""
  read -p "Continue? (y/n) " -n 1 -r
  echo
  if [[ ! $REPLY =~ ^[Yy]$ ]]; then
      exit 1
  fi
  
  # Phase 1: Build framework
  echo ""
  echo ">>> Phase 1: Building test framework with Bazel..."
  bazel build //...
  
  # Phase 2: Create VMs
  echo ""
  echo ">>> Phase 2: Creating VMs..."
  cd vms
  ./create_all_vms.sh
  
  # Phase 3: Deploy
  echo ""
  echo ">>> Phase 3: Deploying test suite to VMs..."
  ./deploy_tests_to_vms.sh
  
  # Phase 4: Populate data
  echo ""
  echo ">>> Phase 4: Populating test data..."
  ./populate_test_data.sh
  
  echo ""
  echo "=== RUDRA Setup Complete ==="
  echo ""
  echo "To run tests:"
  echo "  cd vms"
  echo "  ./run_all_tests.sh"
  echo ""
  echo "To manage VMs:"
  echo "  ./vm_control.sh start|stop|status"
  echo ""
  echo "To build components:"
  echo "  bazel build //common:dir_reader"
  echo "  bazel build //chaos:all"
  ```
- [ ] Save file

### 9.2 Create Quick Start Guide

- [ ] Create file: `touch QUICKSTART.md`
- [ ] Add content:
  ```markdown
  # Quick Start Guide
  
  ## One-Time Setup
  
  \`\`\`bash
  ./SETUP_ALL.sh
  \`\`\`
  
  This will take 2-3 hours and set up everything.
  
  ## Running Tests
  
  \`\`\`bash
  cd vms
  ./vm_control.sh start           # Start all VMs
  ./run_all_tests.sh              # Run tests
  firefox test-results/*/summary.html  # View results
  \`\`\`
  
  ## Running Individual Tests
  
  \`\`\`bash
  # Run chaos test on ext4
  ./run_tests_on_vm.sh async-getdents-ext4 ext4 /test/ext4
  \`\`\`
  
  ## VM Management
  
  \`\`\`bash
  ./vm_control.sh status    # Check status
  ./vm_control.sh start     # Start all
  ./vm_control.sh stop      # Stop all
  ./vm_control.sh clean     # Delete all VMs
  \`\`\`
  
  ## Troubleshooting
  
  See logs/: All operations log here
  
  VM won't boot:
  - Check: virsh list --all
  - Console: virsh console <vm-name>
  - Logs: sudo journalctl -u libvirtd
  
  Can't SSH to VM:
  - Check IP: virsh domifaddr <vm-name>
  - Check SSH key: ssh -v test@<ip>
  - Check firewall: sudo iptables -L
  ```
- [ ] Save file

### 9.3 Create Main README

- [ ] Edit existing or create: `vim README.md`
- [ ] Add comprehensive content:
  - [ ] Overview
  - [ ] Architecture diagram (ASCII art)
  - [ ] Prerequisites
  - [ ] Setup instructions
  - [ ] Usage examples
  - [ ] Troubleshooting
  - [ ] Contributing
- [ ] Save file

### 9.4 Test Clean Setup from Scratch

- [ ] In a fresh terminal, navigate to project root
- [ ] Clean everything:
  ```bash
  cd vms
  ./vm_control.sh clean
  cd ../common
  make clean
  cd ../chaos
  make clean
  cd ..
  ```
- [ ] Verify clean slate
- [ ] Run master setup: `./SETUP_ALL.sh`
- [ ] Verify everything works
- [ ] If any errors, fix them and update scripts

---

## Phase 10: Validation & Sign-Off

**Timeline**: 1-2 days

### 10.1 End-to-End Validation

- [ ] Clean setup from scratch (if not just done)
- [ ] Run `./SETUP_ALL.sh`
- [ ] Verify all VMs created: `virsh list --all`
- [ ] Run full test suite: `cd vms && ./run_all_tests.sh`
- [ ] Check results: Open HTML report
- [ ] Verify all filesystems tested
- [ ] Verify chaos tests ran
- [ ] Check for any unexpected failures

### 10.2 Performance Baseline

- [ ] For each filesystem, record:
  - [ ] Time to create 100K files
  - [ ] Time to read directory (single-threaded)
  - [ ] Time to read directory (10 concurrent threads)
  - [ ] Chaos test pass/fail
- [ ] Create baseline document: `touch BASELINE_RESULTS.md`
- [ ] Document results
- [ ] Save file

### 10.3 Create Checklist for Future Runs

- [ ] Create file: `touch TESTING_CHECKLIST.md`
- [ ] Add content:
  ```markdown
  # Testing Checklist
  
  Before each test run:
  
  - [ ] All VMs running: `./vm_control.sh status`
  - [ ] Test data present on all VMs
  - [ ] Latest test code deployed
  - [ ] Enough disk space: `df -h`
  - [ ] eBPF tools working (if using): `sudo bpftool prog`
  
  After each test run:
  
  - [ ] Results collected in test-results/
  - [ ] HTML report generated
  - [ ] Any failures investigated
  - [ ] Results compared to baseline
  - [ ] VMs shutdown if not needed: `./vm_control.sh stop`
  
  Weekly maintenance:
  
  - [ ] Update VM snapshots
  - [ ] Clean old test results
  - [ ] Check disk usage: `du -sh vms/images/`
  - [ ] Verify all VMs still bootable
  ```
- [ ] Save file

### 10.4 Final Documentation Review

- [ ] Review all created documentation
- [ ] Check for:
  - [ ] Broken links
  - [ ] Incorrect paths
  - [ ] Missing information
  - [ ] Typos
- [ ] Update as needed
- [ ] Commit all documentation:
  ```bash
  git add .
  git commit -m "Complete chaos testing framework and VM infrastructure"
  ```

---

## Completion Checklist

### All Phases Complete

- [ ] Phase 0: Prerequisites ✓
- [ ] Phase 1: DirectoryReader abstraction ✓
- [ ] Phase 2: Basic chaos framework ✓
- [ ] Phase 3: eBPF fault injection ✓
- [ ] Phase 4: Base VM image ✓
- [ ] Phase 5: Filesystem VMs ✓
- [ ] Phase 6: Test data generation ✓
- [ ] Phase 7: Orchestration ✓
- [ ] Phase 8: Integration ✓
- [ ] Phase 9: Validation ✓

### Deliverables Verified

- [ ] Can create all VMs with one command
- [ ] Can run all tests with one command
- [ ] Can generate HTML report
- [ ] Chaos tests find race conditions
- [ ] eBPF injection works (if implemented)
- [ ] All 5 filesystems tested
- [ ] Documentation complete
- [ ] Everything committed to git

---

## 🎉 Success Criteria

**You have completed the implementation when:**

✅ Running `./SETUP_ALL.sh` creates a complete test infrastructure

✅ Running `./run_all_tests.sh` tests all 5 filesystems

✅ HTML report shows pass/fail for each filesystem

✅ Chaos tests survive 60-second torture runs

✅ Can reproduce entire setup on a fresh machine

✅ All scripts work without manual intervention

✅ Documentation explains everything clearly

---

## Maintenance Schedule

**Daily** (during active development):
- [ ] Run test suite
- [ ] Check for new failures
- [ ] Review test logs

**Weekly**:
- [ ] Update VM snapshots
- [ ] Clean old test results
- [ ] Verify disk space

**Monthly**:
- [ ] Update base image (security patches)
- [ ] Rebuild all VMs
- [ ] Update test data

---

## Estimated Total Time

| Phase | Time | Notes |
|-------|------|-------|
| Prerequisites | 1-2 days | Environment setup |
| DirectoryReader | 1 week | C library abstraction |
| Chaos framework | 1-2 weeks | Basic concurrent tests |
| eBPF injection | 2-3 weeks | ⚠️ Learning curve if new to eBPF |
| VM base image | 2-3 days | Single base setup |
| VM filesystems (5) | 3-5 days | ⚠️ Assumes no issues |
| Test data generation | 2-3 days | Helper scripts |
| Orchestration | 2-3 days | ⚠️ Integration always takes longer |
| Integration & docs | 3-5 days | Polish and debugging |
| Validation | 1-2 days | ⚠️ Will find issues |
| **OPTIMISTIC TOTAL** | **8-10 weeks** | If everything goes smoothly |
| **REALISTIC TOTAL** | **12-16 weeks** | Including debugging & iteration |

---

*This plan provides a complete, step-by-step guide to building a production-quality chaos testing infrastructure for async directory iteration.*

*Follow the checkboxes sequentially for a working system.*

*Good luck! 🚀*

