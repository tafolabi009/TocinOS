/**
 * TocinOS USB HID (Human Interface Device) Driver
 * 
 * Supports USB keyboards and mice using the HID Boot Protocol.
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
extern void keyboard_buffer_add(char c);
extern void mouse_handle_packet(int dx, int dy, int buttons);

/* ================================================================
 * HID CONSTANTS
 * ================================================================ */

/* HID Class Requests */
#define HID_REQ_GET_REPORT      0x01
#define HID_REQ_GET_IDLE        0x02
#define HID_REQ_GET_PROTOCOL    0x03
#define HID_REQ_SET_REPORT      0x09
#define HID_REQ_SET_IDLE        0x0A
#define HID_REQ_SET_PROTOCOL    0x0B

/* HID Report Types */
#define HID_REPORT_INPUT        0x01
#define HID_REPORT_OUTPUT       0x02
#define HID_REPORT_FEATURE      0x03

/* HID Protocols */
#define HID_PROTO_BOOT          0
#define HID_PROTO_REPORT        1

/* Keyboard Modifier Keys */
#define KBD_MOD_LCTRL       0x01
#define KBD_MOD_LSHIFT      0x02
#define KBD_MOD_LALT        0x04
#define KBD_MOD_LGUI        0x08
#define KBD_MOD_RCTRL       0x10
#define KBD_MOD_RSHIFT      0x20
#define KBD_MOD_RALT        0x40
#define KBD_MOD_RGUI        0x80

/* Mouse Button Bits */
#define MOUSE_BTN_LEFT      0x01
#define MOUSE_BTN_RIGHT     0x02
#define MOUSE_BTN_MIDDLE    0x04

/* Boot Protocol Report Sizes */
#define KBD_BOOT_REPORT_SIZE    8
#define MOUSE_BOOT_REPORT_SIZE  4

/* ================================================================
 * USB HID SCANCODE TO ASCII CONVERSION
 * ================================================================ */

/* USB HID Keyboard Usage IDs to ASCII (US layout) */
static const char hid_kbd_lower[128] = {
    0, 0, 0, 0,                         /* 0x00-0x03: Reserved */
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',  /* 0x04-0x10 */
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',  /* 0x11-0x1D */
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',                  /* 0x1E-0x27 */
    '\n',   /* 0x28: Enter */
    27,     /* 0x29: Escape */
    '\b',   /* 0x2A: Backspace */
    '\t',   /* 0x2B: Tab */
    ' ',    /* 0x2C: Space */
    '-', '=', '[', ']', '\\',   /* 0x2D-0x31 */
    '#',    /* 0x32: Non-US # */
    ';', '\'', '`', ',', '.', '/',   /* 0x33-0x38 */
    0,      /* 0x39: Caps Lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x3A-0x45: F1-F12 */
    0, 0, 0, 0, 0, 0, 0,  /* 0x46-0x4C: Print Screen, Scroll Lock, Pause, Insert, Home, PgUp, Delete */
    0, 0, 0, 0,           /* 0x4D-0x50: End, PgDn, Right, Left */
    0, 0, 0,              /* 0x51-0x53: Down, Up, Num Lock */
    '/', '*', '-', '+', '\n',  /* 0x54-0x58: Keypad */
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '.',  /* 0x59-0x63: Keypad */
    0, 0, 0, '='  /* 0x64-0x67: Non-US \, Application, Power, Keypad = */
};

static const char hid_kbd_upper[128] = {
    0, 0, 0, 0,
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '\n', 27, '\b', '\t', ' ',
    '_', '+', '{', '}', '|',
    '~',
    ':', '"', '~', '<', '>', '?',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '/', '*', '-', '+', '\n',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '.',
    0, 0, 0, '='
};

/* ================================================================
 * HID DEVICE DATA
 * ================================================================ */

typedef struct usb_hid_device {
    usb_interface_t *interface;
    usb_endpoint_t *ep_in;
    usb_endpoint_t *ep_out;
    
    uint8_t protocol;           /* Boot or Report */
    uint8_t device_type;        /* Keyboard or Mouse */
    
    /* Keyboard state */
    uint8_t modifiers;
    uint8_t last_keys[6];
    int caps_lock;
    int num_lock;
    int scroll_lock;
    
    /* Mouse state */
    int mouse_x, mouse_y;
    uint8_t mouse_buttons;
    
    /* Polling */
    uint8_t poll_buffer[8];
    int poll_interval;
    
    struct usb_hid_device *next;
} usb_hid_device_t;

#define HID_TYPE_KEYBOARD   1
#define HID_TYPE_MOUSE      2

static usb_hid_device_t *hid_devices = NULL;

/* ================================================================
 * UTILITY FUNCTIONS
 * ================================================================ */

static void hid_memset(void *dest, uint8_t val, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    while (n--) *d++ = val;
}

static void hid_memcpy(void *dest, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

/* ================================================================
 * HID CLASS REQUESTS
 * ================================================================ */

static int hid_set_idle(usb_interface_t *interface, uint8_t duration, uint8_t report_id) {
    usb_device_t *device = interface->device;
    
    return usb_control_transfer(device,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        HID_REQ_SET_IDLE,
        (duration << 8) | report_id,
        interface->number,
        NULL, 0);
}

static int hid_set_protocol(usb_interface_t *interface, uint8_t protocol) {
    usb_device_t *device = interface->device;
    
    return usb_control_transfer(device,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        HID_REQ_SET_PROTOCOL,
        protocol,
        interface->number,
        NULL, 0);
}

static int hid_get_report(usb_interface_t *interface, uint8_t type, uint8_t id,
                          void *buffer, uint16_t length) {
    usb_device_t *device = interface->device;
    
    return usb_control_transfer(device,
        USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        HID_REQ_GET_REPORT,
        (type << 8) | id,
        interface->number,
        buffer, length);
}

static int hid_set_report(usb_interface_t *interface, uint8_t type, uint8_t id,
                          void *buffer, uint16_t length) {
    usb_device_t *device = interface->device;
    
    return usb_control_transfer(device,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        HID_REQ_SET_REPORT,
        (type << 8) | id,
        interface->number,
        buffer, length);
}

/* ================================================================
 * KEYBOARD HANDLING
 * ================================================================ */

static void hid_keyboard_set_leds(usb_hid_device_t *hid) {
    uint8_t led_report = 0;
    
    if (hid->num_lock) led_report |= 0x01;
    if (hid->caps_lock) led_report |= 0x02;
    if (hid->scroll_lock) led_report |= 0x04;
    
    hid_set_report(hid->interface, HID_REPORT_OUTPUT, 0, &led_report, 1);
}

static void hid_keyboard_process(usb_hid_device_t *hid, uint8_t *report) {
    uint8_t modifiers = report[0];
    /* report[1] is reserved */
    uint8_t *keys = &report[2];
    
    hid->modifiers = modifiers;
    
    int shift = (modifiers & (KBD_MOD_LSHIFT | KBD_MOD_RSHIFT)) != 0;
    int ctrl = (modifiers & (KBD_MOD_LCTRL | KBD_MOD_RCTRL)) != 0;
    int alt = (modifiers & (KBD_MOD_LALT | KBD_MOD_RALT)) != 0;
    
    (void)alt;  /* Not used yet */
    
    /* Process each key */
    for (int i = 0; i < 6; i++) {
        uint8_t key = keys[i];
        
        if (key == 0) continue;
        if (key == 1) continue;  /* Error rollover */
        
        /* Check if this is a new key press */
        int is_new = 1;
        for (int j = 0; j < 6; j++) {
            if (hid->last_keys[j] == key) {
                is_new = 0;
                break;
            }
        }
        
        if (!is_new) continue;
        
        /* Handle special keys */
        if (key == 0x39) {  /* Caps Lock */
            hid->caps_lock = !hid->caps_lock;
            hid_keyboard_set_leds(hid);
            continue;
        }
        if (key == 0x53) {  /* Num Lock */
            hid->num_lock = !hid->num_lock;
            hid_keyboard_set_leds(hid);
            continue;
        }
        if (key == 0x47) {  /* Scroll Lock */
            hid->scroll_lock = !hid->scroll_lock;
            hid_keyboard_set_leds(hid);
            continue;
        }
        
        /* Convert to ASCII */
        if (key < 128) {
            char c;
            
            int use_upper = shift;
            if (hid->caps_lock && key >= 0x04 && key <= 0x1D) {
                use_upper = !use_upper;  /* Letters toggle with caps lock */
            }
            
            if (use_upper) {
                c = hid_kbd_upper[key];
            } else {
                c = hid_kbd_lower[key];
            }
            
            if (c) {
                /* Handle Ctrl combinations */
                if (ctrl && c >= 'a' && c <= 'z') {
                    c = c - 'a' + 1;  /* Ctrl+A = 1, Ctrl+Z = 26 */
                } else if (ctrl && c >= 'A' && c <= 'Z') {
                    c = c - 'A' + 1;
                }
                
                serial_printf("[HID] Key: '%c' (0x%02X)\n", c, key);
                keyboard_buffer_add(c);
            }
        }
    }
    
    /* Save current keys for next comparison */
    hid_memcpy(hid->last_keys, keys, 6);
}

/* ================================================================
 * MOUSE HANDLING
 * ================================================================ */

static void hid_mouse_process(usb_hid_device_t *hid, uint8_t *report) {
    uint8_t buttons = report[0];
    int8_t dx = (int8_t)report[1];
    int8_t dy = (int8_t)report[2];
    /* report[3] is wheel (if present) */
    
    hid->mouse_buttons = buttons;
    hid->mouse_x += dx;
    hid->mouse_y += dy;
    
    /* Clamp to reasonable bounds */
    if (hid->mouse_x < 0) hid->mouse_x = 0;
    if (hid->mouse_y < 0) hid->mouse_y = 0;
    if (hid->mouse_x > 1920) hid->mouse_x = 1920;
    if (hid->mouse_y > 1080) hid->mouse_y = 1080;
    
    if (dx != 0 || dy != 0 || buttons != 0) {
        serial_printf("[HID] Mouse: x=%d y=%d btn=%02X\n", 
                      hid->mouse_x, hid->mouse_y, buttons);
    }
    
    /* Notify mouse subsystem */
    /* mouse_handle_packet(dx, dy, buttons); */
}

/* ================================================================
 * HID POLLING
 * ================================================================ */

/**
 * Poll HID device for input (called periodically)
 */
void usb_hid_poll(void) {
    for (usb_hid_device_t *hid = hid_devices; hid; hid = hid->next) {
        if (!hid->interface || !hid->ep_in) continue;
        
        usb_device_t *device = hid->interface->device;
        uint32_t actual;
        
        int ret = usb_interrupt_transfer(device, hid->ep_in,
                                         hid->poll_buffer, 8, &actual);
        
        if (ret == USB_STATUS_SUCCESS && actual > 0) {
            if (hid->device_type == HID_TYPE_KEYBOARD) {
                hid_keyboard_process(hid, hid->poll_buffer);
            } else if (hid->device_type == HID_TYPE_MOUSE) {
                hid_mouse_process(hid, hid->poll_buffer);
            }
        }
    }
}

/* ================================================================
 * USB DRIVER CALLBACKS
 * ================================================================ */

static int usb_hid_probe(usb_interface_t *interface, const usb_device_descriptor_t *desc) {
    (void)desc;
    
    serial_printf("[HID] Probing interface %d (class=%02X/%02X/%02X)\n",
                  interface->number, interface->class_code,
                  interface->subclass, interface->protocol);
    
    /* Check for HID class */
    if (interface->class_code != USB_CLASS_HID) {
        return -1;
    }
    
    /* Check for boot protocol support */
    if (interface->subclass != USB_HID_SUBCLASS_BOOT) {
        serial_printf("[HID] Only boot protocol supported\n");
        return -1;
    }
    
    /* Determine device type */
    int device_type = 0;
    if (interface->protocol == USB_HID_PROTOCOL_KEYBOARD) {
        device_type = HID_TYPE_KEYBOARD;
        serial_printf("[HID] USB Keyboard detected\n");
    } else if (interface->protocol == USB_HID_PROTOCOL_MOUSE) {
        device_type = HID_TYPE_MOUSE;
        serial_printf("[HID] USB Mouse detected\n");
    } else {
        serial_printf("[HID] Unknown HID protocol %d\n", interface->protocol);
        return -1;
    }
    
    /* Find interrupt IN endpoint */
    usb_endpoint_t *ep_in = usb_find_endpoint(interface, 
                                              USB_EP_TYPE_INTERRUPT, USB_DIR_IN);
    if (!ep_in) {
        serial_printf("[HID] No interrupt IN endpoint found\n");
        return -1;
    }
    
    /* Allocate HID device structure */
    usb_hid_device_t *hid = kmalloc(sizeof(usb_hid_device_t));
    if (!hid) {
        return -1;
    }
    
    hid_memset(hid, 0, sizeof(usb_hid_device_t));
    hid->interface = interface;
    hid->ep_in = ep_in;
    hid->ep_out = usb_find_endpoint(interface, USB_EP_TYPE_INTERRUPT, USB_DIR_OUT);
    hid->device_type = device_type;
    hid->protocol = HID_PROTO_BOOT;
    hid->poll_interval = ep_in->interval;
    hid->num_lock = 1;  /* Enable Num Lock by default */
    
    /* Set boot protocol */
    hid_set_protocol(interface, HID_PROTO_BOOT);
    
    /* Set idle rate (infinite) */
    hid_set_idle(interface, 0, 0);
    
    /* Set initial LED state */
    if (device_type == HID_TYPE_KEYBOARD) {
        hid_keyboard_set_leds(hid);
    }
    
    /* Store private data */
    interface->driver_data = hid;
    
    /* Add to device list */
    hid->next = hid_devices;
    hid_devices = hid;
    
    serial_printf("[HID] Device configured (poll interval: %d ms)\n", 
                  hid->poll_interval);
    
    return 0;
}

static void usb_hid_disconnect(usb_interface_t *interface) {
    usb_hid_device_t *hid = (usb_hid_device_t *)interface->driver_data;
    
    if (!hid) return;
    
    serial_printf("[HID] Device disconnected\n");
    
    /* Remove from list */
    if (hid_devices == hid) {
        hid_devices = hid->next;
    } else {
        for (usb_hid_device_t *d = hid_devices; d; d = d->next) {
            if (d->next == hid) {
                d->next = hid->next;
                break;
            }
        }
    }
    
    kfree(hid);
    interface->driver_data = NULL;
}

/* ================================================================
 * USB DRIVER REGISTRATION
 * ================================================================ */

static usb_driver_t usb_hid_driver = {
    .name = "usb_hid",
    .id_vendor = 0,         /* Any vendor */
    .id_product = 0,        /* Any product */
    .class_code = USB_CLASS_HID,
    .subclass = 0,          /* Any subclass */
    .protocol = 0,          /* Any protocol */
    .probe = usb_hid_probe,
    .disconnect = usb_hid_disconnect,
    .suspend = NULL,
    .resume = NULL,
    .next = NULL
};

/**
 * Initialize USB HID subsystem
 */
int usb_hid_init(void) {
    serial_printf("[HID] Initializing USB HID driver\n");
    
    return usb_register_driver(&usb_hid_driver);
}

/**
 * Get current mouse position
 */
void usb_hid_get_mouse(int *x, int *y, uint8_t *buttons) {
    for (usb_hid_device_t *hid = hid_devices; hid; hid = hid->next) {
        if (hid->device_type == HID_TYPE_MOUSE) {
            if (x) *x = hid->mouse_x;
            if (y) *y = hid->mouse_y;
            if (buttons) *buttons = hid->mouse_buttons;
            return;
        }
    }
    
    /* No mouse found */
    if (x) *x = 0;
    if (y) *y = 0;
    if (buttons) *buttons = 0;
}
