/**
 * TocinOS CPU Information Header
 * 
 * CPU feature detection and information
 */

#ifndef CPU_INFO_H
#define CPU_INFO_H

// CPU feature flags (from CPUID EDX register)
#define CPU_FEATURE_FPU         (1 << 0)   // Floating Point Unit
#define CPU_FEATURE_VME         (1 << 1)   // Virtual 8086 Mode Extensions
#define CPU_FEATURE_DE          (1 << 2)   // Debugging Extensions
#define CPU_FEATURE_PSE         (1 << 3)   // Page Size Extension
#define CPU_FEATURE_TSC         (1 << 4)   // Time Stamp Counter
#define CPU_FEATURE_MSR         (1 << 5)   // Model Specific Registers
#define CPU_FEATURE_PAE         (1 << 6)   // Physical Address Extension
#define CPU_FEATURE_MCE         (1 << 7)   // Machine Check Exception
#define CPU_FEATURE_CX8         (1 << 8)   // CMPXCHG8 instruction
#define CPU_FEATURE_APIC        (1 << 9)   // APIC on chip
#define CPU_FEATURE_SEP         (1 << 11)  // SYSENTER/SYSEXIT
#define CPU_FEATURE_MTRR        (1 << 12)  // Memory Type Range Registers
#define CPU_FEATURE_PGE         (1 << 13)  // Page Global Enable
#define CPU_FEATURE_MCA         (1 << 14)  // Machine Check Architecture
#define CPU_FEATURE_CMOV        (1 << 15)  // Conditional Move
#define CPU_FEATURE_PAT         (1 << 16)  // Page Attribute Table
#define CPU_FEATURE_PSE36       (1 << 17)  // 36-bit Page Size Extension
#define CPU_FEATURE_PSN         (1 << 18)  // Processor Serial Number
#define CPU_FEATURE_CLFSH       (1 << 19)  // CLFLUSH instruction
#define CPU_FEATURE_MMX         (1 << 23)  // MMX Technology
#define CPU_FEATURE_FXSR        (1 << 24)  // FXSAVE/FXRSTOR
#define CPU_FEATURE_SSE         (1 << 25)  // SSE extensions
#define CPU_FEATURE_SSE2        (1 << 26)  // SSE2 extensions
#define CPU_FEATURE_HTT         (1 << 28)  // Hyper-Threading Technology

// CPU feature flags (from CPUID ECX register)
#define CPU_FEATURE_SSE3        (1 << 0)   // SSE3 extensions
#define CPU_FEATURE_PCLMULQDQ   (1 << 1)   // PCLMULQDQ instruction
#define CPU_FEATURE_MONITOR     (1 << 3)   // MONITOR/MWAIT
#define CPU_FEATURE_SSSE3       (1 << 9)   // Supplemental SSE3
#define CPU_FEATURE_FMA         (1 << 12)  // Fused Multiply-Add
#define CPU_FEATURE_CX16        (1 << 13)  // CMPXCHG16B instruction
#define CPU_FEATURE_SSE41       (1 << 19)  // SSE4.1
#define CPU_FEATURE_SSE42       (1 << 20)  // SSE4.2
#define CPU_FEATURE_POPCNT      (1 << 23)  // POPCNT instruction
#define CPU_FEATURE_AES         (1 << 25)  // AES instruction set
#define CPU_FEATURE_XSAVE       (1 << 26)  // XSAVE/XRSTOR
#define CPU_FEATURE_AVX         (1 << 28)  // Advanced Vector Extensions
#define CPU_FEATURE_F16C        (1 << 29)  // F16C (half-precision) FP
#define CPU_FEATURE_RDRAND      (1 << 30)  // RDRAND instruction

// Extended CPU features (from extended CPUID)
#define CPU_EXT_FEATURE_SYSCALL (1 << 11)  // SYSCALL/SYSRET
#define CPU_EXT_FEATURE_NX      (1 << 20)  // Execute Disable Bit
#define CPU_EXT_FEATURE_1GB_PAGE (1 << 26) // 1GB pages
#define CPU_EXT_FEATURE_LONG_MODE (1 << 29) // Long Mode (64-bit)
#define CPU_EXT_FEATURE_3DNOW   (1 << 31)  // 3DNow!

/**
 * CPU information structure
 */
typedef struct {
    char vendor[13];                // CPU vendor string (null-terminated)
    unsigned int features_edx;      // Standard features (EDX)
    unsigned int features_ecx;      // Standard features (ECX)
    unsigned int extended_features; // Extended features
    unsigned int family;            // CPU family
    unsigned int model;             // CPU model
    unsigned int stepping;          // CPU stepping
    unsigned int brand_index;       // Brand index
    unsigned int cache_line_size;   // Cache line size in bytes
    unsigned int max_logical_processors; // Maximum logical processors
} cpu_info_t;

// CPU information functions
void cpu_detect(void);
void cpu_print_info(void);
int cpu_has_feature(unsigned int feature);
const char* cpu_get_vendor(void);
cpu_info_t* cpu_get_info(void);

#endif // CPU_INFO_H
