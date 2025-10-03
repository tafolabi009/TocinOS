/**
 * TocinOS CPU Information Module
 * 
 * Detects and reports CPU features and capabilities
 */

#include "../include/kernel/cpu_info.h"
#include "../include/kernel/kernel.h"

static cpu_info_t cpu_info = {0};
static int cpu_detected = 0;

/**
 * Execute CPUID instruction
 */
static inline void cpuid(unsigned int func, unsigned int *eax, unsigned int *ebx, 
                         unsigned int *ecx, unsigned int *edx) {
    __asm__ volatile(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(func), "c"(0)
    );
}

/**
 * Check if CPUID is supported
 */
static int cpuid_supported(void) {
    unsigned int eflags1, eflags2;
    
    __asm__ volatile(
        "pushfl\n"
        "popl %0\n"
        "movl %0, %1\n"
        "xorl $0x200000, %0\n"
        "pushl %0\n"
        "popfl\n"
        "pushfl\n"
        "popl %0\n"
        : "=&r"(eflags1), "=&r"(eflags2)
    );
    
    return ((eflags1 ^ eflags2) & 0x200000) != 0;
}

/**
 * Detect CPU features and capabilities
 */
void cpu_detect(void) {
    if (cpu_detected) {
        return; // Already detected
    }
    
    if (!cpuid_supported()) {
        kernel_print("[!] CPUID not supported!\n");
        return;
    }
    
    unsigned int eax, ebx, ecx, edx;
    
    // Get vendor string
    cpuid(0, &eax, &ebx, &ecx, &edx);
    ((unsigned int*)cpu_info.vendor)[0] = ebx;
    ((unsigned int*)cpu_info.vendor)[1] = edx;
    ((unsigned int*)cpu_info.vendor)[2] = ecx;
    cpu_info.vendor[12] = '\0';
    
    // Get standard features
    cpuid(1, &eax, &ebx, &ecx, &edx);
    cpu_info.features_edx = edx;
    cpu_info.features_ecx = ecx;
    
    // Extract family, model, stepping
    cpu_info.stepping = eax & 0xF;
    cpu_info.model = (eax >> 4) & 0xF;
    cpu_info.family = (eax >> 8) & 0xF;
    
    // Extended model and family
    if (cpu_info.family == 0xF) {
        cpu_info.family += (eax >> 20) & 0xFF;
    }
    if (cpu_info.family == 0x6 || cpu_info.family == 0xF) {
        cpu_info.model += ((eax >> 16) & 0xF) << 4;
    }
    
    cpu_info.brand_index = ebx & 0xFF;
    cpu_info.cache_line_size = ((ebx >> 8) & 0xFF) * 8;
    cpu_info.max_logical_processors = (ebx >> 16) & 0xFF;
    
    // Check for extended CPUID
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000001) {
        cpuid(0x80000001, &eax, &ebx, &ecx, &edx);
        cpu_info.extended_features = edx;
    }
    
    cpu_detected = 1;
}

/**
 * Print CPU information
 */
void cpu_print_info(void) {
    if (!cpu_detected) {
        cpu_detect();
    }
    
    kernel_print("[*] CPU Information:\n");
    kernel_print("    Vendor: ");
    kernel_print(cpu_info.vendor);
    kernel_print("\n");
    
    // Print features
    kernel_print("    Features: ");
    
    if (cpu_info.features_edx & CPU_FEATURE_FPU) kernel_print("FPU ");
    if (cpu_info.features_edx & CPU_FEATURE_VME) kernel_print("VME ");
    if (cpu_info.features_edx & CPU_FEATURE_PSE) kernel_print("PSE ");
    if (cpu_info.features_edx & CPU_FEATURE_PAE) kernel_print("PAE ");
    if (cpu_info.features_edx & CPU_FEATURE_APIC) kernel_print("APIC ");
    if (cpu_info.features_edx & CPU_FEATURE_SEP) kernel_print("SEP ");
    if (cpu_info.features_edx & CPU_FEATURE_MTRR) kernel_print("MTRR ");
    if (cpu_info.features_edx & CPU_FEATURE_MMX) kernel_print("MMX ");
    if (cpu_info.features_edx & CPU_FEATURE_SSE) kernel_print("SSE ");
    if (cpu_info.features_edx & CPU_FEATURE_SSE2) kernel_print("SSE2 ");
    if (cpu_info.features_edx & CPU_FEATURE_HTT) kernel_print("HTT ");
    
    kernel_print("\n                ");
    
    if (cpu_info.features_ecx & CPU_FEATURE_SSE3) kernel_print("SSE3 ");
    if (cpu_info.features_ecx & CPU_FEATURE_SSSE3) kernel_print("SSSE3 ");
    if (cpu_info.features_ecx & CPU_FEATURE_SSE41) kernel_print("SSE4.1 ");
    if (cpu_info.features_ecx & CPU_FEATURE_SSE42) kernel_print("SSE4.2 ");
    if (cpu_info.features_ecx & CPU_FEATURE_AVX) kernel_print("AVX ");
    if (cpu_info.features_ecx & CPU_FEATURE_AES) kernel_print("AES ");
    if (cpu_info.features_ecx & CPU_FEATURE_RDRAND) kernel_print("RDRAND ");
    
    kernel_print("\n");
    
    // Extended features
    if (cpu_info.extended_features) {
        kernel_print("    Extended: ");
        if (cpu_info.extended_features & CPU_EXT_FEATURE_SYSCALL) kernel_print("SYSCALL ");
        if (cpu_info.extended_features & CPU_EXT_FEATURE_NX) kernel_print("NX ");
        if (cpu_info.extended_features & CPU_EXT_FEATURE_1GB_PAGE) kernel_print("1GB_PAGE ");
        if (cpu_info.extended_features & CPU_EXT_FEATURE_LONG_MODE) kernel_print("LONG_MODE ");
        kernel_print("\n");
    }
}

/**
 * Check if CPU has specific feature
 */
int cpu_has_feature(unsigned int feature) {
    if (!cpu_detected) {
        cpu_detect();
    }
    
    // Check standard features
    if (feature & 0xFFFF) {
        return (cpu_info.features_edx & feature) || (cpu_info.features_ecx & feature);
    }
    
    // Check extended features
    return (cpu_info.extended_features & feature) != 0;
}

/**
 * Get CPU vendor string
 */
const char* cpu_get_vendor(void) {
    if (!cpu_detected) {
        cpu_detect();
    }
    return cpu_info.vendor;
}

/**
 * Get CPU info structure
 */
cpu_info_t* cpu_get_info(void) {
    if (!cpu_detected) {
        cpu_detect();
    }
    return &cpu_info;
}
