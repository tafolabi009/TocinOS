# TocinOS v1.0

A minimal, production-ready x86/x86-64 operating system kernel for developers and researchers.

## Overview

TocinOS is a bare-metal operating system designed for learning, experimentation, and embedded/research use cases. It provides a complete boot-to-shell environment with preemptive multitasking, memory protection, and a modular architecture.

**Target Audience:** OS developers, systems researchers, embedded systems engineers, and students learning low-level programming.

## Supported Hardware

| Component | Support Level |
|-----------|---------------|
| **Architecture** | x86 (32-bit), x86-64 (64-bit) |
| **Boot Firmware** | BIOS (Legacy) |
| **Memory** | Up to 128MB RAM (configurable) |
| **Display** | VGA text mode (80x25) |
| **Input** | PS/2 keyboard |
| **Serial** | COM1 (38400 baud) |
| **Emulation** | QEMU (recommended), Bochs |

## v1.0 Feature Set

### Core Kernel
- **Preemptive multitasking** with priority-based scheduler (8 levels, up to 256 tasks)
- **Physical Memory Manager** — bitmap allocator with 4KB pages
- **Virtual Memory Manager** — paging with identity-mapped kernel space
- **Interrupt handling** — full IDT/ISR with PIC remapping
- **System timer** — PIT at 100Hz for scheduling and delays
- **System calls** — int 0x80 interface (exit, read, write, gettime, sleep)

### Bootloader
- Two-stage BIOS bootloader (MBR + Stage2)
- A20 gate enable
- Automatic CPU mode detection (32-bit or 64-bit)
- Protected mode and long mode transitions
- Interactive boot menu with timeout

### Drivers
- VGA text mode console with scrolling
- PS/2 keyboard with US QWERTY layout
- Serial port (COM1) for debugging output

### User Interface
- Built-in shell with commands: `help`, `clear`, `cpuinfo`, `meminfo`, `uptime`, `echo`, `history`

## Building

### Prerequisites

```bash
# Debian/Ubuntu
sudo apt install build-essential nasm qemu-system-x86

# Fedora
sudo dnf install gcc make nasm qemu-system-x86

# Arch Linux
sudo pacman -S base-devel nasm qemu
```

### Build Commands

```bash
# Build for x86 (32-bit, default)
make

# Build for x86-64 (64-bit)
make ARCH=x86_64

# Clean build artifacts
make clean
```

### Output

Build produces `build/TocinOS.img` — a bootable disk image.

## Running

### QEMU (Recommended)

```bash
# Basic run
qemu-system-i386 -fda build/TocinOS.img

# With serial output to terminal
qemu-system-i386 -fda build/TocinOS.img -serial stdio

# For x86-64 build
qemu-system-x86_64 -fda build/TocinOS.img -serial stdio

# With debugging
qemu-system-i386 -fda build/TocinOS.img -s -S
```

### Real Hardware

Write the image to a USB drive or floppy disk:

```bash
sudo dd if=build/TocinOS.img of=/dev/sdX bs=512
```

**Warning:** Ensure `/dev/sdX` is the correct device. This will overwrite all data.

## Architecture

```
┌─────────────────────────────────────────────────┐
│                   User Shell                     │
├─────────────────────────────────────────────────┤
│              System Call Interface               │
├─────────────────────────────────────────────────┤
│  Scheduler  │  Memory Mgr  │  Interrupt Handler │
├─────────────┼──────────────┼────────────────────┤
│   VGA       │   Keyboard   │   Serial   │ Timer │
├─────────────────────────────────────────────────┤
│            Hardware Abstraction Layer            │
└─────────────────────────────────────────────────┘
```

### Source Layout

```
TocinOS/
├── boot/
│   ├── mbr/          # Stage 1 bootloader (512 bytes)
│   └── stage2/       # Stage 2 bootloader (protected/long mode setup)
├── kernel/
│   ├── arch/         # Architecture-specific code (x86, x86_64)
│   ├── drivers/      # Device drivers
│   ├── mm/           # Memory management (PMM, VMM)
│   ├── task/         # Scheduler and task management
│   └── *.c           # Core kernel (IDT, ISR, syscall, shell, etc.)
├── include/          # Header files
├── Makefile          # Build system
└── linker_*.ld       # Linker scripts
```

## Limitations

TocinOS v1.0 is a **minimal kernel**. The following are explicitly **not supported**:

- **No filesystem access** — FAT/ext2 code exists but lacks disk backend integration
- **No UEFI boot** — UEFI structures defined but not functional
- **No networking** — Driver framework only, no protocol stack
- **No USB** — Stub implementation
- **No user-space programs** — Ring 3 framework exists but ELF loading not complete
- **No SMP** — Single CPU only
- **No dynamic memory allocation** — No malloc/free (kernel uses static allocation)
- **No ACPI/power management**

## Stability Guarantees

- Kernel will not panic on invalid user input
- Memory protection prevents user code from corrupting kernel space
- All interrupt handlers are registered and functional
- Serial output available for debugging even if VGA fails

## Non-Goals

TocinOS is **not** intended to be:

- A general-purpose desktop operating system
- POSIX-compliant
- Binary-compatible with Linux/Windows applications
- A hypervisor or virtualization platform

## Post-v1.0 Roadmap (Planned)

Future releases may include:

1. **v1.1** — IDE disk driver integration, FAT filesystem mounting
2. **v1.2** — ELF binary loading, basic user-space execution
3. **v1.3** — UEFI boot support
4. **v2.0** — Networking stack, dynamic memory allocator

## Contributing

Contributions must:

1. Target documented v1.0 scope or approved roadmap items
2. Include tests where applicable
3. Follow existing code style (K&R braces, 4-space indent)
4. Not introduce new experimental features without discussion

```bash
# Run tests
make test
```

## License

This project is provided for educational purposes. See individual file headers for licensing details.

## Acknowledgments

TocinOS draws inspiration from:

- OSDev Wiki (https://wiki.osdev.org)
- James Molloy's kernel tutorials
- Linux kernel source code (for reference only)
