#!/bin/bash
# TocinOS QEMU Test Helper
# Runs OS in QEMU with various test configurations

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "================================================"
echo "  TocinOS QEMU Test Helper"
echo "================================================"
echo ""

# Check if OS image exists
if [ ! -f "build/TocinOS.img" ]; then
    echo -e "${YELLOW}[WARN]${NC} OS image not found. Building..."
    make all
fi

# Parse command line options
TEST_MODE=${1:-normal}

case $TEST_MODE in
    normal)
        echo -e "${GREEN}[INFO]${NC} Running in normal mode..."
        qemu-system-i386 \
            -drive format=raw,file=build/TocinOS.img \
            -serial stdio
        ;;
    
    headless)
        echo -e "${GREEN}[INFO]${NC} Running in headless mode..."
        qemu-system-i386 \
            -drive format=raw,file=build/TocinOS.img \
            -display none \
            -serial stdio
        ;;
    
    x64)
        echo -e "${GREEN}[INFO]${NC} Running in 64-bit mode..."
        qemu-system-x86_64 \
            -drive format=raw,file=build/TocinOS.img \
            -serial stdio
        ;;
    
    test)
        echo -e "${GREEN}[INFO]${NC} Running in test mode (5s timeout)..."
        timeout 5 qemu-system-i386 \
            -drive format=raw,file=build/TocinOS.img \
            -display none \
            -serial stdio \
            -no-reboot || true
        ;;
    
    monitor)
        echo -e "${GREEN}[INFO]${NC} Running with QEMU monitor..."
        qemu-system-i386 \
            -drive format=raw,file=build/TocinOS.img \
            -serial stdio \
            -monitor stdio
        ;;
    
    *)
        echo "Usage: $0 [normal|headless|x64|test|monitor]"
        echo ""
        echo "Modes:"
        echo "  normal    - Normal QEMU with display"
        echo "  headless  - No display, serial only"
        echo "  x64       - Run 64-bit version"
        echo "  test      - Quick 5-second test"
        echo "  monitor   - Run with QEMU monitor"
        exit 1
        ;;
esac
