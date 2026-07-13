# TocinOS Makefile
# Builds the operating system for both x86 and x86-64 architectures

# Architecture selection (x86 or x86_64)
ARCH ?= x86

# Verbose mode (V=1 for verbose output)
V ?= 0

# Compiler and tools
AS = nasm
CC = gcc
LD = ld
OBJCOPY = objcopy

# Directories
BUILD_DIR = build
BOOT_DIR = boot
KERNEL_DIR = kernel
INCLUDE_DIR = include

# Quiet/verbose build mode
ifeq ($(V),1)
    Q =
    msg = @printf "  %-7s %s\n"
else
    Q = @
    msg = @printf "  %-7s %s\n"
endif

# Architecture-specific settings
ifeq ($(ARCH),x86_64)
    ARCH_DIR = $(KERNEL_DIR)/arch/x86_64
    CFLAGS = -m64 -ffreestanding -fno-pie -nostdlib -nostdinc -fno-builtin -fno-stack-protector -mno-red-zone
    LDFLAGS = -m elf_x86_64 -T linker_x86_64.ld
    ASFLAGS = -f elf64
else
    ARCH_DIR = $(KERNEL_DIR)/arch/x86
    CFLAGS = -m32 -ffreestanding -fno-pie -nostdlib -nostdinc -fno-builtin -fno-stack-protector
    LDFLAGS = -m elf_i386 -T linker_x86.ld
    ASFLAGS = -f elf32
endif

CFLAGS += -Wall -Wextra -I$(INCLUDE_DIR)

# Load configuration if it exists
-include .config

# Source files
BOOT_MBR = $(BOOT_DIR)/mbr/mbr.asm
BOOT_STAGE2 = $(BOOT_DIR)/stage2/stage2.asm
KERNEL_ENTRY = $(ARCH_DIR)/entry.asm
KERNEL_ISR_ASM = $(ARCH_DIR)/isr_asm.asm
KERNEL_C_SOURCES = $(wildcard $(KERNEL_DIR)/*.c) \
                   $(wildcard $(KERNEL_DIR)/mm/*.c) \
                   $(wildcard $(KERNEL_DIR)/task/*.c) \
                   $(wildcard $(KERNEL_DIR)/fs/*.c) \
                   $(wildcard $(KERNEL_DIR)/net/*.c) \
                   $(wildcard $(KERNEL_DIR)/drivers/*.c) \
                   $(wildcard $(KERNEL_DIR)/drivers/net/*.c)

# USB sources (handled separately due to subdirectory)
USB_SOURCES = $(wildcard $(KERNEL_DIR)/drivers/usb/*.c)
USB_OBJS = $(patsubst $(KERNEL_DIR)/drivers/usb/%.c,$(BUILD_DIR)/%.o,$(USB_SOURCES))

# Object files
KERNEL_ENTRY_OBJ = $(BUILD_DIR)/entry.o
KERNEL_ISR_ASM_OBJ = $(BUILD_DIR)/isr_asm.o
KERNEL_C_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(KERNEL_C_SOURCES))) $(USB_OBJS) 

# Output files
MBR_BIN = $(BUILD_DIR)/mbr.bin
STAGE2_BIN = $(BUILD_DIR)/stage2.bin
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
OS_IMAGE = $(BUILD_DIR)/TocinOS.img

.PHONY: all clean directories check-deps test test-unit test-integration docs docs-serve help

all: check-deps directories
	@echo ""
	@START_TIME=$$(date +%s); \
	$(MAKE) --no-print-directory $(OS_IMAGE); \
	END_TIME=$$(date +%s); \
	BUILD_TIME=$$((END_TIME - START_TIME)); \
	echo ""; \
	echo "Build complete in $${BUILD_TIME}s!"; \
	echo ""

directories:
	@mkdir -p $(BUILD_DIR)

# Check build dependencies
.PHONY: check-deps
check-deps:
	@echo "Checking build dependencies..."
	@which $(AS) >/dev/null || (echo "ERROR: $(AS) not found - install nasm" && exit 1)
	@which $(CC) >/dev/null || (echo "ERROR: $(CC) not found - install gcc" && exit 1)
	@which $(LD) >/dev/null || (echo "ERROR: $(LD) not found - install binutils" && exit 1)
	@which $(OBJCOPY) >/dev/null || (echo "ERROR: $(OBJCOPY) not found - install binutils" && exit 1)
	@echo "All dependencies found!"

# Build MBR
$(MBR_BIN): $(BOOT_MBR)
	$(msg) "AS" "$@"
	$(Q)$(AS) -f bin $(BOOT_MBR) -o $(MBR_BIN)

# Build Stage 2 bootloader
$(STAGE2_BIN): $(BOOT_STAGE2)
	$(msg) "AS" "$@"
	$(Q)$(AS) -f bin $(BOOT_STAGE2) -o $(STAGE2_BIN)

# Build kernel entry
$(KERNEL_ENTRY_OBJ): $(KERNEL_ENTRY)
	$(msg) "AS" "$@"
	$(Q)$(AS) $(ASFLAGS) $(KERNEL_ENTRY) -o $(KERNEL_ENTRY_OBJ)

# Build ISR assembly
$(KERNEL_ISR_ASM_OBJ): $(KERNEL_ISR_ASM)
	$(msg) "AS" "$@"
	$(Q)$(AS) $(ASFLAGS) $(KERNEL_ISR_ASM) -o $(KERNEL_ISR_ASM_OBJ)

# Build kernel C files
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c
	$(msg) "CC" "$<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/mm/%.c
	$(msg) "CC" "$<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/task/%.c
	$(msg) "CC" "$<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/drivers/%.c
	$(msg) "CC" "$<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@


$(BUILD_DIR)/%.o: $(KERNEL_DIR)/drivers/usb/%.c
	$(msg) "CC" "$<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/drivers/net/%.c
	$(msg) "CC" "$<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/net/%.c
	$(msg) "CC" "$<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/fs/%.c
	$(msg) "CC" "$<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Link kernel
$(KERNEL_ELF): $(KERNEL_ENTRY_OBJ) $(KERNEL_ISR_ASM_OBJ) $(KERNEL_C_OBJS)
	$(msg) "LD" "$@"
	$(Q)$(LD) $(LDFLAGS) -o $(KERNEL_ELF) $(KERNEL_ENTRY_OBJ) $(KERNEL_ISR_ASM_OBJ) $(KERNEL_C_OBJS)

# Convert ELF to flat binary
$(KERNEL_BIN): $(KERNEL_ELF)
	$(msg) "OBJCOPY" "$@"
	$(Q)$(OBJCOPY) -O binary $(KERNEL_ELF) $(KERNEL_BIN)

# Create OS image
$(OS_IMAGE): $(MBR_BIN) $(STAGE2_BIN) $(KERNEL_BIN)
	$(msg) "IMAGE" "$@"
	$(Q)dd if=/dev/zero of=$(OS_IMAGE) bs=512 count=2880 2>/dev/null
	$(Q)dd if=$(MBR_BIN) of=$(OS_IMAGE) bs=512 count=1 conv=notrunc 2>/dev/null
	$(Q)dd if=$(STAGE2_BIN) of=$(OS_IMAGE) bs=512 seek=1 count=16 conv=notrunc 2>/dev/null
	$(Q)dd if=$(KERNEL_BIN) of=$(OS_IMAGE) bs=512 seek=17 conv=notrunc 2>/dev/null

# Testing targets
.PHONY: test test-unit test-integration

test: test-unit test-integration

test-unit:
	@echo "Running unit tests..."
	@if [ -d tests/unit ]; then \
		mkdir -p $(BUILD_DIR); \
		$(CC) -Itests/framework tests/test_runner.c tests/unit/*.c tests/framework/*.c -o $(BUILD_DIR)/test_runner 2>&1 || \
		(echo "Failed to compile tests" && exit 1); \
		$(BUILD_DIR)/test_runner; \
	else \
		echo "No unit tests found. Run 'make setup-tests' to create test infrastructure."; \
	fi

test-integration:
	@echo "Running integration tests..."
	@if [ -d tests/integration ]; then \
		for test in tests/integration/*.sh; do \
			[ -f "$$test" ] && bash "$$test"; \
		done \
	else \
		echo "No integration tests found."; \
	fi

# Documentation targets
.PHONY: docs docs-serve docs-clean

docs:
	@echo "Generating documentation..."
	@if command -v doxygen >/dev/null 2>&1; then \
		doxygen Doxyfile 2>/dev/null || echo "Doxyfile not configured. Run './configure --enable-docs'"; \
	else \
		echo "Doxygen not installed. Install with: sudo apt-get install doxygen graphviz"; \
	fi

docs-serve: docs
	@echo "Serving documentation at http://localhost:8000"
	@if [ -d docs/html ]; then \
		cd docs/html && python3 -m http.server; \
	else \
		echo "No documentation generated. Run 'make docs' first."; \
	fi

docs-clean:
	@echo "Cleaning documentation..."
	@rm -rf docs/html docs/latex

# Run in QEMU
run: all
	@echo "Running TocinOS in QEMU..."
	qemu-system-i386 -drive format=raw,file=$(OS_IMAGE)

run64: ARCH=x86_64
run64: all
	@echo "Running TocinOS in QEMU (64-bit)..."
	qemu-system-x86_64 -drive format=raw,file=$(OS_IMAGE)

# Clean build artifacts
clean:
	$(msg) "CLEAN" "build artifacts"
	$(Q)rm -rf $(BUILD_DIR)

clean-all: clean docs-clean
	$(msg) "CLEAN" "all generated files"

# Setup helpers
.PHONY: setup-tests setup-docs

setup-tests:
	@echo "Setting up test infrastructure..."
	@mkdir -p tests/unit tests/integration tests/framework
	@echo "Test directories created. Add your test files to tests/unit/ and tests/integration/"

setup-docs:
	@echo "Setting up documentation..."
	@if command -v doxygen >/dev/null 2>&1; then \
		doxygen -g Doxyfile 2>/dev/null; \
		echo "Doxyfile created. Edit it to configure documentation generation."; \
	else \
		echo "Doxygen not installed. Install with: sudo apt-get install doxygen graphviz"; \
	fi

# Code quality targets
.PHONY: format check-format analyze

format:
	@echo "Formatting code..."
	@if command -v clang-format >/dev/null 2>&1; then \
		find kernel include -name '*.c' -o -name '*.h' | xargs clang-format -i; \
		echo "Code formatted successfully"; \
	else \
		echo "clang-format not installed. Install with: sudo apt-get install clang-format"; \
	fi

check-format:
	@echo "Checking code format..."
	@if command -v clang-format >/dev/null 2>&1; then \
		find kernel include -name '*.c' -o -name '*.h' | xargs clang-format --dry-run --Werror; \
	else \
		echo "clang-format not installed. Install with: sudo apt-get install clang-format"; \
	fi

analyze:
	@echo "Running static analysis..."
	@if command -v scan-build >/dev/null 2>&1; then \
		scan-build -o analysis make clean all; \
		echo "Analysis complete. Results in analysis/"; \
	else \
		echo "scan-build not installed. Install with: sudo apt-get install clang-tools"; \
	fi

# Add license headers
.PHONY: add-license check-license

add-license:
	@echo "Adding license headers..."
	@python3 tools/add_license.py

check-license:
	@echo "Checking license headers..."
	@python3 tools/add_license.py --dry-run


# Help
help:
	@echo "TocinOS Build System"
	@echo "===================="
	@echo ""
	@echo "Build Targets:"
	@echo "  all          - Build the OS image (default)"
	@echo "  clean        - Remove build artifacts"
	@echo "  clean-all    - Remove all generated files including docs"
	@echo "  check-deps   - Check build dependencies"
	@echo ""
	@echo "Testing Targets:"
	@echo "  test         - Run all tests"
	@echo "  test-unit    - Run unit tests only"
	@echo "  test-integration - Run integration tests only"
	@echo "  setup-tests  - Create test infrastructure"
	@echo ""
	@echo "Documentation Targets:"
	@echo "  docs         - Generate documentation with Doxygen"
	@echo "  docs-serve   - Generate and serve documentation"
	@echo "  docs-clean   - Remove generated documentation"
	@echo "  setup-docs   - Create Doxyfile configuration"
	@echo ""
	@echo "Code Quality Targets:"
	@echo "  format       - Format code with clang-format"
	@echo "  check-format - Check code formatting"
	@echo "  analyze      - Run static analysis with scan-build"
	@echo "  add-license  - Add license headers to source files"
	@echo "  check-license - Check which files need license headers"
	@echo ""
	@echo "Run Targets:"
	@echo "  run          - Build and run in QEMU (32-bit)"
	@echo "  run64        - Build and run in QEMU (64-bit)"
	@echo ""
	@echo "Variables:"
	@echo "  ARCH=x86|x86_64  - Target architecture (default: x86)"
	@echo "  V=1              - Verbose build output"
	@echo ""
	@echo "Examples:"
	@echo "  make                 # Build for x86"
	@echo "  make ARCH=x86_64     # Build for x86-64"
	@echo "  make V=1             # Build with verbose output"
	@echo "  make run             # Build and run x86 version"
	@echo "  make run64           # Build and run x86-64 version"
	@echo "  make test            # Run all tests"
	@echo "  make docs            # Generate documentation"
	@echo "  make format          # Format all source code"
	@echo "  make analyze         # Run static analysis"

