/**
 * TocinOS AppArmor-style Mandatory Access Control (MAC)
 * 
 * Profile-based security enforcement
 */

#ifndef APPARMOR_H
#define APPARMOR_H

#include "../stdint.h"

// AppArmor modes
#define AA_MODE_ENFORCE   0  // Deny and log violations
#define AA_MODE_COMPLAIN  1  // Allow but log violations
#define AA_MODE_DISABLED  2  // Disabled

// Access modes
#define AA_MAY_READ       0x01
#define AA_MAY_WRITE      0x02
#define AA_MAY_EXEC       0x04
#define AA_MAY_APPEND     0x08
#define AA_MAY_LINK       0x10
#define AA_MAY_LOCK       0x20

// Path rule structure
typedef struct aa_path_rule {
    char *path;                     // Path pattern
    uint32_t allowed_access;        // Allowed access modes
    struct aa_path_rule *next;      // Next rule
} aa_path_rule_t;

// Network rule structure
typedef struct aa_network_rule {
    uint32_t family;                // Address family
    uint32_t type;                  // Socket type
    uint32_t protocol;              // Protocol
    struct aa_network_rule *next;   // Next rule
} aa_network_rule_t;

// Capability rule structure
typedef struct aa_capability_rule {
    uint32_t capability;            // Capability number
    int allowed;                    // 1 if allowed, 0 if denied
    struct aa_capability_rule *next; // Next rule
} aa_capability_rule_t;

// AppArmor rules
typedef struct aa_rules {
    aa_path_rule_t *paths;          // File path rules
    aa_network_rule_t *network;     // Network rules
    aa_capability_rule_t *caps;     // Capability rules
} aa_rules_t;

// AppArmor profile
typedef struct aa_profile {
    char name[256];                 // Profile name
    uint32_t mode;                  // Enforce, complain, disabled
    aa_rules_t rules;               // Profile rules
    
    struct aa_profile *parent;      // Parent profile
    struct aa_profile *children;    // Child profiles
    struct aa_profile *next;        // Next profile
} aa_profile_t;

// Function prototypes

/**
 * Initialize AppArmor subsystem
 */
void aa_init(void);

/**
 * Load profile
 */
int aa_load_profile(const char *profile_data);

/**
 * Unload profile
 */
int aa_unload_profile(const char *name);

/**
 * Check file access permission
 */
int aa_file_permission(aa_profile_t *profile, const char *path, int access_mode);

/**
 * Check network permission
 */
int aa_network_permission(aa_profile_t *profile, int family, int type, int protocol);

/**
 * Check capability permission
 */
int aa_capability_permission(aa_profile_t *profile, int capability);

/**
 * Get profile by name
 */
aa_profile_t *aa_get_profile(const char *name);

/**
 * Attach profile to process
 */
int aa_attach_profile(void *process, aa_profile_t *profile);

/**
 * Log access violation
 */
void aa_log_denied(aa_profile_t *profile, const char *path, int access_mode);

#endif // APPARMOR_H
