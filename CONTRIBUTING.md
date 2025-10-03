# Contributing to TocinOS

Thank you for your interest in contributing to TocinOS! This document provides guidelines for contributing to this educational operating system project.

## Getting Started

1. **Fork the Repository**: Click the "Fork" button on GitHub
2. **Clone Your Fork**: `git clone https://github.com/YOUR_USERNAME/TocinOS.git`
3. **Create a Branch**: `git checkout -b feature/your-feature-name`

## Development Environment

### Required Tools

- **NASM** (2.14+): For assembling bootloader and kernel entry points
- **GCC** (8.0+): For compiling C code with cross-compilation support
- **GNU Binutils**: For linking
- **QEMU** (optional): For testing the OS

### Setup on Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install nasm gcc binutils make qemu-system-x86
```

### Setup on macOS

```bash
brew install nasm gcc binutils qemu
```

### Setup on Windows

Use WSL2 (Windows Subsystem for Linux) and follow Ubuntu instructions.

## Building and Testing

```bash
# Build for x86 (32-bit)
make

# Build for x86-64 (64-bit)
make ARCH=x86_64

# Test in QEMU
make run        # 32-bit
make run64      # 64-bit

# Clean build artifacts
make clean
```

## Code Style

### Assembly (NASM)
- Use 4-space indentation
- Comment extensively for educational purposes
- Use meaningful label names
- Document registers used by functions

Example:
```asm
; Function: print_string
; Inputs: SI = pointer to null-terminated string
; Outputs: None
; Modifies: AX, SI
print_string:
    lodsb                   ; Load byte from SI into AL
    or al, al               ; Check if zero
    jz .done                ; If zero, we're done
    mov ah, 0x0E            ; BIOS teletype function
    int 0x10                ; Call BIOS
    jmp print_string        ; Loop
.done:
    ret
```

### C Code
- Follow K&R style with 4-space indentation
- Use meaningful variable and function names
- Document all functions with comments
- Keep functions focused and modular

Example:
```c
/**
 * Allocate a physical page
 * 
 * Returns: Physical address of allocated page, or 0 if out of memory
 */
unsigned int pmm_alloc_page(void) {
    // Implementation
}
```

## Contribution Areas

### 1. Core Kernel Development
- Interrupt handling (IDT/ISR)
- System calls interface
- Process management improvements
- Memory management optimizations

### 2. Driver Development
- Keyboard driver (PS/2)
- Timer driver (PIT)
- Storage drivers (ATA/IDE)
- Network drivers

### 3. File System
- FAT32 support
- Custom file system design
- VFS (Virtual File System) layer

### 4. User Mode
- User space programs
- Ring 3 support
- ELF loader

### 5. Documentation
- Code comments
- Architecture documentation
- Tutorials and guides
- API documentation

### 6. Testing
- Unit tests
- Integration tests
- Boot test cases

## Submitting Changes

### Commit Messages

Use clear, descriptive commit messages:

```
[Component] Brief description

Detailed explanation of what changed and why.

- Bullet point for specific change 1
- Bullet point for specific change 2
```

Example:
```
[Memory] Improve PMM allocation algorithm

Optimized the page allocation search to reduce
time complexity from O(n) to O(1) for most cases.

- Added free page cache
- Implemented bitmap scanning optimization
```

### Pull Request Process

1. **Update Documentation**: If you add features, update relevant docs
2. **Test Thoroughly**: Ensure your changes build and run
3. **Keep Changes Focused**: One feature/fix per PR
4. **Describe Changes**: Write a clear PR description

### PR Template

```markdown
## Description
[Brief description of changes]

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Documentation update
- [ ] Performance improvement
- [ ] Code refactoring

## Testing
- [ ] Tested on x86 (32-bit)
- [ ] Tested on x86-64 (64-bit)
- [ ] Tested in QEMU
- [ ] Tested on real hardware (optional)

## Checklist
- [ ] Code follows project style guidelines
- [ ] Comments added for complex logic
- [ ] Documentation updated (if applicable)
- [ ] No compiler warnings introduced
- [ ] Changes are minimal and focused
```

## Adding New Drivers

To add a new driver using the MDF framework:

1. Create driver source file in `kernel/drivers/`
2. Implement driver operations (init, probe, open, close, etc.)
3. Register driver using `mdf_register_driver()`
4. Add driver header if needed in `include/drivers/`
5. Update Makefile to include new driver
6. Document the driver

See `kernel/drivers/vga_driver.c` for a complete example.

## Debugging Tips

### QEMU Debugging
```bash
# Run with QEMU monitor
qemu-system-i386 -drive format=raw,file=build/TocinOS.img -monitor stdio

# Run with GDB support
qemu-system-i386 -drive format=raw,file=build/TocinOS.img -s -S

# In another terminal
gdb
(gdb) target remote localhost:1234
(gdb) break kernel_main
(gdb) continue
```

### Common Issues

1. **Triple Fault**: Usually caused by invalid IDT or GDT
2. **Page Fault**: Check memory mapping and page tables
3. **General Protection Fault**: Check segment selectors and privilege levels
4. **Blank Screen**: Check VGA buffer address and bootloader jumps

## Resources

- [OSDev Wiki](https://wiki.osdev.org/)
- [Intel Manuals](https://software.intel.com/content/www/us/en/develop/articles/intel-sdm.html)
- [AMD Manuals](https://developer.amd.com/resources/developer-guides-manuals/)
- [NASM Documentation](https://www.nasm.us/xdoc/2.15.05/html/nasmdoc0.html)

## Code of Conduct

- Be respectful and constructive
- Help others learn
- Give credit where due
- Focus on education and learning

## Questions?

Feel free to:
- Open an issue for questions
- Join discussions
- Ask for help with contributions

Happy coding! 🚀
