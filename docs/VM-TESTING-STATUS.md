# VM Testing Status

## Summary

We've refactored VM testing to a **modular, Bazel-native architecture** with clear separation of concerns and better error reporting.

## Architecture

### Old Design (Monolithic)
- Single `run_single_vm_gauntlet.sh` did everything
- Opaque to Bazel (one black-box test)
- Poor error messages ("test failed")
- No intermediate caching

### New Design (Modular)
```
vm_test_orchestrator.sh            ← Entry point, clear error codes
  ├─ create_test_vm.sh             ← Exit 10 on failure
  ├─ wait_for_vm.sh                ← Exit 20 on failure  
  ├─ deploy_xibalba.sh             ← Exit 30 on failure
  └─ run_gauntlet_tests.sh         ← Exit 40 on failure
```

### Benefits
- **Exit codes indicate which step failed**
  - 10: VM creation
  - 20: SSH readiness
  - 30: Xibalba deployment
  - 40: Gauntlet tests
- **Each component independently testable**
- **Bazel sees all dependencies clearly**
- **Better debugging** (can run individual steps)

## Current Status

### ✅ Working
- Modular architecture implemented
- VM creation with hermetic image storage (`/tmp/xibalba-$TEST_ID-$$`)
- SSH key generation (`/tmp/xibalba-ssh-keys/id_rsa`)
- Password auth fallback (ubuntu:xibalba, root:xibalba)
- Proper .gitignore for test artifacts
- Clear error reporting

### ⚠️ Known Issues
1. **IP Detection Unreliable**: `virsh domifaddr` returns empty inside scripts (especially in Bazel)
   - Works: Manual `virsh` commands
   - Fails: Inside loop/script, even with same command
   - Likely: DHCP lease timing + libvirt caching issue
   
2. **Long Wait Times**: Step 2 waits up to 10 minutes for IP to appear
   - VM actually boots in ~2 minutes
   - SSH is ready in ~2-3 minutes
   - But IP not visible via `virsh domifaddr` for much longer

### 🔍 Root Cause Analysis
- VM gets IP from DHCP quickly
- `virsh domifaddr` queries libvirt's DHCP lease database
- Database might not update immediately
- ARP cache might not populate fast enough
- Running in script vs manual seems to affect visibility

## Solutions Being Explored

### Option 1: `libnss-libvirt` (Recommended by libvirt)
```bash
sudo apt install libnss-libvirt
```
Enables hostname resolution (`ssh root@ext4_gauntlet` instead of IP)

### Option 2: Alternative IP Detection
- Parse DHCP leases directly: `/var/lib/libvirt/dnsmasq/default.leases`
- Use `arp -a` instead of `virsh domifaddr`
- Query VM via serial console

### Option 3: Accept Reality
- Keep 10-minute timeout
- It works, just slowly
- Document that cloud-init + DHCP takes time

## Test Artifacts

All runtime-only, properly .gitignored:
- `vm/images/` - VM disk images (qcow2), cloud images (img), cloud-init ISOs
- `vm/configs/` - Generated cloud-init user-data/meta-data
- `/tmp/xibalba-*` - Hermetic test directories (auto-cleaned by Bazel)
- `/tmp/xibalba-ssh-keys/` - Temporary SSH keys for test VMs

## Next Steps

1. **Fix IP detection** - Need reliable, fast method
2. **Test parallel execution** - Run ext4 + zfs simultaneously
3. **Implement progressive consistency tests** - weak → eventual → strong
4. **Measure violations quantitatively** - Report metrics
5. **Document required setup** - libnss-libvirt, libvirt group, etc.

## Usage

```bash
# Run single filesystem test
bazel test //vm:ext4_gauntlet --test_output=streamed

# Run all filesystems in parallel
bazel test //vm:vm_gauntlet_suite --test_output=errors

# Debug individual components
vm/create_test_vm.sh --name test-vm --filesystem ext4
vm/wait_for_vm.sh test-vm 600
vm/deploy_xibalba.sh test-vm packaging/xibalba_0.1.0_amd64.deb
vm/run_gauntlet_tests.sh test-vm ext4
```

