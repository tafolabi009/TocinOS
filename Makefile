# TocinOS Makefile
# Builds the operating system for both x86 and x86-64 architectures

# Architecture selection (x86 or x86_64)
ARCH ?= x86

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

# Source files
BOOT_MBR = $(BOOT_DIR)/mbr/mbr.asm
BOOT_STAGE2 = $(BOOT_DIR)/stage2/stage2.asm
KERNEL_ENTRY = $(ARCH_DIR)/entry.asm
KERNEL_C_SOURCES = $(wildcard $(KERNEL_DIR)/*.c) \
                   $(wildcard $(KERNEL_DIR)/mm/*.c) \
                   $(wildcard $(KERNEL_DIR)/task/*.c) \
                   $(wildcard $(KERNEL_DIR)/drivers/*.c)

# Object files
KERNEL_ENTRY_OBJ = $(BUILD_DIR)/entry.o
KERNEL_C_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(KERNEL_C_SOURCES)))

# Output files
MBR_BIN = $(BUILD_DIR)/mbr.bin
STAGE2_BIN = $(BUILD_DIR)/stage2.bin
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
OS_IMAGE = $(BUILD_DIR)/TocinOS.img

.PHONY: all clean directories

all: directories $(OS_IMAGE)

directories:
	@mkdir -p $(BUILD_DIR)

# Build MBR
$(MBR_BIN): $(BOOT_MBR)
	@echo "Building MBR..."
	$(AS) -f bin $(BOOT_MBR) -o $(MBR_BIN)

# Build Stage 2 bootloader
$(STAGE2_BIN): $(BOOT_STAGE2)
	@echo "Building Stage 2 bootloader..."
	$(AS) -f bin $(BOOT_STAGE2) -o $(STAGE2_BIN)

# Build kernel entry
$(KERNEL_ENTRY_OBJ): $(KERNEL_ENTRY)
	@echo "Building kernel entry..."
	$(AS) $(ASFLAGS) $(KERNEL_ENTRY) -o $(KERNEL_ENTRY_OBJ)

# Build kernel C files
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/mm/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/task/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/drivers/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Link kernel
$(KERNEL_BIN): $(KERNEL_ENTRY_OBJ) $(KERNEL_C_OBJS)
	@echo "Linking kernel..."
	$(LD) $(LDFLAGS) -o $(KERNEL_BIN) $(KERNEL_ENTRY_OBJ) $(KERNEL_C_OBJS)

# Create OS image
$(OS_IMAGE): $(MBR_BIN) $(STAGE2_BIN) $(KERNEL_BIN)
	@echo "Creating OS image..."
	@dd if=/dev/zero of=$(OS_IMAGE) bs=512 count=2880 2>/dev/null
	@dd if=$(MBR_BIN) of=$(OS_IMAGE) bs=512 count=1 conv=notrunc 2>/dev/null
	@dd if=$(STAGE2_BIN) of=$(OS_IMAGE) bs=512 seek=1 count=16 conv=notrunc 2>/dev/null
	@dd if=$(KERNEL_BIN) of=$(OS_IMAGE) bs=512 seek=17 conv=notrunc 2>/dev/null
	@echo "OS image created: $(OS_IMAGE)"

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
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)

# Help
help:
	@echo "TocinOS Build System"
	@echo "===================="
	@echo "Targets:"
	@echo "  all      - Build the OS image (default)"
	@echo "  run      - Build and run in QEMU (32-bit)"
	@echo "  run64    - Build and run in QEMU (64-bit)"
	@echo "  clean    - Remove build artifacts"
	@echo ""
	@echo "Variables:"
	@echo "  ARCH     - Target architecture (x86 or x86_64, default: x86)"
	@echo ""
	@echo "Examples:"
	@echo "  make                 # Build for x86"
	@echo "  make ARCH=x86_64     # Build for x86-64"
	@echo "  make run             # Build and run x86 version"
	@echo "  make run64           # Build and run x86-64 version"
