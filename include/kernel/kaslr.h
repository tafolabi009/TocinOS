/**
 * TocinOS KASLR (Kernel Address Space Layout Randomization)
 * 
 * Randomizes kernel load address for security
 */

#ifndef KASLR_H
#define KASLR_H

#include "../stdint.h"

// KASLR configuration
#define KERNEL_BASE_MIN     0xFFFFFFFF80000000ULL  // Minimum kernel base (64-bit)
#define KERNEL_BASE_MAX     0xFFFFFFFFC0000000ULL  // Maximum kernel base
#define KASLR_ENTROPY_BITS  10                      // 1024 possible positions
#define KASLR_ALIGNMENT     (2 * 1024 * 1024)       // 2MB alignment

// KASLR information structure
typedef struct {
    uint64_t kernel_base;        // Randomized kernel base address
    uint64_t kernel_size;        // Total kernel size
    uint64_t random_offset;      // Random offset applied
    uint32_t entropy_source;     // Source of randomness
    int enabled;                 // KASLR enabled flag
} kaslr_info_t;

// Entropy sources
#define KASLR_ENTROPY_RDRAND    0x01  // Hardware RNG
#define KASLR_ENTROPY_RDTSC     0x02  // Timer jitter
#define KASLR_ENTROPY_MEMMAP    0x04  // Memory map hash
#define KASLR_ENTROPY_CPUID     0x08  // CPU features

// Function prototypes

/**
 * Initialize KASLR during boot
 */
void kaslr_init(void);

/**
 * Get randomized kernel base address
 */
uint64_t kaslr_get_base(void);

/**
 * Check if KASLR is enabled
 */
int kaslr_is_enabled(void);

/**
 * Get KASLR information
 */
kaslr_info_t *kaslr_get_info(void);

/**
 * Disable KASLR (for debugging)
 */
void kaslr_disable(void);

#endif // KASLR_H
