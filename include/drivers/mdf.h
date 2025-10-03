/**
 * TocinOS Modular Driver Framework (MDF) Header
 */

#ifndef MDF_H
#define MDF_H

// Driver types
typedef enum {
    DRIVER_TYPE_BLOCK,
    DRIVER_TYPE_CHARACTER,
    DRIVER_TYPE_NETWORK,
    DRIVER_TYPE_USB,
    DRIVER_TYPE_PCI,
    DRIVER_TYPE_GENERIC
} driver_type_t;

// Forward declaration
typedef struct driver driver_t;

// MDF functions
void mdf_init(void);
int mdf_register_driver(const char *name, driver_type_t type,
                        int (*init)(void),
                        int (*probe)(void));
int mdf_unregister_driver(unsigned int driver_id);
int mdf_init_driver(unsigned int driver_id);
int mdf_start_driver(unsigned int driver_id);
int mdf_stop_driver(unsigned int driver_id);
driver_t *mdf_get_driver(unsigned int driver_id);
driver_t *mdf_list_drivers(void);

// Driver operations
int mdf_driver_open(unsigned int driver_id);
int mdf_driver_close(unsigned int driver_id);
int mdf_driver_read(unsigned int driver_id, void *buffer, unsigned int size);
int mdf_driver_write(unsigned int driver_id, const void *buffer, unsigned int size);
int mdf_driver_ioctl(unsigned int driver_id, unsigned int cmd, void *arg);

#endif // MDF_H
