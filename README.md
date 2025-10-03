# TocinOS

**TocinOS** is a custom x86/x86-64 operating system built from scratch, designed with modern OS principles and educational purposes in mind. Drawing inspiration from Windows, macOS, and Linux, TocinOS combines high performance, flexibility, and strong hardware interaction with a custom bootloader and kernel.

## 🎯 Project Vision

TocinOS aims to be a **modern, secure, developer-first OS** that provides:
- Full control over hardware and system behavior
- High performance and optimization
- Educational value for OS development learning
- Modular and extensible architecture
- Support for modern CPU features and hardware

## ✨ Key Features

### 🚀 Multi-Stage Custom Bootloader

#### Stage 1: MBR Bootloader
- Fits within 446 bytes (MBR standard)
- Loads Stage 2 from disk
- Basic error handling and status messages
- BIOS interrupt-based disk I/O

#### Stage 2: Advanced Bootloader
- **Interactive Boot Menu** with 5-second timeout
- ASCII logo display
- Multiple boot options:
  - Normal boot (default)
  - Safe Mode
  - Recovery Mode
- **A20 Line Enable**: Access to memory above 1MB
- **CPU Detection**: CPUID-based capability detection
- **Dual Mode Support**:
  - 32-bit Protected Mode for x86
  - 64-bit Long Mode for x86-64
- **GDT Setup**: Global Descriptor Table configuration
- **Automatic Mode Selection**: Based on CPU capabilities
- Kernel loading from disk into memory

### 💾 Advanced Memory Management

#### Physical Memory Manager (PMM)
- Bitmap-based page allocator
- 4KB page size (standard x86)
- Manages up to 128MB RAM (configurable)
- Memory usage tracking and statistics
- First 1MB protection for BIOS/bootloader

#### Virtual Memory Manager (VMM)
- Full paging support with page tables and directories
- Identity mapping for kernel space
- Dynamic page mapping/unmapping
- Page-level memory protection (present, write, user flags)
- Context switching support
- MMU (Memory Management Unit) setup

### ⚡ Preemptive Multitasking

**Priority-Based Task Scheduler:**
- 8 priority levels (0 = highest, 7 = lowest)
- Round-robin scheduling for equal priority tasks
- Task states: READY, RUNNING, BLOCKED, TERMINATED
- Supports up to 64 concurrent tasks
- Per-task 8KB stacks
- Task control operations:
  - Create tasks with custom entry points
  - Block/unblock tasks
  - Task termination
  - Voluntary CPU yielding
- Context switching framework

### 💻 Comprehensive CPU Feature Detection

**CPUID-Based Detection:**
- CPU vendor identification (Intel, AMD, etc.)
- Family, model, and stepping information
- **Standard Features**: FPU, VME, PSE, PAE, APIC, MMX, SSE, SSE2, SSE3, SSE4.1, SSE4.2, AVX, HTT
- **Extended Features**: SYSCALL, NX, Long Mode, 1GB pages
- **Security Features**: AES-NI, RDRAND
- Runtime feature availability checking
- Boot-time feature display

### 🔌 Modular Driver Framework (MDF)

**Extensible Driver Architecture:**
- Multiple driver types: Block, Character, Network, USB, PCI, Generic
- Driver lifecycle management
- Standard operations: open, close, read, write, ioctl
- Driver states: Uninitialized, Initialized, Running, Suspended, Error
- Up to 32 concurrent drivers (configurable)
- Example VGA text mode driver included

### 🔗 Boot Information Structure

**Standardized Bootloader-to-Kernel Interface:**
- Memory map from BIOS
- Boot flags (BIOS/UEFI, 32/64-bit mode)
- CPU information and features
- Framebuffer information for graphics
- Kernel load location and size
- Fixed memory location (0x8000) for easy access
- Magic number validation (0xB007DA7A)

### 🏗️ Dual Architecture Support
- **x86 (32-bit)**: Full support for legacy systems
- **x86-64 (64-bit)**: Modern long mode support
- Configurable build system
- Architecture-specific code organization
- Dedicated linker scripts

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

Required tools:
- **NASM**: Netwide Assembler for bootloader and kernel assembly
- **GCC**: GNU C Compiler with cross-compilation support
- **GNU Binutils**: LD linker and objcopy
- **QEMU** (optional): For testing the OS in emulator

Install on Ubuntu/Debian:
```bash
sudo apt-get install nasm gcc binutils qemu-system-x86
```

Install on macOS:
```bash
brew install nasm gcc qemu
```

### Build Commands

Build for 32-bit x86:
```bash
make
# or explicitly
make ARCH=x86
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

### Using QEMU (Recommended)

Run 32-bit version:
```bash
make run
```

Run 64-bit version:
```bash
make run64
```

You'll see:
1. BIOS/UEFI initialization
2. MBR loading message
3. Stage 2 bootloader
4. **Interactive boot menu** with TocinOS ASCII logo
5. Kernel initialization with CPU detection
6. Memory management initialization
7. Task scheduler initialization
8. Driver framework initialization

### Using VirtualBox or VMware

1. Build the OS image:
   ```bash
   make
   ```

2. The image will be created at `build/TocinOS.img` (1.44MB floppy format)

3. Create a new virtual machine:
   - Type: Other/Unknown
   - Version: Other/Unknown
   - Memory: 128MB minimum
   - Storage: Attach `build/TocinOS.img` as a floppy disk or raw disk image

4. Boot the virtual machine

### Boot Menu Options

When TocinOS boots, you'll see an interactive menu:

```
  _____         _       ___  ____  
 |_   _|__   __(_) _ _ / _ \/ ___| 
   | | / _ \ / _| || | | | |\___ \ 
   |_| \___/ \__|_||_| |_| ||____/ 

=== TocinOS Boot Menu ===

  [1] Boot TocinOS (Default)
  [2] Boot in Safe Mode
  [3] Recovery Mode

Press a key within 5 seconds or default boot will start...
```

- **Option 1 (Default)**: Normal boot with all features
- **Option 2 (Safe Mode)**: Boot with minimal drivers (planned)
- **Option 3 (Recovery Mode)**: Boot into recovery environment (planned)
- **Timeout**: Automatically boots default option after 5 seconds

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

### ✅ Completed Features
- [x] Multi-stage bootloader (MBR + Stage 2)
- [x] Interactive boot menu with timeout
- [x] Dual architecture support (x86 and x86-64)
- [x] Physical memory management (PMM)
- [x] Virtual memory with paging (VMM)
- [x] Preemptive multitasking scheduler
- [x] Modular driver framework (MDF)
- [x] CPU feature detection (CPUID)
- [x] Boot information structure
- [x] A20 line enable
- [x] GDT setup
- [x] Protected mode and long mode support
- [x] Interrupt Descriptor Table (IDT)
- [x] Interrupt Service Routines (ISR)
- [x] Programmable Interval Timer (PIT)
- [x] PS/2 Keyboard driver with interrupts

### 🚧 In Progress
- [x] Enhanced VGA driver with graphics mode
- [x] Interrupt handling (IDT setup)
- [x] Timer and PIT support
- [x] Keyboard driver (PS/2 with interrupts)

### 🎯 Planned Features
- [ ] **Filesystem Support**
  - FAT12/FAT16/FAT32 implementation
  - File operations (read, write, directory listing)
  - Kernel module loading from filesystem
- [ ] **UEFI Compatibility**
  - UEFI boot support
  - Graphics Output Protocol (GOP)
  - UEFI runtime services
- [ ] **Security Features**
  - KASLR (Kernel Address Space Layout Randomization)
  - NX bit support
  - Boot password/encryption
- [ ] **Advanced Boot Features**
  - Initrd (Initial RAM Disk) support
  - Network boot (PXE)
  - Multi-boot support
- [ ] **Device Drivers**
  - Keyboard driver (PS/2 with interrupts)
  - Enhanced VGA/VESA graphics
  - Serial port communication
  - Storage drivers (IDE/AHCI)
  - Network card drivers
- [ ] **System Services**
  - System call interface
  - User mode support (Ring 3)
  - Process isolation
  - Shell/CLI
  - IPC mechanisms

### 🌟 Long-term Vision

TocinOS aims to evolve into a full-featured OS with:
- **Hybrid Kernel**: Monolithic base with microkernel-style modules
- **Modern Filesystem**: Btrfs-inspired with CoW, snapshots, compression
- **Desktop Environment**: KDE Plasma-based with custom UI
- **Package Management**: Unified package manager (native, Flatpak, AppImage)
- **Security Model**: AppArmor-based with SIP-like kernel protection
- **AI Integration**: LLM-powered system assistant ("Nyra")
- **Cloud Sync**: Git-based dotfile and config synchronization
- **Developer Tools**: Built-in toolchain, containerized builds, LSP support

See [FEATURES.md](FEATURES.md) for detailed feature documentation.

## Contributing

TocinOS is an educational OS project and contributions are welcome! 

### Areas for Contribution
1. **Feature Implementation**: Pick a feature from the roadmap
2. **Driver Development**: Write new device drivers
3. **Documentation**: Improve guides and API documentation
4. **Testing**: Test on real hardware and report issues
5. **Optimization**: Performance improvements and code cleanup

### Contribution Guidelines
1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Test thoroughly (QEMU and real hardware if possible)
5. Commit your changes (`git commit -m 'Add amazing feature'`)
6. Push to the branch (`git push origin feature/amazing-feature`)
7. Open a Pull Request

See [CONTRIBUTING.md](CONTRIBUTING.md) for detailed guidelines.

## Educational Value

TocinOS is designed as a learning platform for understanding:
- **Low-level Programming**: x86/x86-64 assembly and C
- **Bootloader Development**: BIOS interrupts, disk I/O, mode switching
- **OS Kernel Design**: Memory management, process scheduling, drivers
- **Hardware Interaction**: CPU features, memory mapping, device I/O
- **System Architecture**: Modular design, abstraction layers

### Learning Resources
- [OSDev Wiki](https://wiki.osdev.org/) - Comprehensive OS development resource
- [Intel Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [AMD Architecture Programmer's Manual](https://www.amd.com/en/support/tech-docs)
- [NASM Documentation](https://www.nasm.us/doc/)

## Project Structure

```
TocinOS/
├── boot/                   # Bootloader code
│   ├── mbr/               # Stage 1 MBR bootloader
│   │   └── mbr.asm
│   └── stage2/            # Stage 2 bootloader with boot menu
│       └── stage2.asm
├── kernel/                # Kernel source code
│   ├── arch/              # Architecture-specific code
│   │   ├── x86/           # 32-bit x86
│   │   │   └── entry.asm
│   │   └── x86_64/        # 64-bit x86-64
│   │       └── entry.asm
│   ├── mm/                # Memory management
│   │   ├── pmm.c          # Physical memory manager
│   │   └── vmm.c          # Virtual memory manager
│   ├── task/              # Task management
│   │   └── scheduler.c    # Preemptive scheduler
│   ├── drivers/           # Device drivers
│   │   ├── mdf.c          # Modular Driver Framework
│   │   └── vga_driver.c   # VGA text mode driver
│   ├── cpu_info.c         # CPU detection and features
│   └── kernel.c           # Kernel main
├── include/               # Header files
│   ├── boot/              # Boot-related headers
│   │   └── boot_info.h    # Boot information structure
│   ├── kernel/            # Kernel headers
│   │   ├── kernel.h
│   │   ├── memory.h
│   │   ├── task.h
│   │   └── cpu_info.h
│   └── drivers/           # Driver headers
│       └── mdf.h
├── linker_x86.ld         # x86 linker script
├── linker_x86_64.ld      # x86-64 linker script
├── Makefile              # Build system
├── README.md             # This file
├── FEATURES.md           # Detailed feature documentation
├── ARCHITECTURE.md       # Technical architecture details
└── CONTRIBUTING.md       # Contribution guidelines
```

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
