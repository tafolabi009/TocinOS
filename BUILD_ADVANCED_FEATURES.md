# Advanced Features Build Instructions

## Overview
This document explains how to include the advanced features in your TocinOS build.

## New Components

### UEFI Boot Support
Located in `boot/uefi/`:
- `graphics.c` - GOP support
- `filesystem.c` - UEFI filesystem access
- `memory.c` - Memory map handling
- `secure_boot.c` - Secure boot verification

**Note:** These require gnu-efi to compile. They are not included in the default build.

### Kernel Components
Located in `kernel/`:
- `kaslr.c` - KASLR implementation
- `task/cfs.c` - CFS scheduler
- `fs/tocinfs.c` - TocinFS filesystem

### Header Files
Located in `include/`:
- `kernel/kaslr.h`
- `kernel/cfs.h`
- `kernel/zswap.h`
- `kernel/numa.h`
- `kernel/hugepage.h`
- `kernel/capability.h`
- `kernel/seccomp.h`
- `kernel/apparmor.h`
- `fs/tocinfs.h`

## Building UEFI Bootloader (Optional)

To compile the UEFI bootloader, you need gnu-efi:

```bash
# Install dependencies
sudo apt-get install gnu-efi

# Build UEFI bootloader
cd boot/uefi
gcc -I/usr/include/efi -fpic -ffreestanding \
    -fno-stack-protector -fno-builtin -Wl,-dll \
    -shared -Wl,-Bsymbolic -L/usr/lib -lefi \
    -o bootx64.efi bootloader.c graphics.c filesystem.c memory.c secure_boot.c
```

## Including in Kernel Build

To include the new kernel components, add them to the Makefile:

```makefile
# Add to KERNEL_C_SOURCES
KERNEL_C_SOURCES = $(wildcard $(KERNEL_DIR)/*.c) \
                   $(wildcard $(KERNEL_DIR)/mm/*.c) \
                   $(wildcard $(KERNEL_DIR)/task/*.c) \
                   $(wildcard $(KERNEL_DIR)/drivers/*.c) \
                   $(wildcard $(KERNEL_DIR)/fs/*.c) \
                   $(KERNEL_DIR)/kaslr.c
```

## Framework Status

All components are currently framework implementations:
- ✅ API defined and documented
- ✅ Stub implementations in place
- ⏳ Full implementations pending

The framework provides:
1. Complete header files with API definitions
2. Stub implementations that compile but don't execute full logic
3. Documentation of expected behavior
4. Structure for future development

## Next Steps

To fully implement these features:

1. **KASLR**: Integrate with bootloader and kernel relocation
2. **CFS**: Replace current scheduler in `kernel/task/scheduler.c`
3. **Memory Management**: Implement compression algorithms for zswap
4. **TocinFS**: Implement B-tree operations and CoW logic
5. **Security**: Integrate with process management and syscalls

## Testing

Each component can be tested independently:

```c
// Example: Test KASLR
#include "kernel/kaslr.h"

void test_kaslr(void) {
    kaslr_init();
    uint64_t base = kaslr_get_base();
    printf("KASLR base: 0x%llx\n", base);
}
```

## Documentation

For detailed API documentation, see:
- [ADVANCED_FEATURES_GUIDE.md](ADVANCED_FEATURES_GUIDE.md)
- [ADVANCED_FEATURES_ROADMAP.md](ADVANCED_FEATURES_ROADMAP.md)

## Build Requirements

Current build system requires:
- GCC (32-bit and 64-bit support)
- NASM (assembler)
- LD (linker)
- Make

Optional for UEFI:
- gnu-efi package
- OVMF firmware for testing

## Notes

- The default build still uses the existing bootloader and kernel
- New components are ready for integration but not automatically included
- All code compiles independently of the main kernel
- No breaking changes to existing functionality
