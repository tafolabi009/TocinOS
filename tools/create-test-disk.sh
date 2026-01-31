#!/bin/bash
# Script to create a FAT disk image for testing TocinOS

set -e

DISK_IMG="disk.img"
DISK_SIZE_MB=10
MOUNT_POINT="/tmp/tocinos_disk"

echo "Creating ${DISK_SIZE_MB}MB FAT disk image..."

# Create empty disk image
dd if=/dev/zero of=$DISK_IMG bs=1M count=$DISK_SIZE_MB 2>/dev/null

# Format as FAT (FAT16 for smaller images, FAT32 for larger)
if [ $DISK_SIZE_MB -lt 32 ]; then
    mkfs.vfat -F 16 $DISK_IMG
else
    mkfs.vfat -F 32 $DISK_IMG
fi

echo "Disk image created: $DISK_IMG"

# Check if we have a hello.elf to copy
if [ -f "user/hello.elf" ]; then
    echo "Mounting disk image..."
    mkdir -p $MOUNT_POINT
    
    # Try to mount (may require sudo)
    if sudo mount -o loop $DISK_IMG $MOUNT_POINT 2>/dev/null; then
        echo "Copying hello.elf to disk..."
        sudo cp user/hello.elf $MOUNT_POINT/HELLO.ELF
        
        # Create a test text file
        echo "Hello from TocinOS test file!" | sudo tee $MOUNT_POINT/TEST.TXT > /dev/null
        
        # List contents
        echo "Disk contents:"
        ls -la $MOUNT_POINT
        
        sudo umount $MOUNT_POINT
        rmdir $MOUNT_POINT
        echo "Files copied successfully!"
    else
        echo "Could not mount disk image (may need root access)"
        echo "You can manually mount with: sudo mount -o loop $DISK_IMG /mnt"
    fi
else
    echo "Note: user/hello.elf not found. Build it with: cd user && make"
fi

echo ""
echo "To use this disk with QEMU:"
echo "  qemu-system-i386 -fda build/TocinOS.img -hda disk.img -serial stdio"
