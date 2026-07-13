# TocinOS v2.0

A minimal, production-ready x86 operating system kernel with dynamic linking support.

## Overview

TocinOS is a bare-metal operating system designed for learning, experimentation, and embedded/research use cases. It provides a complete boot-to-shell environment with preemptive multitasking, memory protection, ELF binary execution, and dynamic linking.

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
| **Storage** | IDE/ATA disk, FAT16/FAT32 filesystem |
| **Emulation** | QEMU (recommended), Bochs |

## v2.0 Feature Set

### Core Kernel
- **Preemptive multitasking** with CFS-like scheduler
- **Physical Memory Manager** — bitmap allocator with 4KB pages
- **Virtual Memory Manager** — paging with user/kernel space separation
- **Interrupt handling** — full IDT/ISR with PIC remapping
- **System timer** — PIT at 100Hz for scheduling and delays
- **System calls** — int 0x80 interface (exit, read, write, open, close, exec, etc.)

### ELF Loader & Dynamic Linking
- **ELF32 binary loading** — full program header parsing
- **Dynamic linker (ld.so)** — loads at 0x40000000
- **Interpreter support** — PT_INTERP segment handling
- **Auxiliary vector** — AT_PHDR, AT_ENTRY, AT_BASE, AT_PHNUM, AT_PAGESZ
- **Relocation processing** — R_386_32, R_386_PC32, R_386_GLOB_DAT, R_386_JMP_SLOT

### Filesystem
- **FAT16/FAT32** — full read/write support
- **VFS layer** — unified filesystem interface
- **procfs** — process information filesystem
- **devfs** — device filesystem
- **tmpfs** — temporary in-memory filesystem

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
- IDE/ATA disk driver

### User Programs
TocinOS includes several user-space programs:

| Program | Description |
|---------|-------------|
| `hello.elf` | Simple "Hello World" test |
| `echo.elf` | Echo command-line arguments |
| `cat.elf` | Display file contents |
| `ls.elf` | List directory contents |
| `shell.elf` | Interactive command shell |
| `dynhello.elf` | Dynamic linking test program |

### User Interface
- Built-in shell with commands: `help`, `clear`, `cpuinfo`, `meminfo`, `uptime`, `echo`, `history`
- External shell program with enhanced features

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

# Build user programs
cd user && make
```

### Output

Build produces:
- `build/TocinOS.img` — bootable floppy disk image
- `build/kernel.elf` — kernel binary (for QEMU -kernel option)
- `disk.img` — FAT16 data disk with user programs

## Running

### QEMU (Recommended)

```bash
# Basic run with data disk
qemu-system-i386 -kernel build/kernel.elf -hda disk.img -serial stdio

# With display disabled (serial only)
qemu-system-i386 -kernel build/kernel.elf -hda disk.img -serial stdio -display none

# For x86-64 build
qemu-system-x86_64 -kernel build/kernel.elf -hda disk.img -serial stdio

# With debugging
qemu-system-i386 -kernel build/kernel.elf -hda disk.img -s -S -serial stdio
```

### Real Hardware

Write the image to a USB drive or floppy disk:

```bash
sudo dd if=build/TocinOS.img of=/dev/sdX bs=512
```

**Warning:** Ensure `/dev/sdX` is the correct device. This will overwrite all data.

## Dynamic Linking

TocinOS v2.0 includes a working dynamic linker (`ld.so`) that enables ELF binaries to use shared libraries.

### How It Works

1. **Kernel detects interpreter** — When loading an ELF with `PT_INTERP` segment, the kernel reads the interpreter path (e.g., `/LD.SO`)
2. **Interpreter loading** — The kernel loads `ld.so` at address `0x40000000`
3. **Stack setup** — Program headers are copied to user stack; auxiliary vector provides `AT_PHDR`, `AT_ENTRY`, `AT_BASE`
4. **Control transfer** — Kernel jumps to interpreter entry point instead of program entry
5. **Dynamic linker** — `ld.so` parses auxv, processes relocations, then transfers to program

### Building Dynamic Programs

```bash
# Compile with dynamic linking
cd user
gcc -m32 -pie -fPIC -nostdlib -o dynhello.elf dynhello.c crt0.o -Wl,--dynamic-linker=/LD.SO
```

### Example Output

```
[ELF] Found interpreter: /LD.SO
Hello from dynamically loaded program!
The dynamic linker (ld.so) was invoked to load this program.
[EXIT] Program exited with code 0
```

## Architecture

```
┌─────────────────────────────────────────────────┐
│              User Programs (ELF)                 │
├─────────────────────────────────────────────────┤
│         Dynamic Linker (ld.so)                   │
├─────────────────────────────────────────────────┤
│              System Call Interface               │
├─────────────────────────────────────────────────┤
│  Scheduler  │  Memory Mgr  │  ELF Loader        │
├─────────────┼──────────────┼────────────────────┤
│   FAT FS    │   VFS        │  IDE Driver        │
├─────────────────────────────────────────────────┤
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
│   ├── stage2/       # Stage 2 bootloader (protected/long mode setup)
│   └── uefi/         # UEFI bootloader (experimental)
├── kernel/
│   ├── arch/         # Architecture-specific code (x86, x86_64)
│   ├── drivers/      # Device drivers (VGA, IDE, keyboard, serial)
│   ├── mm/           # Memory management (PMM, VMM, slab, buddy)
│   ├── task/         # Scheduler, threads, CFS
│   ├── fs/           # Virtual filesystems (procfs, devfs, tmpfs)
│   ├── elf.c         # ELF loader with dynamic linking support
│   ├── fat.c         # FAT16/FAT32 filesystem
│   ├── vfs.c         # Virtual filesystem layer
│   ├── syscall.c     # System call interface
│   └── *.c           # Core kernel (IDT, ISR, shell, etc.)
├── user/
│   ├── ld.so/        # Dynamic linker
│   ├── libc/         # C library for user programs
│   └── *.c           # User programs (hello, echo, cat, ls, shell)
├── include/          # Header files
├── tests/            # Unit and integration tests
├── tools/            # Build and test scripts
├── Makefile          # Build system
└── linker_*.ld       # Linker scripts
```

## Limitations

TocinOS v2.0 is a **minimal kernel**. The following are current limitations:

- **No networking** — TCP/IP stack framework exists but not connected
- **No USB** — Framework only, controllers not functional
- **No SMP** — Single CPU only
- **No ACPI/power management**
- **No shared libraries** — Dynamic linker loads interpreter only
- **32-bit userspace only** — x86-64 kernel runs 32-bit programs

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

## Roadmap

TocinOS is evolving into a general-purpose operating system — custom UEFI bootloader
(TocinBoot), petabyte-scale filesystem (TocinFS v2), native `.tox`/`.tap` executable and
packaging formats, Linux application compatibility, a compositor-based GUI, and userspace
development in the [Tocin language](https://github.com/tafolabi009/tocinlang) alongside
C and C++.

See **[docs/ROADMAP.md](docs/ROADMAP.md)** for the full architecture decisions, milestone
plan (M0–M8), and acceptance criteria.

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
