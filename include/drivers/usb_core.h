/**
 * TocinOS USB Core Header
 * 
 * Universal Serial Bus core infrastructure definitions.
 * Supports USB 1.1 (UHCI/OHCI), USB 2.0 (EHCI), and USB 3.0 (xHCI).
 * 
 * @author TocinOS Team
 */

#ifndef _TOCINOS_USB_CORE_H
#define _TOCINOS_USB_CORE_H

#include "../stdint.h"

/* ================================================================
 * USB CONSTANTS
 * ================================================================ */

/* USB Speeds */
#define USB_SPEED_LOW           0       /* 1.5 Mbps (USB 1.0) */
#define USB_SPEED_FULL          1       /* 12 Mbps (USB 1.1) */
#define USB_SPEED_HIGH          2       /* 480 Mbps (USB 2.0) */
#define USB_SPEED_SUPER         3       /* 5 Gbps (USB 3.0) */
#define USB_SPEED_SUPER_PLUS    4       /* 10 Gbps (USB 3.1) */

/* USB Directions */
#define USB_DIR_OUT             0x00    /* Host to device */
#define USB_DIR_IN              0x80    /* Device to host */

/* USB Request Types */
#define USB_TYPE_STANDARD       0x00
#define USB_TYPE_CLASS          0x20
#define USB_TYPE_VENDOR         0x40
#define USB_TYPE_RESERVED       0x60

/* USB Recipients */
#define USB_RECIP_DEVICE        0x00
#define USB_RECIP_INTERFACE     0x01
#define USB_RECIP_ENDPOINT      0x02
#define USB_RECIP_OTHER         0x03

/* Standard USB Requests */
#define USB_REQ_GET_STATUS          0x00
#define USB_REQ_CLEAR_FEATURE       0x01
#define USB_REQ_SET_FEATURE         0x03
#define USB_REQ_SET_ADDRESS         0x05
#define USB_REQ_GET_DESCRIPTOR      0x06
#define USB_REQ_SET_DESCRIPTOR      0x07
#define USB_REQ_GET_CONFIGURATION   0x08
#define USB_REQ_SET_CONFIGURATION   0x09
#define USB_REQ_GET_INTERFACE       0x0A
#define USB_REQ_SET_INTERFACE       0x0B
#define USB_REQ_SYNCH_FRAME         0x0C

/* Descriptor Types */
#define USB_DESC_DEVICE             0x01
#define USB_DESC_CONFIGURATION      0x02
#define USB_DESC_STRING             0x03
#define USB_DESC_INTERFACE          0x04
#define USB_DESC_ENDPOINT           0x05
#define USB_DESC_DEVICE_QUALIFIER   0x06
#define USB_DESC_OTHER_SPEED        0x07
#define USB_DESC_INTERFACE_POWER    0x08
#define USB_DESC_HID                0x21
#define USB_DESC_HID_REPORT         0x22

/* Endpoint Types */
#define USB_EP_TYPE_CONTROL         0x00
#define USB_EP_TYPE_ISOCHRONOUS     0x01
#define USB_EP_TYPE_BULK            0x02
#define USB_EP_TYPE_INTERRUPT       0x03

/* Device Classes */
#define USB_CLASS_PER_INTERFACE     0x00
#define USB_CLASS_AUDIO             0x01
#define USB_CLASS_COMM              0x02
#define USB_CLASS_HID               0x03
#define USB_CLASS_PHYSICAL          0x05
#define USB_CLASS_IMAGE             0x06
#define USB_CLASS_PRINTER           0x07
#define USB_CLASS_MASS_STORAGE      0x08
#define USB_CLASS_HUB               0x09
#define USB_CLASS_CDC_DATA          0x0A
#define USB_CLASS_SMART_CARD        0x0B
#define USB_CLASS_VIDEO             0x0E
#define USB_CLASS_HEALTHCARE        0x0F
#define USB_CLASS_DIAGNOSTIC        0xDC
#define USB_CLASS_WIRELESS          0xE0
#define USB_CLASS_VENDOR_SPEC       0xFF

/* HID Subclasses */
#define USB_HID_SUBCLASS_NONE       0x00
#define USB_HID_SUBCLASS_BOOT       0x01

/* HID Protocols */
#define USB_HID_PROTOCOL_NONE       0x00
#define USB_HID_PROTOCOL_KEYBOARD   0x01
#define USB_HID_PROTOCOL_MOUSE      0x02

/* Mass Storage Subclasses */
#define USB_MSC_SUBCLASS_SCSI       0x06

/* Mass Storage Protocols */
#define USB_MSC_PROTOCOL_BBB        0x50    /* Bulk-Only Transport */

/* Port Status Bits */
#define USB_PORT_CONNECTED          0x0001
#define USB_PORT_ENABLED            0x0002
#define USB_PORT_SUSPENDED          0x0004
#define USB_PORT_OVERCURRENT        0x0008
#define USB_PORT_RESET              0x0010
#define USB_PORT_POWER              0x0100
#define USB_PORT_LOW_SPEED          0x0200
#define USB_PORT_HIGH_SPEED         0x0400

/* Hub Features */
#define USB_HUB_FEAT_PORT_RESET     4
#define USB_HUB_FEAT_PORT_POWER     8
#define USB_HUB_FEAT_C_PORT_CONNECT 16
#define USB_HUB_FEAT_C_PORT_RESET   20

/* Maximum limits */
#define USB_MAX_DEVICES             127
#define USB_MAX_ENDPOINTS           16
#define USB_MAX_INTERFACES          8
#define USB_MAX_CONFIGURATIONS      4
#define USB_MAX_CONTROLLERS         8

/* ================================================================
 * USB DESCRIPTOR STRUCTURES
 * ================================================================ */

/**
 * USB Device Descriptor (18 bytes)
 */
typedef struct usb_device_descriptor {
    uint8_t  bLength;               /* Size of this descriptor */
    uint8_t  bDescriptorType;       /* DEVICE descriptor type */
    uint16_t bcdUSB;                /* USB spec version (BCD) */
    uint8_t  bDeviceClass;          /* Class code */
    uint8_t  bDeviceSubClass;       /* Subclass code */
    uint8_t  bDeviceProtocol;       /* Protocol code */
    uint8_t  bMaxPacketSize0;       /* Max packet size for EP0 */
    uint16_t idVendor;              /* Vendor ID */
    uint16_t idProduct;             /* Product ID */
    uint16_t bcdDevice;             /* Device version (BCD) */
    uint8_t  iManufacturer;         /* Index of manufacturer string */
    uint8_t  iProduct;              /* Index of product string */
    uint8_t  iSerialNumber;         /* Index of serial number string */
    uint8_t  bNumConfigurations;    /* Number of configurations */
} __attribute__((packed)) usb_device_descriptor_t;

/**
 * USB Configuration Descriptor (9 bytes)
 */
typedef struct usb_config_descriptor {
    uint8_t  bLength;               /* Size of this descriptor */
    uint8_t  bDescriptorType;       /* CONFIGURATION descriptor type */
    uint16_t wTotalLength;          /* Total length of config + interfaces */
    uint8_t  bNumInterfaces;        /* Number of interfaces */
    uint8_t  bConfigurationValue;   /* Value for SET_CONFIGURATION */
    uint8_t  iConfiguration;        /* Index of configuration string */
    uint8_t  bmAttributes;          /* Configuration attributes */
    uint8_t  bMaxPower;             /* Max power (2mA units) */
} __attribute__((packed)) usb_config_descriptor_t;

/**
 * USB Interface Descriptor (9 bytes)
 */
typedef struct usb_interface_descriptor {
    uint8_t  bLength;               /* Size of this descriptor */
    uint8_t  bDescriptorType;       /* INTERFACE descriptor type */
    uint8_t  bInterfaceNumber;      /* Interface number */
    uint8_t  bAlternateSetting;     /* Alternate setting value */
    uint8_t  bNumEndpoints;         /* Number of endpoints */
    uint8_t  bInterfaceClass;       /* Class code */
    uint8_t  bInterfaceSubClass;    /* Subclass code */
    uint8_t  bInterfaceProtocol;    /* Protocol code */
    uint8_t  iInterface;            /* Index of interface string */
} __attribute__((packed)) usb_interface_descriptor_t;

/**
 * USB Endpoint Descriptor (7 bytes)
 */
typedef struct usb_endpoint_descriptor {
    uint8_t  bLength;               /* Size of this descriptor */
    uint8_t  bDescriptorType;       /* ENDPOINT descriptor type */
    uint8_t  bEndpointAddress;      /* Endpoint address (with direction) */
    uint8_t  bmAttributes;          /* Endpoint attributes */
    uint16_t wMaxPacketSize;        /* Max packet size */
    uint8_t  bInterval;             /* Polling interval (ms) */
} __attribute__((packed)) usb_endpoint_descriptor_t;

/**
 * USB String Descriptor Header
 */
typedef struct usb_string_descriptor {
    uint8_t  bLength;               /* Size of this descriptor */
    uint8_t  bDescriptorType;       /* STRING descriptor type */
    uint16_t wString[1];            /* Unicode string (variable length) */
} __attribute__((packed)) usb_string_descriptor_t;

/**
 * USB HID Descriptor
 */
typedef struct usb_hid_descriptor {
    uint8_t  bLength;               /* Size of this descriptor */
    uint8_t  bDescriptorType;       /* HID descriptor type */
    uint16_t bcdHID;                /* HID spec version */
    uint8_t  bCountryCode;          /* Country code */
    uint8_t  bNumDescriptors;       /* Number of HID descriptors */
    uint8_t  bReportType;           /* Type of report descriptor */
    uint16_t wReportLength;         /* Length of report descriptor */
} __attribute__((packed)) usb_hid_descriptor_t;

/**
 * USB Device Request (Setup Packet) - 8 bytes
 */
typedef struct usb_device_request {
    uint8_t  bmRequestType;         /* Request type */
    uint8_t  bRequest;              /* Specific request */
    uint16_t wValue;                /* Request-specific value */
    uint16_t wIndex;                /* Request-specific index */
    uint16_t wLength;               /* Number of bytes to transfer */
} __attribute__((packed)) usb_device_request_t;

/* ================================================================
 * USB CORE STRUCTURES
 * ================================================================ */

/* Forward declarations */
struct usb_device;
struct usb_endpoint;
struct usb_interface;
struct usb_controller;
struct usb_driver;

/**
 * USB Endpoint
 */
typedef struct usb_endpoint {
    uint8_t address;                /* Endpoint address */
    uint8_t type;                   /* Endpoint type */
    uint8_t direction;              /* IN or OUT */
    uint16_t max_packet_size;       /* Maximum packet size */
    uint8_t interval;               /* Polling interval */
    uint8_t toggle;                 /* Data toggle bit */
    
    struct usb_interface *interface; /* Parent interface */
} usb_endpoint_t;

/**
 * USB Interface
 */
typedef struct usb_interface {
    uint8_t number;                 /* Interface number */
    uint8_t alternate;              /* Alternate setting */
    uint8_t class_code;             /* Class code */
    uint8_t subclass;               /* Subclass code */
    uint8_t protocol;               /* Protocol code */
    
    usb_endpoint_t endpoints[USB_MAX_ENDPOINTS];
    uint8_t num_endpoints;
    
    struct usb_driver *driver;      /* Bound driver */
    void *driver_data;              /* Driver private data */
    
    struct usb_device *device;      /* Parent device */
} usb_interface_t;

/**
 * USB Device
 */
typedef struct usb_device {
    uint8_t address;                /* Device address (1-127) */
    uint8_t speed;                  /* Device speed */
    uint8_t port;                   /* Hub port number */
    uint8_t depth;                  /* Hub depth (0 = root hub) */
    
    usb_device_descriptor_t descriptor;
    usb_config_descriptor_t *config;
    
    usb_interface_t interfaces[USB_MAX_INTERFACES];
    uint8_t num_interfaces;
    
    usb_endpoint_t ep0;             /* Control endpoint */
    
    struct usb_device *parent;      /* Parent hub (NULL for root hub) */
    struct usb_controller *controller; /* Host controller */
    
    uint8_t state;                  /* Device state */
    char product[64];               /* Product string */
    char manufacturer[64];          /* Manufacturer string */
    char serial[32];                /* Serial number string */
    
    struct usb_device *next;        /* Next in device list */
} usb_device_t;

/* Device states */
#define USB_STATE_DETACHED      0
#define USB_STATE_ATTACHED      1
#define USB_STATE_POWERED       2
#define USB_STATE_DEFAULT       3
#define USB_STATE_ADDRESS       4
#define USB_STATE_CONFIGURED    5
#define USB_STATE_SUSPENDED     6

/**
 * USB Transfer Request
 */
typedef struct usb_transfer {
    usb_device_t *device;           /* Target device */
    usb_endpoint_t *endpoint;       /* Target endpoint */
    
    uint8_t type;                   /* Transfer type */
    uint8_t direction;              /* Transfer direction */
    
    usb_device_request_t *setup;    /* Setup packet (for control) */
    void *buffer;                   /* Data buffer */
    uint32_t length;                /* Buffer length */
    uint32_t actual_length;         /* Actual transferred bytes */
    
    int status;                     /* Transfer status */
    int complete;                   /* Transfer complete flag */
    
    void (*callback)(struct usb_transfer *); /* Completion callback */
    void *context;                  /* Callback context */
    
    struct usb_transfer *next;      /* Queue link */
} usb_transfer_t;

/* Transfer status codes */
#define USB_STATUS_SUCCESS      0
#define USB_STATUS_STALL        -1
#define USB_STATUS_NAK          -2
#define USB_STATUS_TIMEOUT      -3
#define USB_STATUS_CRC_ERROR    -4
#define USB_STATUS_BIT_STUFF    -5
#define USB_STATUS_OVERFLOW     -6
#define USB_STATUS_UNDERRUN     -7
#define USB_STATUS_CANCELLED    -8
#define USB_STATUS_NO_DEVICE    -9

/**
 * USB Host Controller Operations
 */
typedef struct usb_hc_ops {
    /* Controller lifecycle */
    int (*start)(struct usb_controller *hc);
    int (*stop)(struct usb_controller *hc);
    int (*reset)(struct usb_controller *hc);
    
    /* Port operations */
    int (*get_port_status)(struct usb_controller *hc, int port, uint16_t *status);
    int (*set_port_feature)(struct usb_controller *hc, int port, int feature);
    int (*clear_port_feature)(struct usb_controller *hc, int port, int feature);
    
    /* Transfer operations */
    int (*control_transfer)(struct usb_controller *hc, usb_transfer_t *transfer);
    int (*bulk_transfer)(struct usb_controller *hc, usb_transfer_t *transfer);
    int (*interrupt_transfer)(struct usb_controller *hc, usb_transfer_t *transfer);
    int (*isochronous_transfer)(struct usb_controller *hc, usb_transfer_t *transfer);
    
    /* Interrupt handling */
    void (*irq_handler)(struct usb_controller *hc);
} usb_hc_ops_t;

/**
 * USB Host Controller
 */
typedef struct usb_controller {
    char name[32];                  /* Controller name */
    uint8_t type;                   /* Controller type */
    uint8_t irq;                    /* IRQ number */
    
    uint32_t io_base;               /* I/O base address */
    void *mmio_base;                /* MMIO base address */
    
    uint8_t num_ports;              /* Number of root hub ports */
    uint8_t next_address;           /* Next device address to assign */
    
    usb_hc_ops_t *ops;              /* Controller operations */
    void *hc_data;                  /* Controller-specific data */
    
    usb_device_t *devices;          /* List of connected devices */
    
    struct usb_controller *next;    /* Next controller in list */
} usb_controller_t;

/* Controller types */
#define USB_HC_UHCI     0           /* USB 1.1 */
#define USB_HC_OHCI     1           /* USB 1.1 */
#define USB_HC_EHCI     2           /* USB 2.0 */
#define USB_HC_XHCI     3           /* USB 3.0+ */

/**
 * USB Device Driver
 */
typedef struct usb_driver {
    char name[32];                  /* Driver name */
    
    /* Match criteria */
    uint16_t id_vendor;             /* Vendor ID (0 = any) */
    uint16_t id_product;            /* Product ID (0 = any) */
    uint8_t class_code;             /* Class code (0 = any) */
    uint8_t subclass;               /* Subclass (0 = any) */
    uint8_t protocol;               /* Protocol (0 = any) */
    
    /* Driver callbacks */
    int (*probe)(usb_interface_t *interface, const usb_device_descriptor_t *desc);
    void (*disconnect)(usb_interface_t *interface);
    void (*suspend)(usb_interface_t *interface);
    void (*resume)(usb_interface_t *interface);
    
    struct usb_driver *next;        /* Next driver in list */
} usb_driver_t;

/* ================================================================
 * USB CORE API
 * ================================================================ */

/**
 * Initialize USB subsystem
 */
int usb_init(void);

/**
 * Register a host controller
 */
int usb_register_controller(usb_controller_t *hc);

/**
 * Unregister a host controller
 */
void usb_unregister_controller(usb_controller_t *hc);

/**
 * Register a USB device driver
 */
int usb_register_driver(usb_driver_t *driver);

/**
 * Unregister a USB device driver
 */
void usb_unregister_driver(usb_driver_t *driver);

/**
 * Enumerate USB devices on all controllers
 */
void usb_enumerate(void);

/**
 * Reset and enumerate a specific port
 */
int usb_port_reset(usb_controller_t *hc, int port);

/**
 * Control transfer (synchronous)
 */
int usb_control_transfer(usb_device_t *device,
                         uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index,
                         void *data, uint16_t length);

/**
 * Bulk transfer (synchronous)
 */
int usb_bulk_transfer(usb_device_t *device, usb_endpoint_t *endpoint,
                      void *data, uint32_t length, uint32_t *actual);

/**
 * Interrupt transfer (synchronous)
 */
int usb_interrupt_transfer(usb_device_t *device, usb_endpoint_t *endpoint,
                           void *data, uint32_t length, uint32_t *actual);

/**
 * Get device descriptor
 */
int usb_get_device_descriptor(usb_device_t *device, usb_device_descriptor_t *desc);

/**
 * Get configuration descriptor
 */
int usb_get_config_descriptor(usb_device_t *device, uint8_t index, void *buffer, uint16_t length);

/**
 * Set device configuration
 */
int usb_set_configuration(usb_device_t *device, uint8_t config);

/**
 * Set device address
 */
int usb_set_address(usb_device_t *device, uint8_t address);

/**
 * Get string descriptor
 */
int usb_get_string(usb_device_t *device, uint8_t index, char *buffer, int length);

/**
 * Find endpoint in interface
 */
usb_endpoint_t *usb_find_endpoint(usb_interface_t *interface, uint8_t type, uint8_t direction);

/**
 * Set interface alternate setting
 */
int usb_set_interface(usb_device_t *device, uint8_t interface, uint8_t alternate);

/**
 * Clear endpoint halt condition
 */
int usb_clear_halt(usb_device_t *device, usb_endpoint_t *endpoint);

/**
 * Allocate transfer buffer (DMA-capable)
 */
void *usb_alloc_buffer(uint32_t size);

/**
 * Free transfer buffer
 */
void usb_free_buffer(void *buffer);

/**
 * Get virtual address for physical address
 */
void *usb_phys_to_virt(uint32_t phys);

/**
 * Get physical address for virtual address
 */
uint32_t usb_virt_to_phys(void *virt);

/**
 * Debug: Print device info
 */
void usb_print_device(usb_device_t *device);

#endif /* _TOCINOS_USB_CORE_H */
