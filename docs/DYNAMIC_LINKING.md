# TocinOS Dynamic Linking Implementation

This document describes the dynamic linking subsystem in TocinOS, which allows ELF binaries to use a dynamic linker (interpreter) for runtime symbol resolution.

## Overview

Dynamic linking in TocinOS follows the ELF specification for i386. When a dynamically-linked ELF binary is executed, the kernel:

1. Detects the `PT_INTERP` program header
2. Loads the specified interpreter (`/LD.SO`)
3. Sets up the user stack with an auxiliary vector
4. Transfers control to the interpreter instead of the program

## Components

### Kernel ELF Loader (`kernel/elf.c`)

The `elf_execute()` function handles dynamic binary loading:

```c
// Check for interpreter
if (context->dyn.interp_path[0] != '\0') {
    // Load interpreter at its specified address
    fat_file_t *interp_file = fat_open(context->dyn.interp_path);
    // Map interpreter segments
    // Set actual_entry = interpreter's entry point
}

// Set up auxiliary vector on stack
stack[idx++] = AT_PHDR;    // Program headers address
stack[idx++] = phdr_stack_addr;
stack[idx++] = AT_ENTRY;   // Program's original entry
stack[idx++] = context->entry_point;
stack[idx++] = AT_BASE;    // Interpreter base address
stack[idx++] = interp_base;
```

### Dynamic Linker (`user/ld.so/ld.c`)

The dynamic linker is a standalone ELF executable that:

1. **Parses the auxiliary vector** to find program headers and entry point
2. **Locates the `PT_DYNAMIC` segment** in the main program
3. **Processes relocations** (R_386_32, R_386_PC32, R_386_GLOB_DAT, R_386_JMP_SLOT)
4. **Transfers control** to the program's original entry point

Entry point flow:
```c
void _start(void) {
    // Pass initial ESP to _dl_start
    __asm__ volatile(
        "mov %%esp, %%eax\n"
        "push %%eax\n"
        "call _dl_start\n"
    );
}

void _dl_start(uint32_t *initial_sp) {
    // Parse argc, argv, envp, auxv from stack
    // Load program, process relocations
    // Jump to program entry
}
```

## Memory Layout

```
┌──────────────────────────────┐ 0xC0000000
│      Kernel Space            │
├──────────────────────────────┤ 0xBFFFF000
│      User Stack              │ ← ESP points here
│  [argc][argv][envp][auxv]    │
│  [program headers copy]      │
├──────────────────────────────┤ 0xBFFFC000
│         ...                  │
├──────────────────────────────┤ 0x40000000
│    Dynamic Linker (ld.so)    │ ← ET_EXEC at fixed address
├──────────────────────────────┤
│         ...                  │
├──────────────────────────────┤ 0x08048000
│    Main Program (ELF)        │ ← Program's .text, .data, etc.
├──────────────────────────────┤
│         ...                  │
└──────────────────────────────┘ 0x00000000
```

## Auxiliary Vector

The kernel provides the following auxiliary values:

| Type | Value | Description |
|------|-------|-------------|
| `AT_PHDR` | Stack address | Pointer to program headers (copied to stack) |
| `AT_PHENT` | 32 | Size of each program header entry |
| `AT_PHNUM` | varies | Number of program headers |
| `AT_PAGESZ` | 4096 | System page size |
| `AT_BASE` | 0 or 0x40000000 | Interpreter base address |
| `AT_ENTRY` | varies | Program's original entry point |
| `AT_NULL` | 0 | End of auxiliary vector |

## Relocation Types

Currently supported x86 relocation types:

| Type | Description |
|------|-------------|
| `R_386_32` | Direct 32-bit absolute |
| `R_386_PC32` | PC-relative 32-bit |
| `R_386_GLOB_DAT` | Global data (GOT entry) |
| `R_386_JMP_SLOT` | PLT jump slot |
| `R_386_RELATIVE` | Relative to base address |

## Building Dynamic Programs

### Compiler Flags

```makefile
CFLAGS = -m32 -fPIC -nostdlib -nostdinc
LDFLAGS = -m elf_i386 -dynamic-linker /LD.SO -T linker_dyn.ld
```

### Example Program

```c
// dynhello.c
#define SYS_EXIT  0
#define SYS_WRITE 1

static inline int syscall2(int num, int arg1, int arg2) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2));
    return ret;
}

void _start(void) {
    const char *msg = "Hello from dynamic program!\n";
    syscall2(SYS_WRITE, 1, (int)msg);
    syscall2(SYS_EXIT, 0, 0);
}
```

## Limitations

1. **No shared library loading** - ld.so only handles the interpreter, not additional `.so` files
2. **No lazy binding** - All relocations are processed at load time
3. **No TLS support** - Thread-local storage not implemented
4. **No symbol versioning** - All symbols are unversioned

## Future Work

- [ ] Load shared libraries (libc.so, libm.so)
- [ ] Implement lazy PLT resolution
- [ ] Add TLS support for threading
- [ ] Symbol versioning
- [ ] DT_NEEDED processing for library dependencies

## Testing

Run the dynamic linking test:

```bash
# Build
make
cd user && make

# Update disk and test
mcopy -o -i disk.img user/ld.so/ld.so ::/LD.SO
mcopy -o -i disk.img user/dynhello.elf ::/DYNHELLO.ELF

# Run
qemu-system-i386 -kernel build/kernel.elf -hda disk.img -serial stdio
```

Expected output:
```
[ELF] Found interpreter: /LD.SO
Hello from dynamically loaded program!
The dynamic linker (ld.so) was invoked to load this program.
[EXIT] Program exited with code 0
```
