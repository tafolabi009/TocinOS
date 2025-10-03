# TocinOS Features Documentation

This document outlines the features implemented in TocinOS and their alignment with modern OS requirements.

## ✅ Implemented Features

### 🚀 Custom Bootloader (Multi-Stage)

#### Stage 1: MBR/VBR Loader
- ✅ **MBR Loading**: Loads from the disk's MBR, fits within first 446 bytes
- ✅ **Partition Table Support**: Properly reserves space for partition table and signature (0xAA55)
- ✅ **Stage 2 Loading**: Loads Stage 2 bootloader from sectors 1-16
- ✅ **BIOS Interrupts**: Uses INT 0x13 for disk I/O
- ✅ **Error Handling**: Basic error detection and reporting
- **Location**: `boot/mbr/mbr.asm`

#### Stage 2: Advanced Bootloader
- ✅ **C/Assembly Implementation**: Written in Assembly for maximum control
- ✅ **A20 Line Enable**: Enables access to memory above 1MB using fast A20 gate
- ✅ **GDT Setup**: Global Descriptor Table configured for protected mode
- ✅ **CPU Detection**: CPUID instruction used to detect CPU capabilities
- ✅ **Dual Mode Support**:
  - **32-bit Protected Mode**: Full support for x86 systems
  - **64-bit Long Mode**: Support for x86-64 systems with proper detection
- ✅ **Mode Switching**: Automatic detection and switching based on CPU capabilities
- ✅ **Kernel Loading**: Loads kernel binary from disk (sectors 17+) into memory at 0x10000
- ✅ **Protected Mode Entry**: Sets up and enters protected mode with proper segment registers
- ✅ **Paging Preparation**: Sets up basic paging structures for 64-bit mode
- ✅ **Boot Messages**: Status messages displayed during boot process
- **Location**: `boot/stage2/stage2.asm`

### 💻 Kernel Features

#### Memory Management System

##### Physical Memory Manager (PMM)
- ✅ **Bitmap Allocator**: Efficient page-level memory allocation
- ✅ **4KB Page Size**: Standard x86 page size support
- ✅ **128MB Default**: Manages up to 128MB of RAM (configurable)
- ✅ **First 1MB Protection**: Reserves low memory for BIOS and bootloader
- ✅ **Page Tracking**: Tracks used and free pages
- ✅ **Memory Statistics**: Provides total, used, and free page counts
- **Location**: `kernel/mm/pmm.c`

##### Virtual Memory Manager (VMM)
- ✅ **Paging Support**: Full paging implementation with page tables and directories
- ✅ **4KB Pages**: Standard page size
- ✅ **Identity Mapping**: Identity maps first 4MB for kernel
- ✅ **Page Mapping/Unmapping**: Dynamic page table management
- ✅ **Page Flags**: Support for present, write, and user flags
- ✅ **Directory Switching**: Context switching support
- ✅ **MMU Setup**: Memory Management Unit properly configured
- **Location**: `kernel/mm/vmm.c`

#### Task Scheduler (Preemptive Multitasking)
- ✅ **Priority-Based Scheduling**: 8 priority levels (0 = highest, 7 = lowest)
- ✅ **Round-Robin**: Tasks of equal priority scheduled round-robin
- ✅ **Task States**: READY, RUNNING, BLOCKED, TERMINATED
- ✅ **64 Task Support**: Up to 64 concurrent tasks
- ✅ **Per-Task Stacks**: 8KB stack per task
- ✅ **Task Control**:
  - Task creation with custom entry point
  - Task blocking and unblocking
  - Task termination
  - Voluntary yielding
- ✅ **Context Switching**: Full register state preservation (framework in place)
- **Location**: `kernel/task/scheduler.c`

#### CPU Feature Detection
- ✅ **CPUID Support**: Comprehensive CPU feature detection using CPUID instruction
- ✅ **Vendor Identification**: Detects CPU manufacturer (Intel, AMD, etc.)
- ✅ **Feature Flags**: Detects standard and extended CPU features:
  - **Standard Features (EDX)**: FPU, VME, PSE, PAE, APIC, MMX, SSE, SSE2, HTT, etc.
  - **Standard Features (ECX)**: SSE3, SSSE3, SSE4.1, SSE4.2, AVX, AES, RDRAND, etc.
  - **Extended Features**: SYSCALL, NX, Long Mode, 1GB Pages, etc.
- ✅ **CPU Information**: Family, Model, Stepping, Cache line size
- ✅ **Feature Testing**: Runtime feature availability checking
- ✅ **Boot Time Display**: CPU features printed during kernel initialization
- **Location**: `kernel/cpu_info.c`, `include/kernel/cpu_info.h`

#### Interrupt Handling System
- ✅ **IDT Setup**: 256-entry Interrupt Descriptor Table
- ✅ **Exception Handlers**: CPU exception handlers for all x86 exceptions (0-31)
- ✅ **IRQ Handlers**: Hardware interrupt handlers (32-47)
- ✅ **PIC Remapping**: Programmable Interrupt Controller configured
- ✅ **Custom Handlers**: Support for registering custom interrupt handlers
- ✅ **Exception Messages**: Descriptive error messages for exceptions
- **Location**: `kernel/idt.c`, `kernel/isr.c`, `kernel/arch/x86/isr_asm.asm`

#### Timer Support (PIT)
- ✅ **Programmable Interval Timer**: Full PIT support
- ✅ **Configurable Frequency**: Default 100 Hz, adjustable
- ✅ **Tick Counter**: System uptime tracking
- ✅ **Timer Waits**: Delay functionality
- **Location**: `kernel/timer.c`, `include/kernel/timer.h`

#### Keyboard Driver
- ✅ **PS/2 Support**: Full PS/2 keyboard driver with interrupts
- ✅ **Scancode Translation**: US QWERTY scancode to ASCII
- ✅ **Shift Support**: Uppercase letters and special characters
- ✅ **Input Buffer**: 256-byte circular buffer
- ✅ **Blocking/Non-blocking**: Multiple input methods
- **Location**: `kernel/keyboard.c`, `include/kernel/keyboard.h`

#### Serial Port Driver
- ✅ **COM Port Support**: COM1, COM2, COM3, COM4
- ✅ **Baud Rate**: 38400 baud (configurable)
- ✅ **Configuration**: 8N1 (8 bits, no parity, 1 stop bit)
- ✅ **FIFO Buffering**: Hardware buffering enabled
- ✅ **Loopback Test**: Hardware verification on initialization
- ✅ **I/O Operations**: Blocking read/write operations
- **Location**: `kernel/serial.c`, `include/kernel/serial.h`

#### System Call Interface
- ✅ **INT 0x80**: Standard system call interrupt
- ✅ **System Call Table**: 32-slot handler table
- ✅ **Implemented Calls**:
  - `sys_exit` - Process termination
  - `sys_write` - Write to file descriptor
  - `sys_read` - Read from file descriptor
  - `sys_gettime` - Get system uptime in ticks
  - `sys_sleep` - Sleep for specified ticks
- ✅ **DPL3 Access**: User mode accessible
- **Location**: `kernel/syscall.c`, `include/kernel/syscall.h`

#### Shell/CLI
- ✅ **Interactive Prompt**: Command-line interface
- ✅ **Built-in Commands**:
  - `help` - Show available commands
  - `clear` - Clear screen
  - `cpuinfo` - Display CPU information
  - `meminfo` - Display memory status
  - `uptime` - Show system uptime
  - `history` - View command history
- ✅ **Command History**: Stores 10 previous commands
- ✅ **Input Editing**: Backspace support
- ✅ **Real-time Echo**: Character-by-character display
- **Location**: `kernel/shell.c`, `include/kernel/shell.h`

#### Boot Information Structure
- ✅ **Standardized Interface**: Defined structure for bootloader-to-kernel communication
- ✅ **Memory Map**: Support for BIOS memory map entries
- ✅ **Boot Flags**: BIOS/UEFI detection, 32/64-bit mode indication
- ✅ **CPU Information**: Vendor, features passed from bootloader
- ✅ **Framebuffer Info**: Graphics mode information support
- ✅ **Kernel Load Info**: Kernel location and size
- ✅ **Fixed Location**: Located at 0x8000 for easy access
- ✅ **Magic Number**: 0xB007DA7A for validation
- **Location**: `include/boot/boot_info.h`

### 🔌 Modular Driver Framework (MDF)

#### Driver Architecture
- ✅ **Multiple Driver Types**:
  - Block devices (hard drives, SSDs)
  - Character devices (keyboard, serial, VGA)
  - Network interfaces
  - USB devices
  - PCI devices
  - Generic drivers
- ✅ **Driver States**: Uninitialized, Initialized, Running, Suspended, Error
- ✅ **Lifecycle Management**: Registration, initialization, start/stop, removal
- ✅ **Standard Operations**: Open, close, read, write, ioctl
- ✅ **Driver Discovery**: List and query registered drivers
- ✅ **32 Driver Limit**: Supports up to 32 concurrent drivers (configurable)
- **Location**: `kernel/drivers/mdf.c`, `include/drivers/mdf.h`

#### Example VGA Driver
- ✅ **Character Device**: Implements character device interface
- ✅ **VGA Text Mode**: 80x25 text mode support
- ✅ **Basic Operations**: Clear screen, write text, cursor control
- ✅ **MDF Integration**: Demonstrates proper MDF usage
- **Location**: `kernel/drivers/vga_driver.c`

### 🏗️ Architecture Support

#### Dual Architecture
- ✅ **x86 (32-bit)**: Full support for legacy x86 systems
- ✅ **x86-64 (64-bit)**: Modern 64-bit long mode support
- ✅ **Configurable Build**: Switch between architectures via Makefile
- ✅ **Architecture-Specific Code**: Separate entry points and configurations
- ✅ **Linker Scripts**: Dedicated linker scripts for each architecture

### 🛠️ Development Infrastructure

#### Build System
- ✅ **Makefile**: Comprehensive build automation
- ✅ **NASM**: Assembly code compilation
- ✅ **GCC**: C code compilation with freestanding environment
- ✅ **Proper Flags**: No standard library, no built-ins, no stack protector
- ✅ **Binary Conversion**: ELF to flat binary conversion for bootloader
- ✅ **Image Creation**: Automatic OS image generation (floppy format)
- ✅ **Clean Targets**: Build artifact cleanup
- ✅ **QEMU Integration**: Run targets for testing in emulator
- **Location**: `Makefile`

## 🔄 Features In Progress

### Boot Menu UI
- 📋 **Planned**: Text-based boot menu
- 📋 **Features**:
  - Multiple kernel selection
  - Recovery options
  - Timeout support
  - Visual feedback

### Boot Splash/Logo
- 📋 **Planned**: Custom boot logo display
- 📋 **Options**:
  - ASCII art logo
  - Bitmap graphics
  - Animation support

### Enhanced Hardware Detection
- 📋 **Basic Detection**: Keyboard, display initialization
- 📋 **Expansion**: More hardware probing

## 🎯 Planned Features (Roadmap)

### Filesystem Support
- 🎯 **FAT12**: Basic floppy disk support
- 🎯 **FAT16/FAT32**: Hard drive and USB support
- 🎯 **File Operations**: Read, write, directory listing
- 🎯 **Kernel Module Loading**: Load additional modules from filesystem

### UEFI Support
- 🎯 **UEFI Boot**: Support for modern UEFI firmware
- 🎯 **GOP Support**: Graphics Output Protocol for framebuffer
- 🎯 **UEFI Services**: Use UEFI runtime services
- 🎯 **Secure Boot**: Support for secure boot (optional)

### Advanced Security Features
- 🎯 **KASLR**: Kernel Address Space Layout Randomization
- 🎯 **NX Support**: No-Execute bit enforcement
- 🎯 **Boot Password**: Encrypted boot process
- 🎯 **Secure Enclave**: Protected memory regions

### Initial Ramdisk (Initrd)
- 🎯 **Ramdisk Loading**: Load initrd during boot
- 🎯 **Module Support**: Early driver and module loading
- 🎯 **Root Filesystem**: Use initrd as temporary root

### Network Boot (PXE)
- 🎯 **PXE Support**: Network-based kernel loading
- 🎯 **TFTP**: Trivial File Transfer Protocol support
- 🎯 **Cluster Support**: Deploy to multiple machines

### Interrupt Handling
- ✅ **IDT Setup**: Interrupt Descriptor Table (Complete)
- ✅ **ISR Handlers**: Interrupt Service Routines (Complete)
- ✅ **Exception Handling**: CPU exception handlers (Complete)
- ✅ **IRQ Management**: Hardware interrupt handling (Complete)

### Timer and PIT
- ✅ **PIT Programming**: Programmable Interval Timer (Complete)
- ✅ **Preemptive Scheduling**: Time-slice based task switching (Ready)
- ✅ **System Uptime**: Track system time (Complete)

### Device Drivers
- ✅ **Keyboard Driver**: PS/2 keyboard support with interrupt handling (Complete)
- 🎯 **Enhanced VGA**: Graphics mode support
- ✅ **Serial Port**: COM port communication (Complete)
- 🎯 **Storage**: IDE/AHCI disk drivers
- 🎯 **Network**: Basic network card support

### System Calls
- ✅ **Syscall Interface**: User mode to kernel mode transition (Complete)
- ✅ **System Call Table**: Organized syscall dispatch (Complete)
- 🎯 **POSIX-like API**: Familiar interface for applications

### User Mode
- 🎯 **Ring 3 Support**: Privilege separation
- 🎯 **User Processes**: Run applications in user mode
- 🎯 **Memory Protection**: Separate user/kernel address spaces

### Shell/CLI
- ✅ **Command Interpreter**: Basic shell (Complete)
- ✅ **Built-in Commands**: System utilities (Complete)
- 🎯 **Script Support**: Simple scripting

## 📊 Feature Comparison

| Feature | TocinOS Status | Notes |
|---------|---------------|-------|
| MBR Bootloader | ✅ Complete | Stage 1 loader |
| Stage 2 Bootloader | ✅ Complete | With A20, GDT, mode detection |
| Protected Mode | ✅ Complete | 32-bit support |
| Long Mode | ✅ Complete | 64-bit support |
| CPU Detection | ✅ Complete | CPUID-based with full feature detection |
| A20 Enable | ✅ Complete | Fast A20 gate method |
| GDT | ✅ Complete | Properly configured |
| Paging | ✅ Complete | Page tables and directories |
| Physical Memory Manager | ✅ Complete | Bitmap allocator |
| Virtual Memory Manager | ✅ Complete | Page mapping |
| Task Scheduler | ✅ Complete | Priority-based, preemptive |
| Driver Framework | ✅ Complete | Modular architecture |
| Boot Info Structure | ✅ Complete | Standardized interface |
| Boot Menu | 📋 Planned | Coming soon |
| Boot Splash | 📋 Planned | Coming soon |
| Filesystem (FAT) | 🎯 Roadmap | Future release |
| UEFI Support | 🎯 Roadmap | Future release |
| KASLR | 🎯 Roadmap | Security feature |
| Initrd | 🎯 Roadmap | Module loading |
| Network Boot (PXE) | 🎯 Roadmap | Enterprise feature |
| Interrupt Handling | 🎯 Roadmap | IDT/ISR |
| Timer Support | 🎯 Roadmap | PIT programming |
| Keyboard Driver | 🎯 Roadmap | PS/2 support |
| System Calls | 🎯 Roadmap | User/kernel interface |
| User Mode | 🎯 Roadmap | Ring 3 |
| Shell | 🎯 Roadmap | CLI |

## 🎓 Educational Value

TocinOS is designed as an educational platform to understand:
- Low-level x86/x86-64 programming
- Bootloader development
- Operating system kernel design
- Memory management techniques
- Task scheduling algorithms
- Driver architecture patterns
- Hardware interaction

## 🔗 References

- [OSDev Wiki](https://wiki.osdev.org/) - Comprehensive OS development resource
- [Intel® 64 and IA-32 Architectures Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [AMD64 Architecture Programmer's Manual](https://www.amd.com/en/support/tech-docs)
- [NASM Documentation](https://www.nasm.us/doc/)

## 📝 Contributing

Contributions are welcome! Areas of focus:
1. Implementing planned features
2. Improving existing implementations
3. Adding more drivers
4. Enhancing documentation
5. Bug fixes and optimizations

See CONTRIBUTING.md for guidelines.

## 📄 License

This project is open source and available for educational purposes.

---

**Legend:**
- ✅ Complete and tested
- 📋 In progress or partially implemented
- 🎯 Planned for future releases
