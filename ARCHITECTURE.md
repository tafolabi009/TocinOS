# TocinOS Architecture Documentation

## Overview

TocinOS is a custom operating system with support for both x86 and x86-64 architectures. This document provides technical details about the system architecture.

## Boot Sequence

### 1. MBR Stage (boot/mbr/mbr.asm)
- **Size**: 512 bytes (one sector)
- **Location**: Loaded at 0x7C00 by BIOS
- **Responsibilities**:
  - Initialize segments (DS, ES, SS)
  - Display boot message via BIOS interrupts
  - Load Stage 2 bootloader from disk (16 sectors starting at sector 2)
  - Jump to Stage 2 at 0x7E00

### 2. Stage 2 Bootloader (boot/stage2/stage2.asm)
- **Size**: 8KB (16 sectors)
- **Location**: Loaded at 0x7E00
- **Responsibilities**:
  - Enable A20 line for extended memory access
  - Detect CPU capabilities (CPUID)
  - Check for long mode (64-bit) support
  - Load kernel from disk (32 sectors starting at sector 18)
  - Set up GDT (Global Descriptor Table)
  - Enter protected mode (32-bit) or long mode (64-bit)
  - Jump to kernel entry point at 0x10000

### 3. Kernel Entry Point
- **32-bit**: kernel/arch/x86/entry.asm
- **64-bit**: kernel/arch/x86_64/entry.asm
- **Responsibilities**:
  - Set up kernel stack
  - Call kernel_main() function
  - Halt system if kernel returns

## Kernel Subsystems

### Memory Management

#### Physical Memory Manager (kernel/mm/pmm.c)
- **Algorithm**: Bitmap allocator
- **Page Size**: 4KB (4096 bytes)
- **Default Memory**: 128MB managed
- **Features**:
  - Page allocation/deallocation
  - Memory usage tracking
  - Protection of first 1MB (BIOS, bootloader, kernel)

#### Virtual Memory Manager (kernel/mm/vmm.c)
- **Paging Structure**: 
  - Page Directory (1024 entries)
  - Page Tables (1024 entries each)
- **Features**:
  - Identity mapping for kernel
  - Page mapping/unmapping
  - Page directory switching
  - Support for page flags (present, write, user)

### Task Scheduler (kernel/task/scheduler.c)

#### Design
- **Type**: Preemptive, priority-based
- **Algorithm**: Round-robin for same priority
- **Priority Levels**: 8 (0 = highest, 7 = lowest)
- **Max Tasks**: 64

#### Task States
- **READY**: Task is ready to run
- **RUNNING**: Task is currently executing
- **BLOCKED**: Task is waiting for resource
- **TERMINATED**: Task has finished execution

#### Operations
- `scheduler_init()`: Initialize scheduler
- `scheduler_start()`: Enable scheduling
- `task_create()`: Create new task with priority
- `task_block()`: Block current task
- `task_unblock()`: Unblock a task
- `task_yield()`: Voluntarily give up CPU
- `task_exit()`: Terminate current task

### Modular Driver Framework (kernel/drivers/mdf.c)

#### Driver Types
- **DRIVER_TYPE_BLOCK**: Block devices (hard drives, etc.)
- **DRIVER_TYPE_CHARACTER**: Character devices (VGA, keyboard, etc.)
- **DRIVER_TYPE_NETWORK**: Network interfaces
- **DRIVER_TYPE_USB**: USB devices
- **DRIVER_TYPE_PCI**: PCI devices
- **DRIVER_TYPE_GENERIC**: Generic drivers

#### Driver States
- **UNINITIALIZED**: Driver not initialized
- **INITIALIZED**: Driver initialized but not running
- **RUNNING**: Driver is active
- **SUSPENDED**: Driver is paused
- **ERROR**: Driver encountered an error

#### Driver Operations
```c
int (*init)(void);                              // Initialize driver
int (*probe)(void);                             // Detect hardware
int (*remove)(void);                            // Remove driver
int (*open)(void);                              // Open device
int (*close)(void);                             // Close device
int (*read)(void *buffer, unsigned int size);  // Read from device
int (*write)(const void *buffer, unsigned int size); // Write to device
int (*ioctl)(unsigned int cmd, void *arg);     // Device control
```

#### MDF API
- `mdf_init()`: Initialize framework
- `mdf_register_driver()`: Register a new driver
- `mdf_unregister_driver()`: Remove a driver
- `mdf_init_driver()`: Initialize a specific driver
- `mdf_start_driver()`: Start a driver
- `mdf_stop_driver()`: Stop a driver
- `mdf_driver_open/close/read/write/ioctl()`: Device operations

## Build System

### Makefile Targets
- `make`: Build x86 version
- `make ARCH=x86_64`: Build x86-64 version
- `make run`: Build and run x86 in QEMU
- `make run64`: Build and run x86-64 in QEMU
- `make clean`: Remove build artifacts

### Compilation Flags
- `-ffreestanding`: No standard library
- `-fno-pie`: No position-independent executable
- `-nostdlib`: No standard library linking
- `-nostdinc`: No standard include files
- `-fno-builtin`: No built-in functions
- `-fno-stack-protector`: No stack protection
- `-mno-red-zone` (x86-64 only): No red zone usage

### Linker Scripts
- `linker_x86.ld`: Links 32-bit kernel
- `linker_x86_64.ld`: Links 64-bit kernel
- Both place kernel at 0x10000 (64KB)

## Memory Map

```
Physical Address    Size        Description
----------------    ----        -----------
0x00000000          1KB         Real Mode IVT
0x00000400          256B        BIOS Data Area
0x00000500          ~30KB       Free conventional memory
0x00007C00          512B        MBR (Stage 1 bootloader)
0x00007E00          8KB         Stage 2 bootloader
0x00010000          variable    Kernel code and data
0x00090000          64KB        Kernel stack
0x000A0000          128KB       VGA memory
0x000C0000          256KB       BIOS ROM area
0x00100000+         variable    Extended memory (managed by PMM)
```

## Example Driver Implementation

See `kernel/drivers/vga_driver.c` for a complete example of an MDF driver.

## Future Enhancements

1. **Interrupt Handling**: IDT setup, ISR handlers
2. **Timer**: PIT programming for preemptive multitasking
3. **Keyboard Driver**: PS/2 keyboard support
4. **System Calls**: User mode to kernel mode interface
5. **File System**: FAT32 or custom FS
6. **User Mode**: Ring 3 support with privilege separation
7. **Shell**: Command-line interface

## References

- Intel® 64 and IA-32 Architectures Software Developer Manuals
- AMD64 Architecture Programmer's Manual
- OSDev Wiki: https://wiki.osdev.org/
- NASM Documentation: https://www.nasm.us/doc/
