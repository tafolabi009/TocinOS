/**
 * TocinOS Capability-Based Security
 * 
 * Linux-compatible capability system
 */

#ifndef CAPABILITY_H
#define CAPABILITY_H

#include "../stdint.h"

// Capability definitions (Linux-compatible)
#define CAP_CHOWN              0   // Change file ownership
#define CAP_DAC_OVERRIDE       1   // Override file access controls
#define CAP_DAC_READ_SEARCH    2   // Override read/search permissions
#define CAP_FOWNER             3   // Override file owner checks
#define CAP_FSETID             4   // Don't clear set-user/group ID bits
#define CAP_KILL               5   // Send signals
#define CAP_SETGID             6   // Set GID
#define CAP_SETUID             7   // Set UID
#define CAP_SETPCAP            8   // Transfer capabilities
#define CAP_LINUX_IMMUTABLE    9   // Set immutable and append-only flags
#define CAP_NET_BIND_SERVICE   10  // Bind to privileged ports
#define CAP_NET_BROADCAST      11  // Network broadcast
#define CAP_NET_ADMIN          12  // Network administration
#define CAP_NET_RAW            13  // Use RAW and PACKET sockets
#define CAP_IPC_LOCK           14  // Lock memory
#define CAP_IPC_OWNER          15  // Override IPC ownership
#define CAP_SYS_MODULE         16  // Load/unload kernel modules
#define CAP_SYS_RAWIO          17  // Raw I/O operations
#define CAP_SYS_CHROOT         18  // Use chroot()
#define CAP_SYS_PTRACE         19  // Trace processes
#define CAP_SYS_PACCT          20  // Process accounting
#define CAP_SYS_ADMIN          21  // System administration
#define CAP_SYS_BOOT           22  // Reboot system
#define CAP_SYS_NICE           23  // Raise process priority
#define CAP_SYS_RESOURCE       24  // Override resource limits
#define CAP_SYS_TIME           25  // Set system time
#define CAP_SYS_TTY_CONFIG     26  // Configure TTY

// Process capabilities
typedef struct process_capabilities {
    uint64_t effective;     // Currently effective capabilities
    uint64_t permitted;     // Maximum capabilities allowed
    uint64_t inheritable;   // Capabilities inherited by children
    uint64_t bounding;      // Bounding set (limits permitted)
    uint64_t ambient;       // Ambient set (always active)
} proc_caps_t;

// Function prototypes

/**
 * Check if process has capability
 */
int capable(int cap);

/**
 * Check if specific process has capability
 */
int proc_capable(void *process, int cap);

/**
 * Drop capability
 */
int cap_drop(int cap);

/**
 * Raise capability (if permitted)
 */
int cap_raise(int cap);

/**
 * Get process capabilities
 */
proc_caps_t *cap_get(void *process);

/**
 * Set process capabilities
 */
int cap_set(void *process, proc_caps_t *caps);

/**
 * Initialize capability system
 */
void cap_init(void);

#endif // CAPABILITY_H
