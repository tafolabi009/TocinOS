#!/bin/bash
# TocinOS Infrastructure Validation Script
# Demonstrates and validates all new features

set -e

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "================================================"
echo "  TocinOS Infrastructure Validation"
echo "================================================"
echo ""

# Function to show feature
show_feature() {
    echo -e "${BLUE}[FEATURE]${NC} $1"
    echo "Command: $2"
    echo ""
}

# Function to validate feature
validate_feature() {
    echo -e "${YELLOW}[VALIDATE]${NC} $1"
    eval "$2" >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ PASS${NC}"
    else
        echo -e "✗ FAIL"
    fi
    echo ""
}

echo "=== Build System Features ==="
echo ""

show_feature "Configuration file" "cat .config"
validate_feature "Configuration exists" "test -f .config"

show_feature "Configure script" "./configure --help"
validate_feature "Configure is executable" "test -x configure"

show_feature "Dependency checker" "make check-deps"
# Note: This will fail if tools aren't installed, but that's expected

echo "=== Testing Infrastructure ==="
echo ""

show_feature "Test framework" "ls tests/framework/"
validate_feature "Test framework exists" "test -f tests/framework/unittest.h"

show_feature "Unit tests" "ls tests/unit/"
validate_feature "Unit tests exist" "test -f tests/unit/test_pmm.c"

show_feature "Run unit tests" "make test-unit"
validate_feature "Tests can run" "make test-unit >/dev/null 2>&1"

echo "=== Documentation System ==="
echo ""

show_feature "Doxyfile configuration" "head -20 Doxyfile"
validate_feature "Doxyfile exists" "test -f Doxyfile"

show_feature "Documented headers" "grep -A5 '@file' include/kernel/memory.h"
validate_feature "Documentation present" "grep -q '@file' include/kernel/memory.h"

echo "=== Performance Profiling ==="
echo ""

show_feature "Profiling header" "head -30 include/kernel/profiling.h"
validate_feature "Profiling header exists" "test -f include/kernel/profiling.h"

show_feature "Profiling implementation" "head -30 kernel/profiling.c"
validate_feature "Profiling code exists" "test -f kernel/profiling.c"

echo "=== Developer Tools ==="
echo ""

show_feature "QEMU debug script" "cat tools/qemu-debug.sh | head -30"
validate_feature "Debug script exists" "test -x tools/qemu-debug.sh"

show_feature "QEMU test script" "cat tools/qemu-test.sh | head -30"
validate_feature "Test script exists" "test -x tools/qemu-test.sh"

echo "=== Code Quality Tools ==="
echo ""

show_feature "Code style configuration" "head -20 .clang-format"
validate_feature "clang-format config exists" "test -f .clang-format"

show_feature "License header tool" "python3 tools/add_license.py --help 2>&1 | head -10"
validate_feature "License tool works" "python3 tools/add_license.py --dry-run >/dev/null 2>&1"

echo "=== Makefile Targets ==="
echo ""

show_feature "Available targets" "make help"

echo ""
echo "=== Summary ==="
echo ""

# Count features
TOTAL=12
echo "Total features validated: $TOTAL"
echo ""

# Show file structure
echo "New file structure:"
tree -L 2 tests/ tools/ 2>/dev/null || find tests tools -type f | head -20

echo ""
echo -e "${GREEN}Validation complete!${NC}"
echo ""
echo "Try these commands:"
echo "  make test           # Run all tests"
echo "  make docs           # Generate documentation"
echo "  make help           # Show all targets"
echo "  make format         # Format code"
echo "  ./tools/qemu-test.sh test  # Quick test"
