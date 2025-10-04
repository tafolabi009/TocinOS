# TocinOS Development Infrastructure Guide

Welcome to the TocinOS development infrastructure! This guide will help you get started with the enhanced build system, testing framework, and development tools.

## Quick Start

### 1. Initial Setup

```bash
# Configure the build system
./configure --arch=x86_64 --enable-tests --enable-docs

# Check dependencies
make check-deps

# Build the OS
make
```

### 2. Running Tests

```bash
# Run all tests
make test

# Run only unit tests
make test-unit

# Run only integration tests
make test-integration
```

### 3. Development Workflow

```bash
# Make changes to code
vim kernel/memory.c

# Format your code
make format

# Run tests
make test

# Build and test
make run
```

## Features Overview

### Build System

The enhanced build system provides:
- **Configuration management** via `.config` and `./configure`
- **Dependency checking** to validate required tools
- **Build time measurement** to track compilation speed
- **Verbose mode** (`make V=1`) for detailed output

### Testing Framework

Comprehensive testing infrastructure with:
- **Unit test framework** (`tests/framework/unittest.h`)
- **16 passing unit tests** covering:
  - Physical Memory Manager (PMM)
  - Virtual Memory Manager (VMM)
  - Task Scheduler
  - FAT Filesystem
- **Integration tests** for boot validation
- **Colored output** for easy test result reading

Example test:
```c
TEST_CASE(allocation_basic)
    pmm_init();
    uint32_t page1 = pmm_alloc_page();
    ASSERT_NE(page1, 0, "Should allocate valid page");
    pmm_free_page(page1);
END_TEST_CASE()
```

### Documentation System

Professional documentation with:
- **Doxygen configuration** for API documentation
- **Documented headers** with `@file`, `@brief`, `@param`, `@return` tags
- **Code examples** in documentation
- **HTML documentation generation**

Generate docs:
```bash
make docs           # Generate documentation
make docs-serve     # Serve at http://localhost:8000
```

### Performance Profiling

Kernel profiling infrastructure:
- **Sample-based profiling** from timer interrupts
- **10,000 sample capacity** per session
- **Statistics tracking** (samples, drops, timing)

API:
```c
#include "kernel/profiling.h"

profile_start();
// ... code to profile ...
profile_stop();

profiler_t stats;
profile_get_stats(&stats);
```

### Developer Tools

#### QEMU Debug Helper

Debug your OS with GDB:
```bash
./tools/qemu-debug.sh
```

Features:
- Automatic GDB connection
- Pre-set breakpoints
- Monitor interface
- Symbol loading

#### QEMU Test Helper

Test in various modes:
```bash
./tools/qemu-test.sh [mode]
```

Modes:
- `normal` - Standard QEMU with display
- `headless` - No display, serial only
- `x64` - 64-bit mode
- `test` - Quick 5-second test
- `monitor` - With QEMU monitor

### Code Quality Tools

#### Code Formatting

Consistent code style with clang-format:
```bash
make format         # Format all code
make check-format   # Check formatting (CI-ready)
```

Configuration: `.clang-format` (Linux style, 100 columns)

#### Static Analysis

Find bugs with clang static analyzer:
```bash
make analyze
```

Results saved to `analysis/` directory.

#### License Management

Automated license header management:
```bash
make add-license     # Add headers to all files
make check-license   # Dry-run mode
```

Or use directly:
```bash
python3 tools/add_license.py --dry-run
```

## Makefile Targets Reference

### Build Targets
- `make all` - Build the OS (default)
- `make clean` - Remove build artifacts
- `make clean-all` - Remove all generated files
- `make check-deps` - Check build dependencies

### Testing Targets
- `make test` - Run all tests
- `make test-unit` - Run unit tests only
- `make test-integration` - Run integration tests
- `make setup-tests` - Create test infrastructure

### Documentation Targets
- `make docs` - Generate documentation
- `make docs-serve` - Serve documentation locally
- `make docs-clean` - Clean documentation artifacts
- `make setup-docs` - Setup Doxygen

### Code Quality Targets
- `make format` - Format code
- `make check-format` - Check formatting
- `make analyze` - Run static analysis
- `make add-license` - Add license headers
- `make check-license` - Check license headers

### Run Targets
- `make run` - Build and run (32-bit)
- `make run64` - Build and run (64-bit)

### Help
- `make help` - Show all targets

## File Structure

```
TocinOS/
├── .clang-format              # Code style config
├── .config                    # Build configuration
├── configure                  # Configuration script
├── Doxyfile                   # Doxygen config
├── Makefile                   # Enhanced build system
│
├── tests/
│   ├── framework/            # Test framework
│   │   ├── unittest.h        # Test macros
│   │   ├── unittest.c        # Test runner
│   │   └── mocks.c           # Mock functions
│   ├── unit/                 # Unit tests
│   │   ├── test_pmm.c
│   │   ├── test_vmm.c
│   │   ├── test_scheduler.c
│   │   └── test_fat.c
│   ├── integration/          # Integration tests
│   │   └── test_boot.sh
│   └── test_runner.c         # Main test runner
│
├── tools/
│   ├── qemu-debug.sh         # Debug helper
│   ├── qemu-test.sh          # Test helper
│   ├── add_license.py        # License tool
│   └── validate-infrastructure.sh  # Validation
│
├── include/kernel/
│   ├── profiling.h           # Profiling API
│   └── memory.h              # Documented memory API
│
└── kernel/
    └── profiling.c           # Profiling implementation
```

## Writing Tests

### Unit Test Example

```c
#include "../framework/unittest.h"

TEST_SUITE(my_tests)
    
    TEST_CASE(test_something)
        int result = my_function();
        ASSERT_EQ(result, 42, "Function should return 42");
    END_TEST_CASE()
    
    TEST_CASE(test_something_else)
        void *ptr = my_alloc();
        ASSERT_NE(ptr, NULL, "Allocation should succeed");
        my_free(ptr);
    END_TEST_CASE()

END_TEST_SUITE()
```

### Integration Test Example

```bash
#!/bin/bash
# tests/integration/test_myfeature.sh

echo "[TEST] My Feature Test"

# Build if needed
make all >/dev/null 2>&1

# Run test
timeout 5 qemu-system-i386 \
    -drive format=raw,file=build/TocinOS.img \
    -display none \
    -serial stdio \
    | grep "Expected Output" || exit 1

echo "[PASS] Test completed"
```

## Continuous Integration

Example CI configuration:

```yaml
# .github/workflows/ci.yml
name: CI

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y nasm gcc binutils make \
            qemu-system-x86 doxygen clang-format clang-tools
      
      - name: Configure
        run: ./configure --enable-tests --enable-docs
      
      - name: Check dependencies
        run: make check-deps
      
      - name: Check formatting
        run: make check-format
      
      - name: Run tests
        run: make test
      
      - name: Build documentation
        run: make docs
      
      - name: Static analysis
        run: make analyze
```

## Best Practices

### Before Committing

1. Format your code:
   ```bash
   make format
   ```

2. Run tests:
   ```bash
   make test
   ```

3. Check for issues:
   ```bash
   make check-format
   make analyze
   ```

### Writing Documentation

Use Doxygen format for all public APIs:

```c
/**
 * @brief Brief description
 * 
 * Detailed description of what the function does.
 * 
 * @param name Parameter description
 * @return Return value description
 * 
 * @code
 * // Usage example
 * int result = my_function(42);
 * @endcode
 */
int my_function(int name);
```

### Adding Tests

1. Create test file in `tests/unit/test_myfeature.c`
2. Use `TEST_SUITE` and `TEST_CASE` macros
3. Add to `test_runner.c`
4. Run with `make test-unit`

## Troubleshooting

### Build Issues

```bash
# Check dependencies
make check-deps

# Clean and rebuild
make clean
make V=1  # Verbose to see errors
```

### Test Failures

```bash
# Run tests with verbose output
make test-unit

# Run specific test file
gcc -Itests/framework tests/test_runner.c tests/unit/test_pmm.c \
    tests/framework/*.c -o test_runner
./test_runner
```

### Documentation Issues

```bash
# Regenerate Doxyfile
make setup-docs

# Check for warnings
make docs
```

## Additional Resources

- [INFRASTRUCTURE_IMPLEMENTATION.md](INFRASTRUCTURE_IMPLEMENTATION.md) - Complete implementation details
- [NEXT_STEPS.md](NEXT_STEPS.md) - Original requirements
- [Makefile](Makefile) - All available targets
- [Doxyfile](Doxyfile) - Documentation configuration

## Getting Help

1. Check `make help` for available targets
2. Read the error messages carefully
3. Run with `V=1` for verbose output
4. Review test output for failures
5. Check documentation at `docs/html/` after `make docs`

## Contributing

When contributing to TocinOS:

1. Follow the code style (`.clang-format`)
2. Add tests for new features
3. Document public APIs with Doxygen
4. Ensure all tests pass
5. Run static analysis
6. Add license headers to new files

```bash
# Standard workflow
make format
make test
make check-format
make analyze
```

## Summary

The TocinOS development infrastructure provides:

✅ Professional build system with configuration
✅ Comprehensive testing framework (16 tests)
✅ API documentation with Doxygen
✅ Performance profiling infrastructure
✅ Debugging tools and QEMU helpers
✅ Code quality tools (format, analyze, license)
✅ CI/CD ready workflows

Start developing with confidence!
