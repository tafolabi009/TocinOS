/**
 * TocinOS Dynamic Linker (ld.so)
 * 
 * Minimal runtime dynamic linker for loading shared libraries
 * and resolving symbols at runtime.
 */

#include "../libc/syscall.h"

// ELF types (must match kernel definitions)
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;

// ELF32 header
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf32_ehdr_t;

// ELF32 program header
typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} elf32_phdr_t;

// ELF32 dynamic entry
typedef struct {
    int32_t  d_tag;
    union {
        uint32_t d_val;
        uint32_t d_ptr;
    } d_un;
} elf32_dyn_t;

// ELF32 symbol
typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} elf32_sym_t;

// ELF32 relocation
typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} elf32_rel_t;

// Auxiliary vector entry
typedef struct {
    uint32_t a_type;
    union {
        uint32_t a_val;
    } a_un;
} elf32_auxv_t;

// Program header types
#define PT_LOAD     1
#define PT_DYNAMIC  2

// Dynamic tags
#define DT_NULL     0
#define DT_NEEDED   1
#define DT_PLTRELSZ 2
#define DT_PLTGOT   3
#define DT_HASH     4
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_STRSZ    10
#define DT_SYMENT   11
#define DT_INIT     12
#define DT_FINI     13
#define DT_REL      17
#define DT_RELSZ    18
#define DT_RELENT   19
#define DT_JMPREL   23

// Relocation types
#define R_386_NONE      0
#define R_386_32        1
#define R_386_PC32      2
#define R_386_GLOB_DAT  6
#define R_386_JMP_SLOT  7
#define R_386_RELATIVE  8

// Relocation macros
#define ELF32_R_SYM(i)  ((i) >> 8)
#define ELF32_R_TYPE(i) ((i) & 0xff)

// Auxiliary vector types
#define AT_NULL     0
#define AT_PHDR     3
#define AT_PHENT    4
#define AT_PHNUM    5
#define AT_PAGESZ   6
#define AT_BASE     7
#define AT_ENTRY    9

// Loaded library info
typedef struct {
    uint32_t base;          // Load base address
    elf32_dyn_t *dynamic;   // Dynamic section
    char *strtab;           // String table
    elf32_sym_t *symtab;    // Symbol table
    uint32_t *hash;         // Hash table
    elf32_rel_t *rel;       // REL relocations
    uint32_t rel_count;
    elf32_rel_t *plt_rel;   // PLT relocations
    uint32_t plt_rel_count;
    uint32_t *got;          // GOT
    void (*init)(void);     // Init function
    void (*fini)(void);     // Fini function
} lib_info_t;

// Maximum number of loaded libraries
#define MAX_LIBS 16
static lib_info_t loaded_libs[MAX_LIBS];
static int num_libs = 0;

// The main executable info
static lib_info_t main_exe;

// Debug output
static void ld_puts(const char *s) {
    int len = 0;
    while (s[len]) len++;
    syscall3(SYS_WRITE, 1, (int)s, len);
}

static void ld_putchar(char c) {
    syscall3(SYS_WRITE, 1, (int)&c, 1);
}

static void ld_puthex(uint32_t val) {
    static const char hex[] = "0123456789abcdef";
    char buf[11] = "0x00000000";
    for (int i = 9; i >= 2; i--) {
        buf[i] = hex[val & 0xf];
        val >>= 4;
    }
    ld_puts(buf);
}

// String compare
static int ld_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

// ELF hash function
static uint32_t elf_hash(const char *name) {
    uint32_t h = 0, g;
    while (*name) {
        h = (h << 4) + (unsigned char)*name++;
        if ((g = h & 0xf0000000)) {
            h ^= g >> 24;
        }
        h &= ~g;
    }
    return h;
}

// Look up a symbol in a library
static void *lookup_symbol_in_lib(lib_info_t *lib, const char *name) {
    if (!lib->symtab || !lib->strtab || !lib->hash) {
        return (void*)0;
    }
    
    uint32_t nbucket = lib->hash[0];
    uint32_t nchain = lib->hash[1];
    uint32_t *bucket = &lib->hash[2];
    uint32_t *chain = &lib->hash[2 + nbucket];
    
    uint32_t h = elf_hash(name);
    uint32_t idx = bucket[h % nbucket];
    
    while (idx != 0 && idx < nchain) {
        elf32_sym_t *sym = &lib->symtab[idx];
        const char *sym_name = &lib->strtab[sym->st_name];
        
        if (ld_strcmp(name, sym_name) == 0 && sym->st_shndx != 0) {
            return (void *)(lib->base + sym->st_value);
        }
        
        idx = chain[idx];
    }
    
    return (void*)0;
}

// Look up a symbol globally
static void *lookup_symbol(const char *name) {
    // Search main executable first
    void *sym = lookup_symbol_in_lib(&main_exe, name);
    if (sym) return sym;
    
    // Search loaded libraries
    for (int i = 0; i < num_libs; i++) {
        sym = lookup_symbol_in_lib(&loaded_libs[i], name);
        if (sym) return sym;
    }
    
    return (void*)0;
}

// Apply a single relocation
static int apply_relocation(lib_info_t *lib, elf32_rel_t *rel) {
    uint32_t type = ELF32_R_TYPE(rel->r_info);
    uint32_t sym_idx = ELF32_R_SYM(rel->r_info);
    
    uint32_t *target = (uint32_t *)(lib->base + rel->r_offset);
    uint32_t sym_val = 0;
    
    if (sym_idx != 0 && lib->symtab) {
        elf32_sym_t *sym = &lib->symtab[sym_idx];
        if (sym->st_shndx != 0) {
            sym_val = lib->base + sym->st_value;
        } else {
            // Look up externally
            const char *sym_name = &lib->strtab[sym->st_name];
            sym_val = (uint32_t)lookup_symbol(sym_name);
            if (!sym_val) {
                ld_puts("[ld.so] Undefined symbol: ");
                ld_puts(sym_name);
                ld_putchar('\n');
                return -1;
            }
        }
    }
    
    switch (type) {
        case R_386_NONE:
            break;
        case R_386_32:
            *target += sym_val;
            break;
        case R_386_PC32:
            *target += sym_val - (uint32_t)target;
            break;
        case R_386_GLOB_DAT:
        case R_386_JMP_SLOT:
            *target = sym_val;
            break;
        case R_386_RELATIVE:
            *target += lib->base;
            break;
        default:
            ld_puts("[ld.so] Unknown relocation type\n");
            return -1;
    }
    
    return 0;
}

// Process all relocations for a library
static int process_relocations(lib_info_t *lib) {
    // Process REL relocations
    if (lib->rel && lib->rel_count > 0) {
        for (uint32_t i = 0; i < lib->rel_count; i++) {
            if (apply_relocation(lib, &lib->rel[i]) < 0) {
                // Continue anyway for weak symbols
            }
        }
    }
    
    // Process PLT relocations
    if (lib->plt_rel && lib->plt_rel_count > 0) {
        for (uint32_t i = 0; i < lib->plt_rel_count; i++) {
            if (apply_relocation(lib, &lib->plt_rel[i]) < 0) {
                // Continue anyway
            }
        }
    }
    
    return 0;
}

// Parse dynamic section of a library
static int parse_dynamic(lib_info_t *lib, elf32_dyn_t *dyn) {
    lib->dynamic = dyn;
    
    uint32_t strtab_addr = 0;
    uint32_t symtab_addr = 0;
    uint32_t rel_addr = 0;
    uint32_t rel_size = 0;
    uint32_t plt_rel_addr = 0;
    uint32_t plt_rel_size = 0;
    
    for (; dyn->d_tag != DT_NULL; dyn++) {
        switch (dyn->d_tag) {
            case DT_STRTAB:
                strtab_addr = dyn->d_un.d_ptr;
                break;
            case DT_SYMTAB:
                symtab_addr = dyn->d_un.d_ptr;
                break;
            case DT_HASH:
                lib->hash = (uint32_t *)(lib->base + dyn->d_un.d_ptr);
                break;
            case DT_REL:
                rel_addr = dyn->d_un.d_ptr;
                break;
            case DT_RELSZ:
                rel_size = dyn->d_un.d_val;
                break;
            case DT_JMPREL:
                plt_rel_addr = dyn->d_un.d_ptr;
                break;
            case DT_PLTRELSZ:
                plt_rel_size = dyn->d_un.d_val;
                break;
            case DT_PLTGOT:
                lib->got = (uint32_t *)(lib->base + dyn->d_un.d_ptr);
                break;
            case DT_INIT:
                lib->init = (void (*)(void))(lib->base + dyn->d_un.d_ptr);
                break;
            case DT_FINI:
                lib->fini = (void (*)(void))(lib->base + dyn->d_un.d_ptr);
                break;
        }
    }
    
    // Convert addresses to pointers
    if (strtab_addr) lib->strtab = (char *)(lib->base + strtab_addr);
    if (symtab_addr) lib->symtab = (elf32_sym_t *)(lib->base + symtab_addr);
    if (rel_addr) {
        lib->rel = (elf32_rel_t *)(lib->base + rel_addr);
        lib->rel_count = rel_size / sizeof(elf32_rel_t);
    }
    if (plt_rel_addr) {
        lib->plt_rel = (elf32_rel_t *)(lib->base + plt_rel_addr);
        lib->plt_rel_count = plt_rel_size / sizeof(elf32_rel_t);
    }
    
    return 0;
}

// Setup GOT for lazy binding
static void setup_got(lib_info_t *lib) {
    if (!lib->got) return;
    
    // GOT[0] = address of dynamic section
    lib->got[0] = (uint32_t)lib->dynamic;
    // GOT[1] = pointer to lib_info_t for resolver
    lib->got[1] = (uint32_t)lib;
    // GOT[2] = address of resolver function
    // For now, we do eager binding, so this isn't needed
}

// Lazy binding resolver (PLT stub calls this)
void *_dl_runtime_resolve(lib_info_t *lib, uint32_t reloc_idx) {
    elf32_rel_t *rel = &lib->plt_rel[reloc_idx];
    uint32_t sym_idx = ELF32_R_SYM(rel->r_info);
    elf32_sym_t *sym = &lib->symtab[sym_idx];
    const char *name = &lib->strtab[sym->st_name];
    
    void *addr = lookup_symbol(name);
    if (!addr) {
        ld_puts("[ld.so] Lazy resolve failed: ");
        ld_puts(name);
        ld_putchar('\n');
        syscall1(SYS_EXIT, 127);
    }
    
    // Patch GOT entry
    uint32_t *got_entry = (uint32_t *)(lib->base + rel->r_offset);
    *got_entry = (uint32_t)addr;
    
    return addr;
}

// Main entry point for dynamic linker
// Called by kernel with special stack setup:
// [argc] [argv...] [NULL] [envp...] [NULL] [auxv...]
void _dl_start(void) {
    ld_puts("[ld.so] TocinOS Dynamic Linker starting\n");
    
    // Get stack pointer
    uint32_t *sp;
    __asm__ volatile("mov %%esp, %0" : "=r"(sp));
    
    // Parse auxiliary vector to find main program info
    int argc = (int)*sp++;
    char **argv = (char **)sp;
    sp += argc + 1;  // Skip argv and NULL
    char **envp = (char **)sp;
    while (*sp) sp++;  // Skip envp
    sp++;  // Skip NULL
    
    // Parse auxv
    elf32_auxv_t *auxv = (elf32_auxv_t *)sp;
    elf32_phdr_t *phdr = (void*)0;
    uint32_t phent = 0, phnum = 0;
    uint32_t entry = 0, base = 0;
    
    while (auxv->a_type != AT_NULL) {
        switch (auxv->a_type) {
            case AT_PHDR:
                phdr = (elf32_phdr_t *)auxv->a_un.a_val;
                break;
            case AT_PHENT:
                phent = auxv->a_un.a_val;
                break;
            case AT_PHNUM:
                phnum = auxv->a_un.a_val;
                break;
            case AT_ENTRY:
                entry = auxv->a_un.a_val;
                break;
            case AT_BASE:
                base = auxv->a_un.a_val;
                break;
        }
        auxv++;
    }
    
    ld_puts("[ld.so] Program entry: ");
    ld_puthex(entry);
    ld_putchar('\n');
    
    // Find main program's DYNAMIC segment
    main_exe.base = 0;  // Main exe has fixed base
    
    for (uint32_t i = 0; i < phnum; i++) {
        elf32_phdr_t *ph = (elf32_phdr_t *)((uint8_t *)phdr + i * phent);
        if (ph->p_type == PT_DYNAMIC) {
            parse_dynamic(&main_exe, (elf32_dyn_t *)ph->p_vaddr);
            break;
        }
    }
    
    // Process relocations
    ld_puts("[ld.so] Processing relocations...\n");
    process_relocations(&main_exe);
    
    // Call init functions
    if (main_exe.init) {
        ld_puts("[ld.so] Calling init\n");
        main_exe.init();
    }
    
    // Transfer control to main program
    ld_puts("[ld.so] Transferring control to main program\n");
    
    void (*entry_fn)(void) = (void (*)(void))entry;
    entry_fn();
    
    // Should not return, but just in case
    syscall1(SYS_EXIT, 0);
}

// Entry point (called by kernel)
__attribute__((section(".text.start")))
__attribute__((naked))
void _start(void) {
    __asm__ volatile(
        "xor %%ebp, %%ebp\n"     // Clear frame pointer
        "call _dl_start\n"        // Call linker main
        "1: hlt\n"
        "jmp 1b\n"
        ::: "memory"
    );
}
