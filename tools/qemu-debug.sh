#!/bin/bash
# TocinOS QEMU Debug Helper
# Starts QEMU with GDB server for debugging

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "================================================"
echo "  TocinOS QEMU Debug Helper"
echo "================================================"
echo ""

# Check if OS image exists
if [ ! -f "build/TocinOS.img" ]; then
    echo -e "${RED}[ERROR]${NC} OS image not found. Building..."
    make ARCH=x86_64 CFLAGS="-g -O0"
fi

# Check if kernel.elf exists for debugging
if [ ! -f "build/kernel.elf" ]; then
    echo -e "${YELLOW}[WARN]${NC} kernel.elf not found. Debugging symbols may be unavailable."
fi

# QEMU options
QEMU_ARCH=${QEMU_ARCH:-qemu-system-x86_64}
GDB_PORT=${GDB_PORT:-1234}
MONITOR_PORT=${MONITOR_PORT:-4444}

echo -e "${GREEN}[INFO]${NC} Starting QEMU with GDB server..."
echo "  GDB port:     tcp::${GDB_PORT}"
echo "  Monitor port: tcp::${MONITOR_PORT}"
echo ""

# Start QEMU with debugging enabled
$QEMU_ARCH \
    -drive format=raw,file=build/TocinOS.img \
    -serial stdio \
    -gdb tcp::${GDB_PORT} \
    -S \
    -monitor telnet::${MONITOR_PORT},server,nowait \
    -no-reboot \
    -no-shutdown &

QEMU_PID=$!
echo -e "${GREEN}[INFO]${NC} QEMU started with PID $QEMU_PID"

# Wait for QEMU to be ready
sleep 2

# Start GDB if available
if command -v gdb >/dev/null 2>&1; then
    echo -e "${GREEN}[INFO]${NC} Starting GDB..."
    echo ""
    
    if [ -f "build/kernel.elf" ]; then
        gdb build/kernel.elf \
            -ex "target remote localhost:${GDB_PORT}" \
            -ex "break kernel_main" \
            -ex "continue"
    else
        gdb \
            -ex "target remote localhost:${GDB_PORT}" \
            -ex "break *0x100000" \
            -ex "continue"
    fi
else
    echo -e "${YELLOW}[WARN]${NC} GDB not found. Connect manually:"
    echo "  gdb build/kernel.elf"
    echo "  (gdb) target remote localhost:${GDB_PORT}"
    echo ""
    echo "Press Ctrl+C to stop QEMU..."
    wait $QEMU_PID
fi

# Cleanup
echo ""
echo -e "${GREEN}[INFO]${NC} Cleaning up..."
kill $QEMU_PID 2>/dev/null || true
