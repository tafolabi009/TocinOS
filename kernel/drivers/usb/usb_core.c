/**
 * TocinOS USB Core Implementation
 * 
 * Core USB subsystem providing device enumeration, driver binding,
 * and transfer management.
 * 
 * @author TocinOS Team
 */

#include "../../../include/drivers/usb_core.h"
#include "../../../include/kernel/memory.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* External functions */
extern void serial_printf(const char *fmt, ...);
extern uint32_t timer_get_ticks(void);

/* ================================================================
 * GLOBAL STATE
 * ================================================================ */

static usb_controller_t *usb_controllers = NULL;
static usb_driver_t *usb_drivers = NULL;
static int usb_initialized = 0;

/* ================================================================
 * STRING UTILITIES
 * ================================================================ */

static void usb_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = '\0';
}

static int usb_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void usb_memset(void *dest, uint8_t val, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    while (n--) *d++ = val;
}

static void usb_memcpy(void *dest, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

/* ================================================================
 * MEMORY ALLOCATION
 * ================================================================ */

void *usb_alloc_buffer(uint32_t size) {
    /* Allocate aligned buffer for DMA */
    void *buf = kmalloc(size + 32);
    if (!buf) return NULL;
    
    /* Align to 16-byte boundary */
    uint32_t addr = (uint32_t)buf;
    uint32_t aligned = (addr + 15) & ~15;
    
    usb_memset((void *)aligned, 0, size);
    return (void *)aligned;
}

void usb_free_buffer(void *buffer) {
    if (buffer) {
        kfree(buffer);
    }
}

void *usb_phys_to_virt(uint32_t phys) {
    /* For now, assume identity mapping */
    return (void *)phys;
}

uint32_t usb_virt_to_phys(void *virt) {
    /* For now, assume identity mapping */
    return (uint32_t)virt;
}

/* ================================================================
 * CONTROLLER MANAGEMENT
 * ================================================================ */

int usb_register_controller(usb_controller_t *hc) {
    if (!hc || !hc->ops) {
        return -1;
    }
    
    serial_printf("[USB] Registering controller: %s\n", hc->name);
    
    /* Initialize controller state */
    hc->next_address = 1;
    hc->devices = NULL;
    
    /* Add to controller list */
    hc->next = usb_controllers;
    usb_controllers = hc;
    
    /* Start the controller */
    if (hc->ops->start) {
        int ret = hc->ops->start(hc);
        if (ret < 0) {
            serial_printf("[USB] Failed to start controller: %d\n", ret);
            return ret;
        }
    }
    
    return 0;
}

void usb_unregister_controller(usb_controller_t *hc) {
    if (!hc) return;
    
    /* Stop the controller */
    if (hc->ops && hc->ops->stop) {
        hc->ops->stop(hc);
    }
    
    /* Remove from list */
    if (usb_controllers == hc) {
        usb_controllers = hc->next;
    } else {
        for (usb_controller_t *c = usb_controllers; c; c = c->next) {
            if (c->next == hc) {
                c->next = hc->next;
                break;
            }
        }
    }
    
    serial_printf("[USB] Unregistered controller: %s\n", hc->name);
}

/* ================================================================
 * DRIVER MANAGEMENT
 * ================================================================ */

int usb_register_driver(usb_driver_t *driver) {
    if (!driver) {
        return -1;
    }
    
    serial_printf("[USB] Registering driver: %s\n", driver->name);
    
    driver->next = usb_drivers;
    usb_drivers = driver;
    
    return 0;
}

void usb_unregister_driver(usb_driver_t *driver) {
    if (!driver) return;
    
    if (usb_drivers == driver) {
        usb_drivers = driver->next;
    } else {
        for (usb_driver_t *d = usb_drivers; d; d = d->next) {
            if (d->next == driver) {
                d->next = driver->next;
                break;
            }
        }
    }
    
    serial_printf("[USB] Unregistered driver: %s\n", driver->name);
}

/**
 * Match driver against interface
 */
static int usb_driver_match(usb_driver_t *driver, usb_interface_t *interface,
                            usb_device_descriptor_t *desc) {
    /* Check vendor/product ID */
    if (driver->id_vendor != 0 && driver->id_vendor != desc->idVendor) {
        return 0;
    }
    if (driver->id_product != 0 && driver->id_product != desc->idProduct) {
        return 0;
    }
    
    /* Check class/subclass/protocol */
    if (driver->class_code != 0 && driver->class_code != interface->class_code) {
        return 0;
    }
    if (driver->subclass != 0 && driver->subclass != interface->subclass) {
        return 0;
    }
    if (driver->protocol != 0 && driver->protocol != interface->protocol) {
        return 0;
    }
    
    return 1;
}

/**
 * Find and bind driver for interface
 */
static void usb_bind_driver(usb_interface_t *interface) {
    usb_device_t *device = interface->device;
    
    for (usb_driver_t *driver = usb_drivers; driver; driver = driver->next) {
        if (usb_driver_match(driver, interface, &device->descriptor)) {
            serial_printf("[USB] Trying driver '%s' for interface %d\n",
                          driver->name, interface->number);
            
            if (driver->probe) {
                int ret = driver->probe(interface, &device->descriptor);
                if (ret == 0) {
                    interface->driver = driver;
                    serial_printf("[USB] Bound driver '%s' to interface %d\n",
                                  driver->name, interface->number);
                    return;
                }
            }
        }
    }
    
    serial_printf("[USB] No driver found for interface %d (class=%02X/%02X/%02X)\n",
                  interface->number, interface->class_code,
                  interface->subclass, interface->protocol);
}

/* ================================================================
 * CONTROL TRANSFERS
 * ================================================================ */

int usb_control_transfer(usb_device_t *device,
                         uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index,
                         void *data, uint16_t length) {
    if (!device || !device->controller) {
        return -1;
    }
    
    usb_controller_t *hc = device->controller;
    if (!hc->ops || !hc->ops->control_transfer) {
        return -1;
    }
    
    /* Set up request */
    usb_device_request_t setup;
    setup.bmRequestType = request_type;
    setup.bRequest = request;
    setup.wValue = value;
    setup.wIndex = index;
    setup.wLength = length;
    
    /* Create transfer */
    usb_transfer_t transfer;
    usb_memset(&transfer, 0, sizeof(transfer));
    transfer.device = device;
    transfer.endpoint = &device->ep0;
    transfer.type = USB_EP_TYPE_CONTROL;
    transfer.direction = (request_type & USB_DIR_IN) ? USB_DIR_IN : USB_DIR_OUT;
    transfer.setup = &setup;
    transfer.buffer = data;
    transfer.length = length;
    
    /* Execute transfer */
    int ret = hc->ops->control_transfer(hc, &transfer);
    
    if (ret == 0) {
        return transfer.actual_length;
    }
    
    return ret;
}

int usb_bulk_transfer(usb_device_t *device, usb_endpoint_t *endpoint,
                      void *data, uint32_t length, uint32_t *actual) {
    if (!device || !device->controller || !endpoint) {
        return -1;
    }
    
    usb_controller_t *hc = device->controller;
    if (!hc->ops || !hc->ops->bulk_transfer) {
        return -1;
    }
    
    /* Create transfer */
    usb_transfer_t transfer;
    usb_memset(&transfer, 0, sizeof(transfer));
    transfer.device = device;
    transfer.endpoint = endpoint;
    transfer.type = USB_EP_TYPE_BULK;
    transfer.direction = endpoint->direction;
    transfer.buffer = data;
    transfer.length = length;
    
    /* Execute transfer */
    int ret = hc->ops->bulk_transfer(hc, &transfer);
    
    if (actual) {
        *actual = transfer.actual_length;
    }
    
    return ret;
}

int usb_interrupt_transfer(usb_device_t *device, usb_endpoint_t *endpoint,
                           void *data, uint32_t length, uint32_t *actual) {
    if (!device || !device->controller || !endpoint) {
        return -1;
    }
    
    usb_controller_t *hc = device->controller;
    if (!hc->ops || !hc->ops->interrupt_transfer) {
        return -1;
    }
    
    /* Create transfer */
    usb_transfer_t transfer;
    usb_memset(&transfer, 0, sizeof(transfer));
    transfer.device = device;
    transfer.endpoint = endpoint;
    transfer.type = USB_EP_TYPE_INTERRUPT;
    transfer.direction = endpoint->direction;
    transfer.buffer = data;
    transfer.length = length;
    
    /* Execute transfer */
    int ret = hc->ops->interrupt_transfer(hc, &transfer);
    
    if (actual) {
        *actual = transfer.actual_length;
    }
    
    return ret;
}

/* ================================================================
 * STANDARD REQUESTS
 * ================================================================ */

int usb_get_device_descriptor(usb_device_t *device, usb_device_descriptor_t *desc) {
    return usb_control_transfer(device,
        USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        USB_REQ_GET_DESCRIPTOR,
        (USB_DESC_DEVICE << 8) | 0,
        0,
        desc, sizeof(usb_device_descriptor_t));
}

int usb_get_config_descriptor(usb_device_t *device, uint8_t index, 
                               void *buffer, uint16_t length) {
    return usb_control_transfer(device,
        USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        USB_REQ_GET_DESCRIPTOR,
        (USB_DESC_CONFIGURATION << 8) | index,
        0,
        buffer, length);
}

int usb_set_configuration(usb_device_t *device, uint8_t config) {
    int ret = usb_control_transfer(device,
        USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        USB_REQ_SET_CONFIGURATION,
        config,
        0,
        NULL, 0);
    
    if (ret >= 0) {
        device->state = USB_STATE_CONFIGURED;
    }
    
    return ret;
}

int usb_set_address(usb_device_t *device, uint8_t address) {
    int ret = usb_control_transfer(device,
        USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        USB_REQ_SET_ADDRESS,
        address,
        0,
        NULL, 0);
    
    if (ret >= 0) {
        /* Small delay for device to change address */
        for (volatile int i = 0; i < 100000; i++);
        
        device->address = address;
        device->state = USB_STATE_ADDRESS;
    }
    
    return ret;
}

int usb_get_string(usb_device_t *device, uint8_t index, char *buffer, int length) {
    if (index == 0) {
        buffer[0] = '\0';
        return 0;
    }
    
    uint8_t raw[256];
    
    /* Get string descriptor */
    int ret = usb_control_transfer(device,
        USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        USB_REQ_GET_DESCRIPTOR,
        (USB_DESC_STRING << 8) | index,
        0x0409,  /* English (US) */
        raw, 256);
    
    if (ret < 0 || ret < 2) {
        buffer[0] = '\0';
        return ret;
    }
    
    /* Convert from UTF-16LE to ASCII */
    int str_len = (raw[0] - 2) / 2;
    if (str_len > length - 1) {
        str_len = length - 1;
    }
    
    for (int i = 0; i < str_len; i++) {
        buffer[i] = raw[2 + i * 2];  /* Low byte of UTF-16 */
    }
    buffer[str_len] = '\0';
    
    return str_len;
}

int usb_set_interface(usb_device_t *device, uint8_t interface, uint8_t alternate) {
    return usb_control_transfer(device,
        USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE,
        USB_REQ_SET_INTERFACE,
        alternate,
        interface,
        NULL, 0);
}

int usb_clear_halt(usb_device_t *device, usb_endpoint_t *endpoint) {
    int ret = usb_control_transfer(device,
        USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT,
        USB_REQ_CLEAR_FEATURE,
        0,  /* ENDPOINT_HALT feature */
        endpoint->address | endpoint->direction,
        NULL, 0);
    
    if (ret >= 0) {
        endpoint->toggle = 0;
    }
    
    return ret;
}

usb_endpoint_t *usb_find_endpoint(usb_interface_t *interface, uint8_t type, uint8_t direction) {
    for (int i = 0; i < interface->num_endpoints; i++) {
        usb_endpoint_t *ep = &interface->endpoints[i];
        if (ep->type == type && ep->direction == direction) {
            return ep;
        }
    }
    return NULL;
}

/* ================================================================
 * DEVICE ENUMERATION
 * ================================================================ */

/**
 * Parse configuration descriptor and its sub-descriptors
 */
static int usb_parse_config(usb_device_t *device, void *config_data, uint16_t total_length) {
    uint8_t *ptr = (uint8_t *)config_data;
    uint8_t *end = ptr + total_length;
    
    usb_interface_t *current_interface = NULL;
    
    while (ptr < end) {
        uint8_t desc_len = ptr[0];
        uint8_t desc_type = ptr[1];
        
        if (desc_len == 0) break;
        
        switch (desc_type) {
            case USB_DESC_CONFIGURATION: {
                usb_config_descriptor_t *cfg = (usb_config_descriptor_t *)ptr;
                device->config = kmalloc(sizeof(usb_config_descriptor_t));
                if (device->config) {
                    usb_memcpy(device->config, cfg, sizeof(usb_config_descriptor_t));
                }
                break;
            }
            
            case USB_DESC_INTERFACE: {
                usb_interface_descriptor_t *iface = (usb_interface_descriptor_t *)ptr;
                
                if (device->num_interfaces < USB_MAX_INTERFACES) {
                    current_interface = &device->interfaces[device->num_interfaces];
                    device->num_interfaces++;
                    
                    current_interface->number = iface->bInterfaceNumber;
                    current_interface->alternate = iface->bAlternateSetting;
                    current_interface->class_code = iface->bInterfaceClass;
                    current_interface->subclass = iface->bInterfaceSubClass;
                    current_interface->protocol = iface->bInterfaceProtocol;
                    current_interface->num_endpoints = 0;
                    current_interface->device = device;
                    current_interface->driver = NULL;
                }
                break;
            }
            
            case USB_DESC_ENDPOINT: {
                usb_endpoint_descriptor_t *ep_desc = (usb_endpoint_descriptor_t *)ptr;
                
                if (current_interface && 
                    current_interface->num_endpoints < USB_MAX_ENDPOINTS) {
                    usb_endpoint_t *ep = &current_interface->endpoints[current_interface->num_endpoints];
                    current_interface->num_endpoints++;
                    
                    ep->address = ep_desc->bEndpointAddress & 0x0F;
                    ep->direction = ep_desc->bEndpointAddress & 0x80;
                    ep->type = ep_desc->bmAttributes & 0x03;
                    ep->max_packet_size = ep_desc->wMaxPacketSize;
                    ep->interval = ep_desc->bInterval;
                    ep->toggle = 0;
                    ep->interface = current_interface;
                }
                break;
            }
        }
        
        ptr += desc_len;
    }
    
    return 0;
}

/**
 * Enumerate a newly attached device
 */
static usb_device_t *usb_enumerate_device(usb_controller_t *hc, int port, uint8_t speed) {
    serial_printf("[USB] Enumerating device on port %d\n", port);
    
    /* Allocate device structure */
    usb_device_t *device = kmalloc(sizeof(usb_device_t));
    if (!device) {
        serial_printf("[USB] Failed to allocate device structure\n");
        return NULL;
    }
    
    usb_memset(device, 0, sizeof(usb_device_t));
    device->controller = hc;
    device->port = port;
    device->speed = speed;
    device->address = 0;  /* Default address */
    device->state = USB_STATE_DEFAULT;
    
    /* Initialize EP0 */
    device->ep0.address = 0;
    device->ep0.type = USB_EP_TYPE_CONTROL;
    device->ep0.direction = 0;
    device->ep0.max_packet_size = 8;  /* Start with 8, will update */
    device->ep0.toggle = 0;
    
    /* Get first 8 bytes of device descriptor to learn max packet size */
    int ret = usb_get_device_descriptor(device, &device->descriptor);
    if (ret < 8) {
        serial_printf("[USB] Failed to get device descriptor: %d\n", ret);
        kfree(device);
        return NULL;
    }
    
    /* Update EP0 max packet size */
    device->ep0.max_packet_size = device->descriptor.bMaxPacketSize0;
    
    /* Reset device again (some devices need this) */
    if (hc->ops->set_port_feature) {
        hc->ops->set_port_feature(hc, port, USB_HUB_FEAT_PORT_RESET);
        for (volatile int i = 0; i < 500000; i++);
    }
    
    /* Assign address */
    uint8_t new_address = hc->next_address++;
    if (new_address > 127) {
        serial_printf("[USB] No more addresses available\n");
        kfree(device);
        return NULL;
    }
    
    ret = usb_set_address(device, new_address);
    if (ret < 0) {
        serial_printf("[USB] Failed to set address: %d\n", ret);
        kfree(device);
        return NULL;
    }
    
    serial_printf("[USB] Device assigned address %d\n", new_address);
    
    /* Get full device descriptor */
    ret = usb_get_device_descriptor(device, &device->descriptor);
    if (ret < 0) {
        serial_printf("[USB] Failed to get full device descriptor: %d\n", ret);
        kfree(device);
        return NULL;
    }
    
    /* Get strings */
    usb_get_string(device, device->descriptor.iManufacturer, device->manufacturer, 64);
    usb_get_string(device, device->descriptor.iProduct, device->product, 64);
    usb_get_string(device, device->descriptor.iSerialNumber, device->serial, 32);
    
    serial_printf("[USB] Device: %s %s\n", device->manufacturer, device->product);
    serial_printf("[USB] VID=%04X PID=%04X Class=%02X\n",
                  device->descriptor.idVendor,
                  device->descriptor.idProduct,
                  device->descriptor.bDeviceClass);
    
    /* Get configuration descriptor */
    uint8_t config_buf[256];
    ret = usb_get_config_descriptor(device, 0, config_buf, 256);
    if (ret > 0) {
        usb_parse_config(device, config_buf, ret);
        
        /* Set configuration */
        if (device->config) {
            usb_set_configuration(device, device->config->bConfigurationValue);
        }
    }
    
    /* Add to device list */
    device->next = hc->devices;
    hc->devices = device;
    
    /* Bind drivers to interfaces */
    for (int i = 0; i < device->num_interfaces; i++) {
        usb_bind_driver(&device->interfaces[i]);
    }
    
    return device;
}

/**
 * Handle port status change
 */
static void usb_handle_port_change(usb_controller_t *hc, int port) {
    if (!hc->ops || !hc->ops->get_port_status) return;
    
    uint16_t status;
    if (hc->ops->get_port_status(hc, port, &status) < 0) {
        return;
    }
    
    serial_printf("[USB] Port %d status: %04X\n", port, status);
    
    if (status & USB_PORT_CONNECTED) {
        /* Device connected */
        uint8_t speed = USB_SPEED_FULL;
        if (status & USB_PORT_LOW_SPEED) {
            speed = USB_SPEED_LOW;
        } else if (status & USB_PORT_HIGH_SPEED) {
            speed = USB_SPEED_HIGH;
        }
        
        /* Reset port */
        if (hc->ops->set_port_feature) {
            hc->ops->set_port_feature(hc, port, USB_HUB_FEAT_PORT_RESET);
            
            /* Wait for reset to complete */
            for (volatile int i = 0; i < 500000; i++);
            
            if (hc->ops->clear_port_feature) {
                hc->ops->clear_port_feature(hc, port, USB_HUB_FEAT_C_PORT_RESET);
            }
        }
        
        /* Enumerate device */
        usb_enumerate_device(hc, port, speed);
    } else {
        /* Device disconnected */
        serial_printf("[USB] Device disconnected from port %d\n", port);
        
        /* Find and remove device */
        usb_device_t **pp = &hc->devices;
        while (*pp) {
            if ((*pp)->port == port) {
                usb_device_t *dev = *pp;
                *pp = dev->next;
                
                /* Notify drivers */
                for (int i = 0; i < dev->num_interfaces; i++) {
                    if (dev->interfaces[i].driver && 
                        dev->interfaces[i].driver->disconnect) {
                        dev->interfaces[i].driver->disconnect(&dev->interfaces[i]);
                    }
                }
                
                if (dev->config) kfree(dev->config);
                kfree(dev);
                break;
            }
            pp = &(*pp)->next;
        }
    }
}

void usb_enumerate(void) {
    serial_printf("[USB] Enumerating devices...\n");
    
    for (usb_controller_t *hc = usb_controllers; hc; hc = hc->next) {
        if (!hc->ops) continue;
        
        for (int port = 0; port < hc->num_ports; port++) {
            usb_handle_port_change(hc, port);
        }
    }
}

int usb_port_reset(usb_controller_t *hc, int port) {
    if (!hc || !hc->ops) return -1;
    
    if (hc->ops->set_port_feature) {
        hc->ops->set_port_feature(hc, port, USB_HUB_FEAT_PORT_RESET);
        
        /* Wait for reset */
        for (volatile int i = 0; i < 500000; i++);
        
        if (hc->ops->clear_port_feature) {
            hc->ops->clear_port_feature(hc, port, USB_HUB_FEAT_C_PORT_RESET);
        }
    }
    
    return 0;
}

/* ================================================================
 * DEBUG
 * ================================================================ */

void usb_print_device(usb_device_t *device) {
    if (!device) return;
    
    serial_printf("USB Device at address %d:\n", device->address);
    serial_printf("  Manufacturer: %s\n", device->manufacturer);
    serial_printf("  Product: %s\n", device->product);
    serial_printf("  Serial: %s\n", device->serial);
    serial_printf("  VID:PID = %04X:%04X\n", 
                  device->descriptor.idVendor,
                  device->descriptor.idProduct);
    serial_printf("  Class: %02X/%02X/%02X\n",
                  device->descriptor.bDeviceClass,
                  device->descriptor.bDeviceSubClass,
                  device->descriptor.bDeviceProtocol);
    serial_printf("  Interfaces: %d\n", device->num_interfaces);
    
    for (int i = 0; i < device->num_interfaces; i++) {
        usb_interface_t *iface = &device->interfaces[i];
        serial_printf("    Interface %d: Class %02X/%02X/%02X, %d endpoints\n",
                      iface->number, iface->class_code, iface->subclass,
                      iface->protocol, iface->num_endpoints);
        
        if (iface->driver) {
            serial_printf("      Driver: %s\n", iface->driver->name);
        }
    }
}

/* ================================================================
 * INITIALIZATION
 * ================================================================ */

int usb_init(void) {
    if (usb_initialized) {
        return 0;
    }
    
    serial_printf("[USB] Initializing USB subsystem...\n");
    
    usb_controllers = NULL;
    usb_drivers = NULL;
    
    usb_initialized = 1;
    
    serial_printf("[USB] USB subsystem initialized\n");
    return 0;
}
