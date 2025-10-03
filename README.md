# TocinOS

TocinOS is a custom x86/x86-64 operating system built from scratch with a focus on educational purposes and modern OS design principles.

## Key Features

### 🏗️ Dual Architecture Support
- **32-bit (x86)**: Full support for legacy x86 systems
- **64-bit (x86-64)**: Modern 64-bit long mode support
- Configurable build system for both architectures

### 🚀 Multi-Stage Bootloader
TocinOS implements a sophisticated boot process:
1. **MBR (Master Boot Record)**: Stage 1 bootloader (512 bytes)
   - Initializes basic system state
   - Loads Stage 2 from disk
   - Displays boot messages
2. **Stage 2 Bootloader**: Advanced bootloader (8KB)
   - Enables A20 line for extended memory access
   - Detects CPU capabilities (32-bit vs 64-bit)
   - Sets up protected mode or long mode
   - Loads and transfers control to kernel

### 💾 Memory Management
- **Physical Memory Manager (PMM)**
  - Bitmap-based page allocator
  - 4KB page size support
  - Memory usage tracking
- **Virtual Memory Manager (VMM)**
  - Paging support with page tables and directories
  - Virtual to physical address mapping
  - Identity mapping for kernel space
  - Page-level memory protection

### ⚡ Preemptive Multitasking
- **Priority-Based Scheduler**
  - 8 priority levels (0 = highest, 7 = lowest)
  - Round-robin scheduling for same-priority tasks
  - Task states: READY, RUNNING, BLOCKED, TERMINATED
  - Context switching support
  - Task creation and destruction
  - CPU yielding and blocking mechanisms

### 🔌 MDF (Modular Driver Framework)
Extensible driver architecture with:
- **Driver Types**: Block, Character, Network, USB, PCI, Generic
- **Driver Lifecycle Management**
  - Registration and unregistration
  - Initialization and probing
  - Start/stop operations
- **Standard Driver Operations**
  - Open/Close
  - Read/Write
  - IOCTL (I/O Control)
- **Driver States**: Uninitialized, Initialized, Running, Suspended, Error

## Directory Structure

```
TocinOS/
├── boot/
│   ├── mbr/                # Master Boot Record (Stage 1)
│   │   └── mbr.asm
│   └── stage2/             # Stage 2 bootloader
│       └── stage2.asm
├── kernel/
│   ├── arch/               # Architecture-specific code
│   │   ├── x86/            # 32-bit x86
│   │   │   └── entry.asm
│   │   └── x86_64/         # 64-bit x86-64
│   │       └── entry.asm
│   ├── mm/                 # Memory management
│   │   ├── pmm.c           # Physical memory manager
│   │   └── vmm.c           # Virtual memory manager
│   ├── task/               # Task management
│   │   └── scheduler.c     # Preemptive scheduler
│   ├── drivers/            # Driver framework
│   │   └── mdf.c           # Modular Driver Framework
│   └── kernel.c            # Kernel main
├── include/
│   ├── kernel/             # Kernel headers
│   │   ├── kernel.h
│   │   ├── memory.h
│   │   └── task.h
│   └── drivers/            # Driver headers
│       └── mdf.h
├── linker_x86.ld          # x86 linker script
├── linker_x86_64.ld       # x86-64 linker script
├── Makefile               # Build system
└── README.md
```

## Building TocinOS

### Prerequisites

- **NASM**: Netwide Assembler for bootloader and kernel assembly
- **GCC**: GNU C Compiler with cross-compilation support
- **LD**: GNU Linker
- **QEMU** (optional): For testing the OS

Install on Ubuntu/Debian:
```bash
sudo apt-get install nasm gcc binutils qemu-system-x86
```

### Build Commands

Build for 32-bit x86:
```bash
make
```

Build for 64-bit x86-64:
```bash
make ARCH=x86_64
```

Clean build artifacts:
```bash
make clean
```

## Running TocinOS

### Using QEMU

Run 32-bit version:
```bash
make run
```

Run 64-bit version:
```bash
make run64
```

### Using VirtualBox or VMware

1. Build the OS image:
   ```bash
   make
   ```

2. The image will be created at `build/TocinOS.img`

3. Create a new virtual machine and attach the image as a floppy disk or raw disk image

4. Boot the virtual machine

## Technical Details

### Boot Process

1. **BIOS Stage**: System BIOS loads MBR from disk sector 0
2. **MBR Stage**: MBR bootloader loads Stage 2 from sectors 1-16
3. **Stage 2**: Detects CPU, enables A20, sets up protected/long mode
4. **Kernel**: Initializes subsystems and starts multitasking

### Memory Layout

```
0x00000000 - 0x000003FF : Real Mode IVT (Interrupt Vector Table)
0x00000400 - 0x000004FF : BIOS Data Area
0x00000500 - 0x00007BFF : Free conventional memory
0x00007C00 - 0x00007DFF : MBR (Stage 1 bootloader)
0x00007E00 - 0x00009FFF : Stage 2 bootloader
0x00010000 - 0x0008FFFF : Kernel code and data
0x00090000 - 0x0009FFFF : Kernel stack
0x000A0000 - 0x000BFFFF : Video memory
0x000C0000 - 0x000FFFFF : BIOS ROM
0x00100000+           : Extended memory (managed by PMM)
```

### Scheduler Algorithm

The scheduler implements a priority-based preemptive multitasking system:
- Tasks are organized in priority queues (8 levels)
- Higher priority tasks always run first
- Tasks of equal priority are scheduled round-robin
- Quantum-based time slicing (future enhancement)
- Support for task blocking and yielding

### Driver Framework

MDF provides a unified interface for device drivers:
- Drivers register with the framework
- Framework manages driver lifecycle
- Standard operations for device I/O
- Support for multiple driver types
- Extensible for new device classes

## Development Roadmap

- [x] Multi-stage bootloader
- [x] Dual architecture support
- [x] Physical memory management
- [x] Virtual memory with paging
- [x] Preemptive multitasking
- [x] Modular driver framework
- [ ] Interrupt handling (IDT/GDT)
- [ ] Timer and PIT support
- [ ] Keyboard driver
- [ ] VGA driver improvements
- [ ] File system support
- [ ] System calls interface
- [ ] User mode support
- [ ] Shell/CLI

## Contributing

This is an educational OS project. Contributions are welcome! Please follow these guidelines:
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

This project is open source and available for educational purposes.

## References

- [OSDev Wiki](https://wiki.osdev.org/)
- Intel Software Developer Manuals
- AMD Architecture Programmer's Manual

## Author

TocinOS - A journey into operating system development

---

*"The best way to learn how something works is to build it yourself."*
