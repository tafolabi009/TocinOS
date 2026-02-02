# TocinOS Dynamic Linker (ld.so)

This directory contains the TocinOS dynamic linker/loader implementation.

## Overview

`ld.so` is the runtime dynamic linker that processes ELF binaries with shared library dependencies. When the kernel loads a dynamically-linked program, it detects the `PT_INTERP` segment and loads this interpreter instead of jumping directly to the program.

## Files

| File | Description |
|------|-------------|
| `ld.c` | Main dynamic linker implementation |
| `linker.ld` | Linker script (loads at 0x40000000) |
| `Makefile` | Build configuration |

## Building

```bash
make        # Build ld.so
make clean  # Remove build artifacts
```

## How It Works

### 1. Kernel Loads Interpreter

When an ELF with `PT_INTERP` is executed:
1. Kernel reads interpreter path from ELF (e.g., `/LD.SO`)
2. Kernel loads `ld.so` at its specified base address (0x40000000)
3. Kernel sets up stack with auxiliary vector
4. Kernel jumps to `ld.so`'s entry point

### 2. Dynamic Linker Startup

```c
void _start(void) {
    // ESP points to: [argc][argv...][NULL][envp...][NULL][auxv...]
    // Pass ESP to main function
    __asm__ volatile("mov %%esp, %%eax; push %%eax; call _dl_start");
}
```

### 3. Parse Auxiliary Vector

```c
void _dl_start(uint32_t *sp) {
    int argc = *sp++;
    char **argv = (char **)sp;
    sp += argc + 1;  // Skip argv + NULL
    char **envp = (char **)sp;
    while (*sp) sp++; sp++;  // Skip envp + NULL
    
    // Now sp points to auxv
    elf32_auxv_t *auxv = (elf32_auxv_t *)sp;
    // Parse AT_PHDR, AT_PHNUM, AT_ENTRY, AT_BASE, etc.
}
```

### 4. Process Relocations

For each relocation entry:
- `R_386_32`: `*ptr = symbol_value + addend`
- `R_386_PC32`: `*ptr = symbol_value - ptr_address + addend`
- `R_386_GLOB_DAT`: `*ptr = symbol_value`
- `R_386_JMP_SLOT`: `*ptr = symbol_value`
- `R_386_RELATIVE`: `*ptr = base + addend`

### 5. Transfer to Program

```c
void (*entry)(void) = (void *)at_entry_value;
entry();  // Jump to program's _start
```

## Memory Layout

```
0x40000000  ┌─────────────────┐
            │  .text          │  Code
            │  .rodata        │  Read-only data
            │  .data          │  Initialized data
            │  .bss           │  Zero-initialized data
0x40001000  └─────────────────┘
```

## Supported Features

- ✅ ELF32 auxiliary vector parsing
- ✅ `R_386_32` relocations
- ✅ `R_386_PC32` relocations
- ✅ `R_386_GLOB_DAT` relocations
- ✅ `R_386_JMP_SLOT` relocations
- ✅ `R_386_RELATIVE` relocations
- ✅ Transfer control to main program

## Not Yet Implemented

- ❌ Shared library loading (DT_NEEDED)
- ❌ Lazy PLT binding
- ❌ Thread-local storage (TLS)
- ❌ Symbol versioning
- ❌ dlopen/dlsym API

## Debugging

Add debug output by defining `DEBUG_LD`:

```c
#define DEBUG_LD 1
```

This enables verbose logging of:
- Stack pointer and argc
- Auxiliary vector entries
- Relocation processing
- Symbol resolution

## Testing

```bash
# Build everything
cd /workspaces/TocinOS
make
cd user && make

# Copy to disk
mcopy -o -i ../disk.img ld.so/ld.so ::/LD.SO
mcopy -o -i ../disk.img dynhello.elf ::/DYNHELLO.ELF

# Test
qemu-system-i386 -kernel ../build/kernel.elf -hda ../disk.img -serial stdio
```

## References

- ELF Specification: https://refspecs.linuxfoundation.org/elf/elf.pdf
- System V ABI i386: https://www.uclibc.org/docs/psABI-i386.pdf
- Linux ld.so source: glibc/elf/rtld.c
