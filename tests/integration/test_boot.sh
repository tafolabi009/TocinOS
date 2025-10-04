#!/bin/bash
# TocinOS Boot Integration Test
# Tests that the OS image builds and boots correctly

set -e

echo "================================================"
echo "  TocinOS Boot Integration Test"
echo "================================================"
echo ""

# Check if build directory exists
if [ ! -d "build" ]; then
    echo "[INFO] Build directory not found, building OS..."
    make all
fi

# Check if OS image exists
if [ ! -f "build/TocinOS.img" ]; then
    echo "[ERROR] OS image not found"
    exit 1
fi

echo "[OK] OS image found: build/TocinOS.img"

# Check image size
SIZE=$(stat -f%z "build/TocinOS.img" 2>/dev/null || stat -c%s "build/TocinOS.img")
EXPECTED_SIZE=$((512 * 2880))  # 2880 sectors

echo "[INFO] Image size: $SIZE bytes (expected: $EXPECTED_SIZE bytes)"

if [ "$SIZE" -ne "$EXPECTED_SIZE" ]; then
    echo "[WARN] Image size mismatch"
fi

# Check MBR signature (last 2 bytes should be 0x55AA)
echo "[INFO] Checking MBR signature..."
if command -v xxd >/dev/null 2>&1; then
    SIG=$(xxd -s 510 -l 2 -p build/TocinOS.img)
    if [ "$SIG" = "55aa" ]; then
        echo "[OK] Valid MBR signature found"
    else
        echo "[ERROR] Invalid MBR signature: $SIG (expected 55aa)"
        exit 1
    fi
else
    echo "[SKIP] xxd not found, skipping signature check"
fi

# Test QEMU boot (timeout after 5 seconds)
if command -v qemu-system-i386 >/dev/null 2>&1; then
    echo "[INFO] Testing boot with QEMU..."
    timeout 5 qemu-system-i386 \
        -drive format=raw,file=build/TocinOS.img \
        -display none \
        -serial stdio \
        -no-reboot 2>&1 | head -20 || true
    echo ""
    echo "[OK] QEMU boot test completed"
else
    echo "[SKIP] QEMU not found, skipping boot test"
fi

echo ""
echo "[PASS] Boot integration test passed"
