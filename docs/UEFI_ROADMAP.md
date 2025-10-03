# TocinOS UEFI Support Roadmap

## Overview

UEFI (Unified Extensible Firmware Interface) is the modern replacement for BIOS. Supporting UEFI will enable TocinOS to boot on modern hardware and take advantage of advanced features like GOP (Graphics Output Protocol), Secure Boot, and GPT partitions.

## Current Status: BIOS Boot (Legacy)

TocinOS currently boots via BIOS using:
- ✅ MBR (Master Boot Record) bootloader
- ✅ Stage 2 bootloader in real mode
- ✅ Manual mode switching (real → protected → long mode)
- ✅ BIOS interrupts for disk I/O and display

## UEFI Boot Goals

### Phase 1: Basic UEFI Boot (v2.0)
**Priority**: High
**Status**: 🎯 Planned

#### Objectives
1. Boot on UEFI firmware
2. Load kernel from GPT partition
3. Use UEFI Boot Services
4. Transition to kernel properly

#### Implementation Components

##### 1. UEFI Bootloader
```c
// UEFI application entry point
EFI_STATUS EFIAPI efi_main(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable
) {
    // Initialize UEFI services
    InitializeLib(ImageHandle, SystemTable);
    
    // Load kernel from disk
    EFI_STATUS Status = LoadKernel();
    
    // Setup boot info structure
    PrepareBootInfo();
    
    // Exit boot services
    Status = ExitBootServices();
    
    // Jump to kernel
    JumpToKernel();
    
    return EFI_SUCCESS;
}
```

##### 2. File System Access
- Use UEFI Simple File System Protocol
- Read kernel from FAT32 partition
- Load additional modules/initrd

```c
// Load kernel using UEFI file services
EFI_STATUS LoadKernelFromFS(void) {
    EFI_FILE_PROTOCOL *Root;
    EFI_FILE_PROTOCOL *KernelFile;
    
    // Open root directory
    Status = Volume->OpenVolume(Volume, &Root);
    
    // Open kernel file
    Status = Root->Open(Root, &KernelFile, L"\\EFI\\TocinOS\\kernel.bin", 
                        EFI_FILE_MODE_READ, 0);
    
    // Read kernel into memory
    UINTN KernelSize = GetFileSize(KernelFile);
    Status = KernelFile->Read(KernelFile, &KernelSize, KernelBuffer);
    
    return Status;
}
```

##### 3. Memory Map
- Get UEFI memory map
- Convert to TocinOS boot info format
- Pass to kernel

```c
// Get UEFI memory map
EFI_STATUS GetMemoryMap(boot_info_t *info) {
    UINTN MapKey;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
    
    Status = gBS->GetMemoryMap(&MapSize, MemoryMap, &MapKey, 
                               &DescriptorSize, &DescriptorVersion);
    
    // Convert to boot_info format
    ConvertMemoryMap(MemoryMap, info);
    
    return Status;
}
```

### Phase 2: Graphics Output (v2.5)
**Priority**: Medium
**Status**: 🎯 Planned

#### GOP (Graphics Output Protocol)
- Get framebuffer information
- Set video mode
- Pass framebuffer info to kernel

```c
// Setup graphics mode
EFI_STATUS SetupGraphics(boot_info_t *info) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    
    // Locate GOP
    Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, 
                                 NULL, (void**)&Gop);
    
    // Get current mode
    info->framebuffer_addr = (UINT32)Gop->Mode->FrameBufferBase;
    info->framebuffer_width = Gop->Mode->Info->HorizontalResolution;
    info->framebuffer_height = Gop->Mode->Info->VerticalResolution;
    info->framebuffer_pitch = Gop->Mode->Info->PixelsPerScanLine * 4;
    info->framebuffer_bpp = 32;
    
    return EFI_SUCCESS;
}
```

### Phase 3: Secure Boot (v3.0)
**Priority**: Low
**Status**: 🎯 Planned

#### Features
- Verify bootloader signature
- Verify kernel signature
- Use platform keys
- Support for custom keys

```c
// Verify kernel signature
EFI_STATUS VerifyKernelSignature(void *Kernel, UINTN Size) {
    // Get signature from PE header
    PE_SIGNATURE *Signature = GetPESignature(Kernel);
    
    // Verify using platform DB
    Status = VerifySignature(Signature, Size);
    
    if (EFI_ERROR(Status)) {
        Print(L"Kernel signature verification failed!\n");
        return Status;
    }
    
    return EFI_SUCCESS;
}
```

## Dual Boot Strategy

TocinOS will support both BIOS and UEFI boot modes:

### Hybrid Image Structure
```
TocinOS.img (Hybrid BIOS/UEFI)
├── MBR (sector 0) - BIOS boot
│   └── Points to Stage 2
├── GPT Header (sector 1+) - UEFI metadata
├── EFI System Partition (FAT32)
│   └── /EFI/BOOT/
│       └── BOOTX64.EFI - UEFI bootloader
├── TocinOS Boot Partition
│   ├── Stage 2 bootloader (BIOS)
│   └── kernel.bin
└── TocinOS System Partition
    └── System files
```

### Detection at Build Time
```makefile
# Build both BIOS and UEFI bootloaders
all: bios_boot uefi_boot

bios_boot: mbr.bin stage2.bin
	# Create BIOS bootable image

uefi_boot: bootx64.efi
	# Create UEFI bootable image

hybrid: bios_boot uefi_boot
	# Create hybrid image
	./create_hybrid_image.sh
```

## UEFI Development Environment

### Tools Required
- **GNU-EFI**: UEFI development library for GCC
- **EDK II**: Official UEFI development kit
- **OVMF**: UEFI firmware for QEMU testing

### Installation
```bash
# Ubuntu/Debian
sudo apt-get install gnu-efi ovmf

# Test with QEMU
qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
                   -drive format=raw,file=TocinOS.img
```

## Boot Process Comparison

### BIOS Boot (Current)
1. BIOS loads MBR (sector 0) to 0x7C00
2. MBR loads Stage 2 bootloader
3. Stage 2 enables A20, sets up GDT
4. Stage 2 loads kernel from disk
5. Stage 2 enters protected/long mode
6. Jump to kernel entry point

### UEFI Boot (Planned)
1. UEFI firmware initializes
2. UEFI loads BOOTX64.EFI from ESP
3. Bootloader uses UEFI services (already in protected mode)
4. Load kernel using File System Protocol
5. Get memory map and other info
6. Exit boot services
7. Jump to kernel (already in correct mode)

## Advantages of UEFI Boot

### For Users
- ✅ Faster boot times
- ✅ Better hardware support
- ✅ Native graphics (GOP)
- ✅ Secure boot option
- ✅ GPT partition support (>2TB drives)
- ✅ Network boot support
- ✅ Mouse support in bootloader

### For Developers
- ✅ Simpler bootloader (UEFI does the hard work)
- ✅ Rich set of protocols
- ✅ Easier debugging
- ✅ Standard API
- ✅ Better testing tools

## Implementation Timeline

| Phase | Version | Components | Status |
|-------|---------|------------|--------|
| BIOS Boot | v1.0 | MBR, Stage 2 | ✅ Implemented |
| UEFI Basics | v2.0 | BOOTX64.EFI, File loading | 🎯 Planned |
| GOP Support | v2.5 | Graphics output | 🎯 Planned |
| Hybrid Boot | v2.5 | Both BIOS & UEFI | 🎯 Planned |
| Secure Boot | v3.0 | Signature verification | 🎯 Planned |
| Network Boot | v3.0 | PXE via UEFI | 🎯 Planned |

## Testing Strategy

### UEFI Testing
1. **QEMU with OVMF**: Primary testing platform
2. **VirtualBox**: UEFI mode testing
3. **Real Hardware**: Final validation

### Test Cases
- ✓ Boot on UEFI-only systems
- ✓ Boot on BIOS-legacy systems
- ✓ Boot on hybrid systems
- ✓ Graphics output modes
- ✓ Secure boot enabled/disabled
- ✓ Multiple boot options
- ✓ File loading from FAT32

## File Structure Changes

### New Directories
```
boot/
├── bios/           # BIOS bootloader (existing)
│   ├── mbr.asm
│   └── stage2.asm
└── uefi/           # UEFI bootloader (new)
    ├── bootloader.c
    ├── graphics.c
    ├── filesystem.c
    └── Makefile
```

### New Build Targets
```makefile
# UEFI bootloader
UEFI_BOOT = build/BOOTX64.EFI

$(UEFI_BOOT): boot/uefi/*.c
	gcc -I/usr/include/efi -fpic -ffreestanding \
	    -fno-stack-protector -fno-builtin -Wl,-dll \
	    -shared -Wl,-Bsymbolic -L/usr/lib -lefi \
	    -o $(UEFI_BOOT) boot/uefi/*.c
```

## Resources & References

- [UEFI Specification](https://uefi.org/specifications)
- [GNU-EFI Documentation](https://sourceforge.net/projects/gnu-efi/)
- [OSDev Wiki: UEFI](https://wiki.osdev.org/UEFI)
- [TianoCore EDK II](https://github.com/tianocore/edk2)
- [UEFI Boot Sequence](https://en.wikipedia.org/wiki/UEFI#Boot_manager)

## Success Criteria

### Milestone 1: Basic UEFI Boot
- ✅ Bootloader compiles as EFI application
- ✅ Boots in QEMU with OVMF
- ✅ Loads kernel from filesystem
- ✅ Passes control to kernel

### Milestone 2: Feature Complete
- ✅ Graphics output working
- ✅ Memory map passed correctly
- ✅ Hybrid image boots in both modes
- ✅ Tested on real hardware

### Milestone 3: Production Ready
- ✅ Secure boot support
- ✅ Multiple video modes
- ✅ Robust error handling
- ✅ Complete documentation

---

UEFI support will modernize TocinOS boot process and enable compatibility with current and future hardware.
