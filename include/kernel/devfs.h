/**
 * TocinOS Device Filesystem (devfs) Header
 * 
 * Virtual filesystem providing device nodes.
 */

#ifndef _TOCINOS_DEVFS_H
#define _TOCINOS_DEVFS_H

#include "../stdint.h"

/* Device types */
#define DEV_TYPE_CHAR       1       /* Character device */
#define DEV_TYPE_BLOCK      2       /* Block device */

/**
 * Device driver read callback
 */
typedef int (*dev_read_fn_t)(uint32_t minor, uint32_t offset, 
                              void *buffer, uint32_t size);

/**
 * Device driver write callback
 */
typedef int (*dev_write_fn_t)(uint32_t minor, uint32_t offset, 
                               const void *buffer, uint32_t size);

/**
 * Device driver ioctl callback
 */
typedef int (*dev_ioctl_fn_t)(uint32_t minor, uint32_t cmd, void *arg);

/**
 * Initialize devfs
 * 
 * @return 0 on success, -1 on failure
 */
int devfs_init(void);

/**
 * Register a device driver
 * 
 * @param name Driver name
 * @param major Major device number
 * @param read Read callback
 * @param write Write callback
 * @param ioctl Ioctl callback
 * @return 0 on success, -1 on failure
 */
int devfs_register_driver(const char *name, uint32_t major,
                          dev_read_fn_t read, dev_write_fn_t write,
                          dev_ioctl_fn_t ioctl);

/**
 * Create device node
 * 
 * @param name Device name (in /dev)
 * @param type Device type (DEV_TYPE_CHAR or DEV_TYPE_BLOCK)
 * @param major Major device number
 * @param minor Minor device number
 * @param mode Permission mode
 * @return 0 on success, -1 on failure
 */
int devfs_mknod(const char *name, uint8_t type, uint32_t major,
                uint32_t minor, uint16_t mode);

/**
 * Remove device node
 * 
 * @param name Device name to remove
 * @return 0 on success, -1 on failure
 */
int devfs_rmnod(const char *name);

/**
 * Get device major/minor by name
 * 
 * @param name Device name
 * @param major Output major number
 * @param minor Output minor number
 * @return 0 on success, -1 on failure
 */
int devfs_get_device(const char *name, uint32_t *major, uint32_t *minor);

#endif /* _TOCINOS_DEVFS_H */
