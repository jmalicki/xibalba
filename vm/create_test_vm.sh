#!/bin/bash
set -euo pipefail

# Create a test VM configured for Xibalba testing

# Script directory for relative paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Default values
VM_NAME=""
CUSTOM_KERNEL=""
FILESYSTEM="ext4"
RAM_MB=2048
DISK_GB=10
DATA_DISK_GB=5

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --name)
            VM_NAME="$2"
            shift 2
            ;;
        --kernel)
            CUSTOM_KERNEL="$2"
            shift 2
            ;;
        --filesystem)
            FILESYSTEM="$2"
            shift 2
            ;;
        --ram)
            RAM_MB="$2"
            shift 2
            ;;
        --disk)
            DISK_GB="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 --name VM_NAME [--kernel PATH] [--initrd PATH] [--filesystem TYPE] [--ram MB] [--disk GB]"
            exit 1
            ;;
    esac
done

if [ -z "$VM_NAME" ]; then
    echo "ERROR: --name is required"
    echo "Usage: $0 --name VM_NAME [--kernel PATH] [--initrd PATH] [--filesystem TYPE]"
    exit 1
fi

echo "=== Creating Xibalba Test VM: $VM_NAME ==="
echo
echo "Configuration:"
echo "  Name: $VM_NAME"
echo "  RAM: ${RAM_MB}MB"
echo "  Disk: ${DISK_GB}GB"
echo "  Data disk: ${DATA_DISK_GB}GB"
echo "  Filesystem: $FILESYSTEM"
if [ -n "$CUSTOM_KERNEL" ]; then
    echo "  Custom kernel: $CUSTOM_KERNEL"
fi
echo

# Create directories
mkdir -p "$SCRIPT_DIR/images"
mkdir -p "$SCRIPT_DIR/configs"

# Download Ubuntu cloud image if needed
CLOUD_IMAGE="$SCRIPT_DIR/images/ubuntu-24.04-server-cloudimg-amd64.img"
if [ ! -f "$CLOUD_IMAGE" ]; then
    echo "Downloading Ubuntu 24.04 cloud image..."
    wget -O "$CLOUD_IMAGE" \
        https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img
    echo "  ✓ Downloaded"
fi

# Create VM disk from cloud image
VM_DISK="$SCRIPT_DIR/images/${VM_NAME}.qcow2"
if [ -f "$VM_DISK" ]; then
    echo "WARNING: $VM_DISK already exists"
    read -p "Overwrite? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
    rm "$VM_DISK"
fi

echo "Creating VM disk..."
qemu-img create -f qcow2 -F qcow2 -b "$CLOUD_IMAGE" "$VM_DISK" "${DISK_GB}G"
echo "  ✓ Created $VM_DISK"

# Create data disk for tests
DATA_DISK="$SCRIPT_DIR/images/${VM_NAME}-data.qcow2"
if [ -f "$DATA_DISK" ]; then
    rm "$DATA_DISK"
fi
echo "Creating data disk..."
qemu-img create -f qcow2 "$DATA_DISK" "${DATA_DISK_GB}G"
echo "  ✓ Created $DATA_DISK"

# Create cloud-init config
CLOUD_INIT_DIR="$SCRIPT_DIR/configs/cloud-init-${VM_NAME}"
mkdir -p "$CLOUD_INIT_DIR"

# Generate SSH key if needed
SSH_KEY="$HOME/.ssh/id_rsa.pub"
if [ ! -f "$SSH_KEY" ]; then
    echo "WARNING: No SSH key found at $SSH_KEY"
    echo "Generate one with: ssh-keygen -t rsa"
    SSH_PUBKEY=""
else
    SSH_PUBKEY=$(cat "$SSH_KEY")
fi

# Create user-data
cat > "$CLOUD_INIT_DIR/user-data" << EOF
#cloud-config
users:
  - name: root
    ssh_authorized_keys:
      - $SSH_PUBKEY
  - name: ubuntu
    sudo: ALL=(ALL) NOPASSWD:ALL
    ssh_authorized_keys:
      - $SSH_PUBKEY

packages:
  - build-essential
  - clang
  - llvm
  - libbpf-dev
  - linux-headers-generic
  - bpftool
  - python3

runcmd:
  - mkdir -p /test
  - echo "Xibalba test VM ready" > /etc/motd

power_state:
  mode: reboot
  timeout: 300
EOF

# Create meta-data
cat > "$CLOUD_INIT_DIR/meta-data" << EOF
instance-id: ${VM_NAME}
local-hostname: ${VM_NAME}
EOF

# Create network-config (simple DHCP)
cat > "$CLOUD_INIT_DIR/network-config" << EOF
version: 2
ethernets:
  enp1s0:
    dhcp4: true
EOF

# Create cloud-init ISO
CLOUD_INIT_ISO="$SCRIPT_DIR/images/${VM_NAME}-cloud-init.iso"
if [ -f "$CLOUD_INIT_ISO" ]; then
    rm "$CLOUD_INIT_ISO"
fi

echo "Creating cloud-init ISO..."
cloud-localds "$CLOUD_INIT_ISO" \
    "$CLOUD_INIT_DIR/user-data" \
    "$CLOUD_INIT_DIR/meta-data" \
    --network-config="$CLOUD_INIT_DIR/network-config"
echo "  ✓ Created cloud-init ISO"

# Install VM with virt-install
echo "Installing VM..."
virt-install \
    --name "$VM_NAME" \
    --ram "$RAM_MB" \
    --vcpus 2 \
    --disk path="$VM_DISK",format=qcow2,bus=virtio \
    --disk path="$DATA_DISK",format=qcow2,bus=virtio \
    --disk path="$CLOUD_INIT_ISO",device=cdrom \
    --os-variant ubuntu24.04 \
    --network network=default,model=virtio \
    --graphics none \
    --console pty,target_type=serial \
    --import \
    --noautoconsole

echo "  ✓ VM created and booting"
echo

# Wait for VM to boot and cloud-init to finish
echo "Waiting for VM to boot (this may take 2-3 minutes)..."
sleep 30

# Try to get VM IP
MAX_WAIT=180
WAITED=0
VM_IP=""
while [ $WAITED -lt $MAX_WAIT ]; do
    VM_IP=$(virsh domifaddr "$VM_NAME" 2>/dev/null | grep -oP '(\d+\.){3}\d+' | head -1 || true)
    if [ -n "$VM_IP" ]; then
        break
    fi
    sleep 5
    WAITED=$((WAITED + 5))
    echo -n "."
done
echo

if [ -z "$VM_IP" ]; then
    echo "⚠️  Could not determine VM IP automatically"
    echo "   Get IP with: virsh domifaddr $VM_NAME"
    echo "   Or use: virsh console $VM_NAME"
else
    echo "  ✓ VM IP: $VM_IP"
    
    # Wait for SSH to be ready
    echo "Waiting for SSH..."
    MAX_WAIT=120
    WAITED=0
    while [ $WAITED -lt $MAX_WAIT ]; do
        if ssh -o StrictHostKeyChecking=no -o ConnectTimeout=2 root@"$VM_IP" true 2>/dev/null; then
            break
        fi
        sleep 3
        WAITED=$((WAITED + 3))
        echo -n "."
    done
    echo
    
    if [ $WAITED -ge $MAX_WAIT ]; then
        echo "⚠️  SSH not responding"
        echo "   Try: virsh console $VM_NAME"
    else
        echo "  ✓ SSH ready"
        
        # Format and mount data disk
        echo "Setting up filesystem: $FILESYSTEM..."
        ssh root@"$VM_IP" bash << 'REMOTE_SCRIPT'
set -e
# Format data disk
case "$FILESYSTEM" in
    ext4)
        mkfs.ext4 -F /dev/vdb
        ;;
    xfs)
        mkfs.xfs -f /dev/vdb
        ;;
    btrfs)
        mkfs.btrfs -f /dev/vdb
        ;;
    tmpfs)
        # No formatting needed, will mount tmpfs
        ;;
    *)
        echo "Unknown filesystem: $FILESYSTEM"
        exit 1
        ;;
esac

# Mount filesystem
mkdir -p /test
if [ "$FILESYSTEM" = "tmpfs" ]; then
    mount -t tmpfs -o size=1G tmpfs /test
else
    mount /dev/vdb /test
fi

# Add to fstab
if [ "$FILESYSTEM" = "tmpfs" ]; then
    echo "tmpfs /test tmpfs defaults,size=1G 0 0" >> /etc/fstab
else
    echo "/dev/vdb /test $FILESYSTEM defaults 0 2" >> /etc/fstab
fi

# Verify
df -h /test
REMOTE_SCRIPT
        echo "  ✓ Filesystem mounted"
    fi
fi

echo
echo "=== VM Creation Complete ==="
echo
echo "VM Details:"
echo "  Name: $VM_NAME"
if [ -n "$VM_IP" ]; then
    echo "  IP: $VM_IP"
    echo "  SSH: ssh root@$VM_IP"
fi
echo "  Console: virsh console $VM_NAME"
echo "  Status: virsh dominfo $VM_NAME"
echo
echo "Next steps:"
echo "  1. Deploy Xibalba: $SCRIPT_DIR/deploy_xibalba.sh $VM_NAME"
echo "  2. Run tests: $SCRIPT_DIR/run_tests.sh $VM_NAME"
echo



