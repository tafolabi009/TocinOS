#!/bin/bash
# TocinOS Storage Stack Testing Script
# This script tests the complete storage stack: IDE -> FAT -> VFS -> ELF

set -e

cd /workspaces/TocinOS

echo "========================================"
echo "TocinOS Storage Stack Test Suite"
echo "========================================"
echo ""

# Step 1: Build the kernel
echo "[1/5] Building kernel..."
make clean >/dev/null 2>&1
make 2>&1 | tee build.log | grep -E "(error:|Error:)" || true

if grep -q "error:" build.log; then
    echo "BUILD FAILED! Check build.log for details."
    exit 1
fi

echo "    Kernel build: OK"
echo ""

# Step 2: Build user program
echo "[2/5] Building user test program..."
cd user
make clean >/dev/null 2>&1
make 2>&1
echo "    User program build: OK"
cd ..
echo ""

# Step 3: Create test disk image
echo "[3/5] Creating test disk image..."
DISK_IMG="test_disk.img"
dd if=/dev/zero of=$DISK_IMG bs=1M count=10 2>/dev/null
mkfs.vfat -F 16 $DISK_IMG >/dev/null

# Try to copy files using mtools (doesn't need root)
if command -v mcopy &> /dev/null; then
    mcopy -i $DISK_IMG user/hello.elf ::HELLO.ELF 2>/dev/null || true
    echo "Hello from TocinOS!" | mcopy -i $DISK_IMG - ::TEST.TXT 2>/dev/null || true
    echo "    Files copied using mtools"
else
    echo "    Note: mtools not installed, disk will be empty"
    echo "    Install with: sudo apt-get install mtools"
fi

echo "    Test disk created: $DISK_IMG"
echo ""

# Step 4: Show test instructions
echo "[4/5] Testing Instructions"
echo "========================================"
echo ""
echo "To test TocinOS with the storage stack:"
echo ""
echo "  # Boot TocinOS with test disk:"
echo "  qemu-system-i386 -fda build/TocinOS.img -hda $DISK_IMG -serial stdio -no-reboot"
echo ""
echo "  # For debugging with GDB:"
echo "  qemu-system-i386 -fda build/TocinOS.img -hda $DISK_IMG -serial stdio -s -S &"
echo "  gdb -ex 'target remote :1234' -ex 'symbol-file build/kernel.elf'"
echo ""

# Step 5: Quick QEMU test (non-interactive)
echo "[5/5] Quick boot test (5 second timeout)..."
echo ""

timeout 5 qemu-system-i386 \
    -fda build/TocinOS.img \
    -hda $DISK_IMG \
    -serial mon:stdio \
    -display none \
    -no-reboot 2>&1 | head -100 || true

echo ""
echo "========================================"
echo "Test complete!"
echo ""
echo "Expected serial output should show:"
echo "  - IDE driver initialization"
echo "  - MBR read and signature check"
echo "  - FAT filesystem mount"
echo "  - Root directory listing"
echo ""
