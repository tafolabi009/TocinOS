/**
 * TocinOS Temporary Filesystem (tmpfs) Header
 * 
 * RAM-based filesystem for temporary storage.
 */

#ifndef _TOCINOS_TMPFS_H
#define _TOCINOS_TMPFS_H

#include "../stdint.h"

/**
 * Initialize tmpfs
 * 
 * @return 0 on success, -1 on failure
 */
int tmpfs_init(void);

/**
 * Get tmpfs usage statistics
 * 
 * @param mountpoint Mount point to query
 * @param used Output: bytes used
 * @param max Output: maximum bytes
 * @return 0 on success, -1 on failure
 */
int tmpfs_get_stats(const char *mountpoint, uint32_t *used, uint32_t *max);

/**
 * Create a file in tmpfs
 * 
 * @param path File path
 * @param data Initial data (can be NULL)
 * @param size Data size
 * @return 0 on success, -1 on failure
 */
int tmpfs_create_file(const char *path, const void *data, uint32_t size);

/**
 * Read a file from tmpfs
 * 
 * @param path File path
 * @param buffer Output buffer
 * @param size Buffer size
 * @param offset Read offset
 * @return Bytes read or -1 on error
 */
int tmpfs_read_file(const char *path, void *buffer, uint32_t size, uint32_t offset);

#endif /* _TOCINOS_TMPFS_H */
