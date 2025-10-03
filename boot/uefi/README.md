# TocinOS UEFI Bootloader

## Overview

This directory contains the UEFI bootloader for TocinOS, which enables the operating system to boot on modern UEFI-based systems.

## Features

- UEFI application entry point
- Graphics Output Protocol (GOP) support for framebuffer initialization
- Memory map retrieval and processing
- Kernel loading from UEFI filesystem
- Boot information structure for kernel handoff
- Console output for debugging

## Building

### Prerequisites

To build the UEFI bootloader, you need:

```bash
# On Ubuntu/Debian
sudo apt-get install gnu-efi

# On macOS
brew install gnu-efi
```

### Build Command

```bash
gcc -I/usr/include/efi -fpic -ffreestanding \
    -fno-stack-protector -fno-builtin -Wl,-dll \
    -shared -Wl,-Bsymbolic -L/usr/lib -lefi \
    -o BOOTX64.EFI bootloader.c
```

## File Structure

```
boot/uefi/
├── bootloader.c    # Main UEFI bootloader implementation
└── README.md       # This file
```

## Integration with TocinOS

The UEFI bootloader:

1. Initializes UEFI services
2. Sets up graphics mode using GOP
3. Gets system memory map
4. Loads kernel from filesystem
5. Prepares boot information structure at address 0x8000
6. Exits boot services
7. Jumps to kernel entry point

## Boot Information Structure

The bootloader passes information to the kernel via a standardized structure:

```c
typedef struct {
    uint32_t magic;                    // 0xB007DA7A
    uint32_t boot_flags;               // BOOT_FLAG_UEFI | BOOT_FLAG_64BIT
    uint32_t framebuffer_addr;         // Framebuffer base address
    uint32_t framebuffer_width;        // Width in pixels
    uint32_t framebuffer_height;       // Height in pixels
    uint32_t framebuffer_pitch;        // Bytes per scanline
    uint32_t framebuffer_bpp;          // Bits per pixel
    // ... additional fields
} boot_info_t;
```

## UEFI Filesystem Layout

The expected filesystem layout for UEFI boot:

```
EFI/
└── TocinOS/
    ├── BOOTX64.EFI    # This bootloader
    └── kernel.bin     # TocinOS kernel
```

## Status

**Current Status**: Framework Complete

The UEFI bootloader framework is complete but requires:
- gnu-efi library for compilation
- UEFI filesystem protocol implementation
- Testing on UEFI hardware or emulator

## Testing

### QEMU with UEFI

```bash
# Install OVMF (UEFI firmware for QEMU)
sudo apt-get install ovmf

# Run QEMU with UEFI
qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
                   -drive format=raw,file=TocinOS.img
```

## Future Enhancements

- [ ] Complete filesystem protocol implementation
- [ ] Secure Boot support
- [ ] Runtime services integration
- [ ] Configuration file parsing
- [ ] Multi-boot menu
- [ ] Kernel module loading

## References

- [UEFI Specification](https://uefi.org/specifications)
- [gnu-efi Library](https://sourceforge.net/projects/gnu-efi/)
- [OSDev Wiki: UEFI](https://wiki.osdev.org/UEFI)

---

**Note**: This is a work in progress. The bootloader structure is complete but requires external dependencies and testing.
