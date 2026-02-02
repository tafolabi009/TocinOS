/**
 * TocinOS Process Filesystem (procfs) Header
 * 
 * Virtual filesystem exposing kernel and process information.
 */

#ifndef _TOCINOS_PROCFS_H
#define _TOCINOS_PROCFS_H

#include "../stdint.h"

/* Proc entry types */
#define PROC_TYPE_FILE      1
#define PROC_TYPE_DIR       2
#define PROC_TYPE_LINK      3

/**
 * Initialize procfs
 * 
 * @return 0 on success, -1 on failure
 */
int procfs_init(void);

/**
 * Register a custom proc entry
 * 
 * @param path Entry path under /proc
 * @param generator Content generator function
 * @return 0 on success, -1 on failure
 */
int procfs_register_entry(const char *path, 
                          int (*generator)(char *buffer, uint32_t size));

/**
 * Unregister a proc entry
 * 
 * @param path Entry path to remove
 * @return 0 on success, -1 on failure
 */
int procfs_unregister_entry(const char *path);

/**
 * Read proc entry content
 * 
 * @param path Entry path
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Bytes written or -1 on error
 */
int procfs_read(const char *path, char *buffer, uint32_t size);

#endif /* _TOCINOS_PROCFS_H */
