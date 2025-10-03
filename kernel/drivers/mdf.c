/**
 * TocinOS Modular Driver Framework (MDF)
 * 
 * Extensible driver architecture for device management
 */

#include "../include/drivers/mdf.h"

#define MAX_DRIVERS 32

typedef enum {
    DRIVER_STATE_UNINITIALIZED,
    DRIVER_STATE_INITIALIZED,
    DRIVER_STATE_RUNNING,
    DRIVER_STATE_SUSPENDED,
    DRIVER_STATE_ERROR
} driver_state_t;

typedef struct driver {
    unsigned int id;
    char name[32];
    driver_type_t type;
    driver_state_t state;
    
    // Driver operations
    int (*init)(void);
    int (*probe)(void);
    int (*remove)(void);
    int (*open)(void);
    int (*close)(void);
    int (*read)(void *buffer, unsigned int size);
    int (*write)(const void *buffer, unsigned int size);
    int (*ioctl)(unsigned int cmd, void *arg);
    
    void *private_data;
    struct driver *next;
} driver_t;

static driver_t drivers[MAX_DRIVERS];
static driver_t *driver_list = 0;
static unsigned int next_driver_id = 0;

/**
 * Initialize the MDF framework
 */
void mdf_init(void) {
    for (int i = 0; i < MAX_DRIVERS; i++) {
        drivers[i].state = DRIVER_STATE_UNINITIALIZED;
        drivers[i].next = 0;
    }
    driver_list = 0;
    next_driver_id = 0;
}

/**
 * Register a driver
 */
int mdf_register_driver(const char *name, driver_type_t type,
                        int (*init)(void),
                        int (*probe)(void)) {
    if (next_driver_id >= MAX_DRIVERS) {
        return -1; // No driver slots available
    }
    
    driver_t *driver = &drivers[next_driver_id];
    driver->id = next_driver_id++;
    driver->type = type;
    driver->state = DRIVER_STATE_UNINITIALIZED;
    
    // Copy name
    int i = 0;
    while (name[i] && i < 31) {
        driver->name[i] = name[i];
        i++;
    }
    driver->name[i] = '\0';
    
    // Set operations
    driver->init = init;
    driver->probe = probe;
    
    // Add to driver list
    driver->next = driver_list;
    driver_list = driver;
    
    return driver->id;
}

/**
 * Unregister a driver
 */
int mdf_unregister_driver(unsigned int driver_id) {
    if (driver_id >= MAX_DRIVERS) {
        return -1;
    }
    
    driver_t *driver = &drivers[driver_id];
    if (driver->state != DRIVER_STATE_UNINITIALIZED) {
        if (driver->remove) {
            driver->remove();
        }
        driver->state = DRIVER_STATE_UNINITIALIZED;
    }
    
    return 0;
}

/**
 * Initialize a driver
 */
int mdf_init_driver(unsigned int driver_id) {
    if (driver_id >= MAX_DRIVERS) {
        return -1;
    }
    
    driver_t *driver = &drivers[driver_id];
    if (driver->state != DRIVER_STATE_UNINITIALIZED) {
        return -1; // Already initialized
    }
    
    // Call driver init
    if (driver->init) {
        int result = driver->init();
        if (result != 0) {
            driver->state = DRIVER_STATE_ERROR;
            return result;
        }
    }
    
    // Call driver probe
    if (driver->probe) {
        int result = driver->probe();
        if (result != 0) {
            driver->state = DRIVER_STATE_ERROR;
            return result;
        }
    }
    
    driver->state = DRIVER_STATE_INITIALIZED;
    return 0;
}

/**
 * Start a driver
 */
int mdf_start_driver(unsigned int driver_id) {
    if (driver_id >= MAX_DRIVERS) {
        return -1;
    }
    
    driver_t *driver = &drivers[driver_id];
    if (driver->state != DRIVER_STATE_INITIALIZED) {
        return -1;
    }
    
    driver->state = DRIVER_STATE_RUNNING;
    return 0;
}

/**
 * Stop a driver
 */
int mdf_stop_driver(unsigned int driver_id) {
    if (driver_id >= MAX_DRIVERS) {
        return -1;
    }
    
    driver_t *driver = &drivers[driver_id];
    if (driver->state == DRIVER_STATE_RUNNING) {
        driver->state = DRIVER_STATE_SUSPENDED;
    }
    
    return 0;
}

/**
 * Get driver by ID
 */
driver_t *mdf_get_driver(unsigned int driver_id) {
    if (driver_id >= MAX_DRIVERS) {
        return 0;
    }
    return &drivers[driver_id];
}

/**
 * List all registered drivers
 */
driver_t *mdf_list_drivers(void) {
    return driver_list;
}

/**
 * Driver open operation
 */
int mdf_driver_open(unsigned int driver_id) {
    driver_t *driver = mdf_get_driver(driver_id);
    if (driver && driver->open) {
        return driver->open();
    }
    return -1;
}

/**
 * Driver close operation
 */
int mdf_driver_close(unsigned int driver_id) {
    driver_t *driver = mdf_get_driver(driver_id);
    if (driver && driver->close) {
        return driver->close();
    }
    return -1;
}

/**
 * Driver read operation
 */
int mdf_driver_read(unsigned int driver_id, void *buffer, unsigned int size) {
    driver_t *driver = mdf_get_driver(driver_id);
    if (driver && driver->read) {
        return driver->read(buffer, size);
    }
    return -1;
}

/**
 * Driver write operation
 */
int mdf_driver_write(unsigned int driver_id, const void *buffer, unsigned int size) {
    driver_t *driver = mdf_get_driver(driver_id);
    if (driver && driver->write) {
        return driver->write(buffer, size);
    }
    return -1;
}

/**
 * Driver ioctl operation
 */
int mdf_driver_ioctl(unsigned int driver_id, unsigned int cmd, void *arg) {
    driver_t *driver = mdf_get_driver(driver_id);
    if (driver && driver->ioctl) {
        return driver->ioctl(cmd, arg);
    }
    return -1;
}
