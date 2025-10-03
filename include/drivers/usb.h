/**
 * TocinOS USB Stack
 * 
 * Universal Serial Bus host controller and device support
 * Supports USB 1.1 (UHCI/OHCI), USB 2.0 (EHCI), and USB 3.0 (xHCI)
 */

#ifndef USB_H
#define USB_H

#include "../stdint.h"

// USB version constants
#define USB_VERSION_1_0     0x0100
#define USB_VERSION_1_1     0x0110
#define USB_VERSION_2_0     0x0200
#define USB_VERSION_3_0     0x0300
#define USB_VERSION_3_1     0x0310

// USB speeds
#define USB_SPEED_LOW       0   // 1.5 Mbps (USB 1.0)
#define USB_SPEED_FULL      1   // 12 Mbps (USB 1.1)
#define USB_SPEED_HIGH      2   // 480 Mbps (USB 2.0)
#define USB_SPEED_SUPER     3   // 5 Gbps (USB 3.0)
#define USB_SPEED_SUPER_PLUS 4  // 10 Gbps (USB 3.1)

// USB transfer types
#define USB_TRANSFER_CONTROL    0
#define USB_TRANSFER_BULK       1
#define USB_TRANSFER_INTERRUPT  2
#define USB_TRANSFER_ISOCHRONOUS 3

// USB request types
#define USB_REQ_GET_STATUS          0
#define USB_REQ_CLEAR_FEATURE       1
#define USB_REQ_SET_FEATURE         3
#define USB_REQ_SET_ADDRESS         5
#define USB_REQ_GET_DESCRIPTOR      6
#define USB_REQ_SET_DESCRIPTOR      7
#define USB_REQ_GET_CONFIGURATION   8
#define USB_REQ_SET_CONFIGURATION   9
#define USB_REQ_GET_INTERFACE       10
#define USB_REQ_SET_INTERFACE       11
#define USB_REQ_SYNCH_FRAME         12

// USB descriptor types
#define USB_DESC_DEVICE             1
#define USB_DESC_CONFIGURATION      2
#define USB_DESC_STRING             3
#define USB_DESC_INTERFACE          4
#define USB_DESC_ENDPOINT           5
#define USB_DESC_DEVICE_QUALIFIER   6
#define USB_DESC_OTHER_SPEED_CONFIG 7
#define USB_DESC_INTERFACE_POWER    8

// USB device classes
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
#define USB_CLASS_CONTENT_SECURITY  0x0D
#define USB_CLASS_VIDEO             0x0E
#define USB_CLASS_PERSONAL_HEALTHCARE 0x0F
#define USB_CLASS_AUDIO_VIDEO       0x10
#define USB_CLASS_DIAGNOSTIC        0xDC
#define USB_CLASS_WIRELESS          0xE0
#define USB_CLASS_MISC              0xEF
#define USB_CLASS_APP_SPEC          0xFE
#define USB_CLASS_VENDOR_SPEC       0xFF

// USB device states
#define USB_STATE_ATTACHED          0
#define USB_STATE_POWERED           1
#define USB_STATE_DEFAULT           2
#define USB_STATE_ADDRESS           3
#define USB_STATE_CONFIGURED        4
#define USB_STATE_SUSPENDED         5

// Standard USB descriptors

// Device descriptor
typedef struct {
    uint8_t  bLength;               // Size of descriptor (18 bytes)
    uint8_t  bDescriptorType;       // Device descriptor (0x01)
    uint16_t bcdUSB;                // USB specification version
    uint8_t  bDeviceClass;          // Device class code
    uint8_t  bDeviceSubClass;       // Device subclass code
    uint8_t  bDeviceProtocol;       // Device protocol code
    uint8_t  bMaxPacketSize0;       // Max packet size for endpoint 0
    uint16_t idVendor;              // Vendor ID
    uint16_t idProduct;             // Product ID
    uint16_t bcdDevice;             // Device release number
    uint8_t  iManufacturer;         // Manufacturer string index
    uint8_t  iProduct;              // Product string index
    uint8_t  iSerialNumber;         // Serial number string index
    uint8_t  bNumConfigurations;    // Number of configurations
} __attribute__((packed)) usb_device_desc_t;

// Configuration descriptor
typedef struct {
    uint8_t  bLength;               // Size of descriptor (9 bytes)
    uint8_t  bDescriptorType;       // Configuration descriptor (0x02)
    uint16_t wTotalLength;          // Total length of data returned
    uint8_t  bNumInterfaces;        // Number of interfaces
    uint8_t  bConfigurationValue;   // Configuration value
    uint8_t  iConfiguration;        // Configuration string index
    uint8_t  bmAttributes;          // Configuration attributes
    uint8_t  bMaxPower;             // Maximum power consumption
} __attribute__((packed)) usb_config_desc_t;

// Interface descriptor
typedef struct {
    uint8_t  bLength;               // Size of descriptor (9 bytes)
    uint8_t  bDescriptorType;       // Interface descriptor (0x04)
    uint8_t  bInterfaceNumber;      // Interface number
    uint8_t  bAlternateSetting;     // Alternate setting number
    uint8_t  bNumEndpoints;         // Number of endpoints
    uint8_t  bInterfaceClass;       // Interface class code
    uint8_t  bInterfaceSubClass;    // Interface subclass code
    uint8_t  bInterfaceProtocol;    // Interface protocol code
    uint8_t  iInterface;            // Interface string index
} __attribute__((packed)) usb_interface_desc_t;

// Endpoint descriptor
typedef struct {
    uint8_t  bLength;               // Size of descriptor (7 bytes)
    uint8_t  bDescriptorType;       // Endpoint descriptor (0x05)
    uint8_t  bEndpointAddress;      // Endpoint address
    uint8_t  bmAttributes;          // Endpoint attributes
    uint16_t wMaxPacketSize;        // Maximum packet size
    uint8_t  bInterval;             // Polling interval
} __attribute__((packed)) usb_endpoint_desc_t;

// String descriptor
typedef struct {
    uint8_t  bLength;               // Size of descriptor
    uint8_t  bDescriptorType;       // String descriptor (0x03)
    uint16_t wData[];               // Unicode string
} __attribute__((packed)) usb_string_desc_t;

// Setup packet
typedef struct {
    uint8_t  bmRequestType;         // Request type
    uint8_t  bRequest;              // Request
    uint16_t wValue;                // Value
    uint16_t wIndex;                // Index
    uint16_t wLength;               // Length
} __attribute__((packed)) usb_setup_packet_t;

// USB endpoint
typedef struct {
    uint8_t  address;               // Endpoint address
    uint8_t  attributes;            // Endpoint attributes
    uint16_t max_packet_size;       // Maximum packet size
    uint8_t  interval;              // Polling interval
    void    *buffer;                // Transfer buffer
} usb_endpoint_t;

// USB interface
typedef struct {
    uint8_t  number;                // Interface number
    uint8_t  class;                 // Interface class
    uint8_t  subclass;              // Interface subclass
    uint8_t  protocol;              // Interface protocol
    uint8_t  num_endpoints;         // Number of endpoints
    usb_endpoint_t endpoints[16];   // Endpoints
} usb_interface_t;

// USB device
typedef struct {
    uint8_t  address;               // Device address
    uint8_t  speed;                 // Device speed
    uint8_t  state;                 // Device state
    uint16_t vendor_id;             // Vendor ID
    uint16_t product_id;            // Product ID
    uint8_t  device_class;          // Device class
    uint8_t  num_interfaces;        // Number of interfaces
    usb_interface_t interfaces[16]; // Interfaces
    void    *driver_data;           // Driver-specific data
} usb_device_t;

// USB host controller
typedef struct {
    uint32_t type;                  // Controller type (UHCI/OHCI/EHCI/xHCI)
    uint32_t num_ports;             // Number of ports
    void    *regs;                  // Controller registers
    usb_device_t *devices[128];     // Connected devices
} usb_hc_t;

// USB transfer
typedef struct {
    usb_device_t *device;           // Target device
    uint8_t  endpoint;              // Target endpoint
    uint8_t  type;                  // Transfer type
    void    *buffer;                // Data buffer
    uint32_t length;                // Transfer length
    uint32_t actual_length;         // Actual transferred length
    int      status;                // Transfer status
} usb_transfer_t;

// USB stack API
int usb_init(void);
int usb_detect_controllers(void);

// Host controller operations
int usb_hc_init(usb_hc_t *hc);
int usb_hc_reset(usb_hc_t *hc);
int usb_hc_port_reset(usb_hc_t *hc, uint8_t port);
int usb_hc_port_enable(usb_hc_t *hc, uint8_t port);

// Device operations
int usb_device_init(usb_device_t *device);
int usb_device_reset(usb_device_t *device);
int usb_device_set_address(usb_device_t *device, uint8_t address);
int usb_device_get_descriptor(usb_device_t *device, uint8_t type, uint8_t index, void *buffer, uint16_t length);
int usb_device_set_configuration(usb_device_t *device, uint8_t config);

// Transfer operations
int usb_control_transfer(usb_device_t *device, usb_setup_packet_t *setup, void *data, uint16_t length);
int usb_bulk_transfer(usb_device_t *device, uint8_t endpoint, void *data, uint32_t length);
int usb_interrupt_transfer(usb_device_t *device, uint8_t endpoint, void *data, uint32_t length);

// USB HID (Human Interface Device) support
#define USB_HID_KEYBOARD    0x01
#define USB_HID_MOUSE       0x02
#define USB_HID_JOYSTICK    0x04

int usb_hid_init(usb_device_t *device);
int usb_hid_get_report(usb_device_t *device, void *buffer, uint16_t length);

// USB Mass Storage support
#define USB_MSC_BULK_ONLY   0x50

int usb_msc_init(usb_device_t *device);
int usb_msc_read(usb_device_t *device, uint64_t lba, uint32_t count, void *buffer);
int usb_msc_write(usb_device_t *device, uint64_t lba, uint32_t count, const void *buffer);

#endif // USB_H
