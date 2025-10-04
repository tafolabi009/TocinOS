/**
 * TocinOS Seccomp (Secure Computing)
 * 
 * System call filtering using BPF
 */

#ifndef SECCOMP_H
#define SECCOMP_H

#include "../stdint.h"

// Seccomp modes
enum seccomp_mode {
    SECCOMP_MODE_DISABLED = 0,
    SECCOMP_MODE_STRICT = 1,      // Only read, write, exit, sigreturn
    SECCOMP_MODE_FILTER = 2,      // Custom BPF filter
};

// Seccomp return values
#define SECCOMP_RET_KILL_PROCESS  0x80000000U  // Kill process
#define SECCOMP_RET_KILL_THREAD   0x00000000U  // Kill thread
#define SECCOMP_RET_TRAP          0x00030000U  // Send SIGSYS
#define SECCOMP_RET_ERRNO         0x00050000U  // Return errno
#define SECCOMP_RET_TRACE         0x7ff00000U  // Notify tracer
#define SECCOMP_RET_LOG           0x7ffc0000U  // Log syscall
#define SECCOMP_RET_ALLOW         0x7fff0000U  // Allow syscall

// BPF instruction structure (simplified)
typedef struct bpf_insn {
    uint16_t code;     // Instruction opcode
    uint8_t jt;        // Jump if true
    uint8_t jf;        // Jump if false
    uint32_t k;        // Constant parameter
} bpf_insn_t;

// BPF program
typedef struct bpf_program {
    uint16_t len;           // Number of instructions
    bpf_insn_t *insns;      // Array of instructions
} bpf_program_t;

// Seccomp data (passed to BPF program)
typedef struct seccomp_data {
    int nr;                 // System call number
    uint32_t arch;          // CPU architecture
    uint64_t instruction_pointer;
    uint64_t args[6];       // System call arguments
} seccomp_data_t;

// Seccomp filter
typedef struct seccomp_filter {
    bpf_program_t program;      // BPF program
    uint32_t refcount;          // Reference count
    struct seccomp_filter *prev; // Previous filter (stacked)
} seccomp_filter_t;

// Function prototypes

/**
 * Install seccomp filter
 */
int seccomp_set_mode_filter(seccomp_filter_t *filter);

/**
 * Set seccomp mode to strict
 */
int seccomp_set_mode_strict(void);

/**
 * Check system call against filter
 */
uint32_t seccomp_run_filter(seccomp_filter_t *filter, seccomp_data_t *data);

/**
 * Get current seccomp mode
 */
int seccomp_get_mode(void);

/**
 * Initialize seccomp subsystem
 */
void seccomp_init(void);

// BPF instruction macros
#define BPF_LD   0x00
#define BPF_W    0x00
#define BPF_ABS  0x20
#define BPF_JMP  0x05
#define BPF_JEQ  0x10
#define BPF_K    0x00
#define BPF_RET  0x06

#define BPF_STMT(code, k) { (code), 0, 0, (k) }
#define BPF_JUMP(code, k, jt, jf) { (code), (jt), (jf), (k) }

#endif // SECCOMP_H
