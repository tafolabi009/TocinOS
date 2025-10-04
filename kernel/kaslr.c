/**
 * TocinOS KASLR Implementation
 * 
 * Kernel Address Space Layout Randomization
 */

#include "../include/kernel/kaslr.h"

// Global KASLR state
static kaslr_info_t kaslr_state = {0};

/**
 * Check if CPU supports RDRAND instruction
 */
static int cpu_has_rdrand(void) {
    uint32_t eax, ebx, ecx, edx;
    
    // CPUID with EAX=1
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
    );
    
    // RDRAND support is bit 30 of ECX
    return (ecx & (1 << 30)) != 0;
}

/**
 * Get random value using RDRAND
 */
static uint64_t rdrand64(void) {
    uint64_t value;
    int retries = 10;
    
    while (retries--) {
        unsigned char ok;
        __asm__ volatile(
            "rdrand %0; setc %1"
            : "=r"(value), "=qm"(ok)
        );
        
        if (ok) {
            return value;
        }
    }
    
    return 0;
}

/**
 * Get timestamp counter
 */
static uint64_t rdtsc(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

/**
 * Simple hash function for memory map
 */
static uint64_t get_memory_map_hash(void) {
    // Placeholder: Would hash memory map layout
    return rdtsc() ^ 0xDEADBEEF;
}

/**
 * Get hash of CPU features
 */
static uint64_t get_cpuid_hash(void) {
    uint32_t eax, ebx, ecx, edx;
    
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0)
    );
    
    return ((uint64_t)ebx << 32) | ecx;
}

/**
 * Gather entropy from various sources
 */
static uint64_t gather_entropy(void) {
    uint64_t entropy = 0;
    uint32_t sources = 0;
    
    // Try RDRAND first (best option)
    if (cpu_has_rdrand()) {
        entropy = rdrand64();
        sources |= KASLR_ENTROPY_RDRAND;
    }
    
    // Mix in timer jitter
    entropy ^= (rdtsc() << 16);
    sources |= KASLR_ENTROPY_RDTSC;
    
    // Mix in memory map
    entropy ^= get_memory_map_hash();
    sources |= KASLR_ENTROPY_MEMMAP;
    
    // Mix in CPU features
    entropy ^= get_cpuid_hash();
    sources |= KASLR_ENTROPY_CPUID;
    
    kaslr_state.entropy_source = sources;
    return entropy;
}

/**
 * Calculate randomized kernel base address
 */
static uint64_t calculate_random_base(void) {
    uint64_t entropy = gather_entropy();
    
    // Extract entropy bits and align to 2MB
    uint64_t offset = (entropy & ((1ULL << KASLR_ENTROPY_BITS) - 1));
    offset = offset * KASLR_ALIGNMENT;
    
    // Add to minimum base address
    uint64_t base = KERNEL_BASE_MIN + offset;
    
    // Ensure we don't exceed maximum
    if (base > KERNEL_BASE_MAX) {
        base = KERNEL_BASE_MAX;
    }
    
    return base;
}

/**
 * Initialize KASLR during boot
 */
void kaslr_init(void) {
    // Calculate random base address
    kaslr_state.kernel_base = calculate_random_base();
    kaslr_state.random_offset = kaslr_state.kernel_base - KERNEL_BASE_MIN;
    kaslr_state.kernel_size = 0; // To be set by kernel
    kaslr_state.enabled = 1;
}

/**
 * Get randomized kernel base address
 */
uint64_t kaslr_get_base(void) {
    if (!kaslr_state.enabled) {
        return KERNEL_BASE_MIN;
    }
    return kaslr_state.kernel_base;
}

/**
 * Check if KASLR is enabled
 */
int kaslr_is_enabled(void) {
    return kaslr_state.enabled;
}

/**
 * Get KASLR information
 */
kaslr_info_t *kaslr_get_info(void) {
    return &kaslr_state;
}

/**
 * Disable KASLR (for debugging)
 */
void kaslr_disable(void) {
    kaslr_state.enabled = 0;
    kaslr_state.kernel_base = KERNEL_BASE_MIN;
    kaslr_state.random_offset = 0;
}
