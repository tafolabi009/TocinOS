/**
 * TocinOS USB Mass Storage Class (MSC) Driver
 * 
 * Implements the Bulk-Only Transport (BBB) protocol for USB storage devices.
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

/* ================================================================
 * USB MSC CONSTANTS
 * ================================================================ */

/* Mass Storage Class Requests */
#define MSC_REQ_ADSC            0x00    /* Accept Device-Specific Command */
#define MSC_REQ_GET_REQUESTS    0xFC    /* Get Requests */
#define MSC_REQ_PUT_REQUESTS    0xFD    /* Put Requests */
#define MSC_REQ_GET_MAX_LUN     0xFE    /* Get Max LUN */
#define MSC_REQ_BOMSR           0xFF    /* Bulk-Only Mass Storage Reset */

/* Command Block Wrapper (CBW) */
#define CBW_SIGNATURE           0x43425355  /* 'USBC' */
#define CBW_FLAGS_OUT           0x00
#define CBW_FLAGS_IN            0x80
#define CBW_LEN                 31

/* Command Status Wrapper (CSW) */
#define CSW_SIGNATURE           0x53425355  /* 'USBS' */
#define CSW_STATUS_PASSED       0x00
#define CSW_STATUS_FAILED       0x01
#define CSW_STATUS_PHASE_ERROR  0x02
#define CSW_LEN                 13

/* SCSI Commands */
#define SCSI_TEST_UNIT_READY    0x00
#define SCSI_REQUEST_SENSE      0x03
#define SCSI_INQUIRY            0x12
#define SCSI_MODE_SENSE_6       0x1A
#define SCSI_START_STOP_UNIT    0x1B
#define SCSI_PREVENT_ALLOW      0x1E
#define SCSI_READ_CAPACITY_10   0x25
#define SCSI_READ_10            0x28
#define SCSI_WRITE_10           0x2A
#define SCSI_MODE_SENSE_10      0x5A
#define SCSI_READ_CAPACITY_16   0x9E

/* SCSI Status */
#define SCSI_STATUS_GOOD        0x00
#define SCSI_STATUS_CHECK_COND  0x02
#define SCSI_STATUS_BUSY        0x08

/* ================================================================
 * USB MSC STRUCTURES
 * ================================================================ */

/**
 * Command Block Wrapper (CBW)
 * Used to wrap SCSI commands for USB transport
 */
typedef struct __attribute__((packed)) {
    uint32_t dCBWSignature;         /* 0x43425355 */
    uint32_t dCBWTag;               /* Command tag */
    uint32_t dCBWDataTransferLength;/* Data length */
    uint8_t  bmCBWFlags;            /* Direction flags */
    uint8_t  bCBWLUN;               /* Logical unit number */
    uint8_t  bCBWCBLength;          /* Command block length (1-16) */
    uint8_t  CBWCB[16];             /* Command block (SCSI CDB) */
} usb_msc_cbw_t;

/**
 * Command Status Wrapper (CSW)
 * Returned after command execution
 */
typedef struct __attribute__((packed)) {
    uint32_t dCSWSignature;         /* 0x53425355 */
    uint32_t dCSWTag;               /* Command tag (matches CBW) */
    uint32_t dCSWDataResidue;       /* Data not transferred */
    uint8_t  bCSWStatus;            /* Command status */
} usb_msc_csw_t;

/**
 * SCSI Inquiry Response
 */
typedef struct __attribute__((packed)) {
    uint8_t  peripheral_type;
    uint8_t  removable;
    uint8_t  version;
    uint8_t  response_format;
    uint8_t  additional_length;
    uint8_t  reserved[3];
    char     vendor[8];
    char     product[16];
    char     revision[4];
} scsi_inquiry_response_t;

/**
 * SCSI Read Capacity (10) Response
 */
typedef struct __attribute__((packed)) {
    uint32_t last_lba;              /* Last logical block address */
    uint32_t block_size;            /* Block size in bytes */
} scsi_read_capacity_response_t;

/**
 * USB Mass Storage Device
 */
typedef struct usb_msc_device {
    usb_interface_t *interface;
    usb_endpoint_t *ep_in;
    usb_endpoint_t *ep_out;
    
    uint8_t max_lun;                /* Maximum LUN count */
    uint32_t tag;                   /* Command tag counter */
    
    /* Device info */
    char vendor[9];
    char product[17];
    char revision[5];
    
    /* Capacity info */
    uint32_t num_blocks;            /* Total blocks */
    uint32_t block_size;            /* Block size (typically 512) */
    uint64_t capacity;              /* Total capacity in bytes */
    
    /* Flags */
    int ready;
    int removable;
    
    struct usb_msc_device *next;
} usb_msc_device_t;

static usb_msc_device_t *msc_devices = NULL;

/* ================================================================
 * UTILITY FUNCTIONS
 * ================================================================ */

static void msc_memset(void *dest, uint8_t val, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    while (n--) *d++ = val;
}

static void msc_memcpy(void *dest, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static uint32_t bswap32(uint32_t val) {
    return ((val & 0xFF) << 24) |
           ((val & 0xFF00) << 8) |
           ((val & 0xFF0000) >> 8) |
           ((val & 0xFF000000) >> 24);
}

/* ================================================================
 * BULK-ONLY TRANSPORT
 * ================================================================ */

/**
 * Execute a bulk-only transport command
 */
static int msc_transport(usb_msc_device_t *msc, uint8_t *cdb, uint8_t cdb_len,
                         void *data, uint32_t data_len, int direction) {
    usb_device_t *device = msc->interface->device;
    usb_msc_cbw_t cbw;
    usb_msc_csw_t csw;
    uint32_t actual;
    int ret;
    
    /* Build CBW */
    msc_memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = CBW_SIGNATURE;
    cbw.dCBWTag = msc->tag++;
    cbw.dCBWDataTransferLength = data_len;
    cbw.bmCBWFlags = direction ? CBW_FLAGS_IN : CBW_FLAGS_OUT;
    cbw.bCBWLUN = 0;
    cbw.bCBWCBLength = cdb_len;
    msc_memcpy(cbw.CBWCB, cdb, cdb_len);
    
    /* Send CBW */
    ret = usb_bulk_transfer(device, msc->ep_out, &cbw, CBW_LEN, &actual);
    if (ret != USB_STATUS_SUCCESS) {
        serial_printf("[MSC] Failed to send CBW: %d\n", ret);
        return -1;
    }
    
    /* Data phase (if any) */
    if (data_len > 0) {
        usb_endpoint_t *ep = direction ? msc->ep_in : msc->ep_out;
        ret = usb_bulk_transfer(device, ep, data, data_len, &actual);
        if (ret != USB_STATUS_SUCCESS) {
            serial_printf("[MSC] Data transfer failed: %d\n", ret);
            /* Continue to get CSW for status */
        }
    }
    
    /* Get CSW */
    ret = usb_bulk_transfer(device, msc->ep_in, &csw, CSW_LEN, &actual);
    if (ret != USB_STATUS_SUCCESS) {
        serial_printf("[MSC] Failed to get CSW: %d\n", ret);
        return -1;
    }
    
    /* Validate CSW */
    if (csw.dCSWSignature != CSW_SIGNATURE) {
        serial_printf("[MSC] Invalid CSW signature: 0x%08X\n", csw.dCSWSignature);
        return -1;
    }
    
    if (csw.dCSWTag != cbw.dCBWTag) {
        serial_printf("[MSC] CSW tag mismatch: %d != %d\n", csw.dCSWTag, cbw.dCBWTag);
        return -1;
    }
    
    if (csw.bCSWStatus == CSW_STATUS_FAILED) {
        return -2;  /* Command failed, need REQUEST SENSE */
    }
    
    if (csw.bCSWStatus == CSW_STATUS_PHASE_ERROR) {
        serial_printf("[MSC] Phase error\n");
        return -3;
    }
    
    return 0;
}

/* ================================================================
 * SCSI COMMANDS
 * ================================================================ */

static int msc_test_unit_ready(usb_msc_device_t *msc) {
    uint8_t cdb[6] = { SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    return msc_transport(msc, cdb, 6, NULL, 0, 1);
}

static int msc_inquiry(usb_msc_device_t *msc) {
    uint8_t cdb[6] = { SCSI_INQUIRY, 0, 0, 0, 36, 0 };
    scsi_inquiry_response_t response;
    
    int ret = msc_transport(msc, cdb, 6, &response, 36, 1);
    if (ret != 0) return ret;
    
    /* Copy vendor/product/revision */
    msc_memcpy(msc->vendor, response.vendor, 8);
    msc->vendor[8] = '\0';
    msc_memcpy(msc->product, response.product, 16);
    msc->product[16] = '\0';
    msc_memcpy(msc->revision, response.revision, 4);
    msc->revision[4] = '\0';
    
    msc->removable = (response.removable & 0x80) != 0;
    
    serial_printf("[MSC] Device: %s %s (%s)\n", 
                  msc->vendor, msc->product, msc->revision);
    serial_printf("[MSC] Removable: %s\n", msc->removable ? "Yes" : "No");
    
    return 0;
}

static int msc_read_capacity(usb_msc_device_t *msc) {
    uint8_t cdb[10] = { SCSI_READ_CAPACITY_10, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    scsi_read_capacity_response_t response;
    
    int ret = msc_transport(msc, cdb, 10, &response, 8, 1);
    if (ret != 0) return ret;
    
    /* Values are big-endian */
    msc->num_blocks = bswap32(response.last_lba) + 1;
    msc->block_size = bswap32(response.block_size);
    msc->capacity = (uint64_t)msc->num_blocks * msc->block_size;
    
    serial_printf("[MSC] Capacity: %d blocks x %d bytes = %d MB\n",
                  msc->num_blocks, msc->block_size,
                  (uint32_t)(msc->capacity / (1024 * 1024)));
    
    return 0;
}

static int msc_request_sense(usb_msc_device_t *msc) {
    uint8_t cdb[6] = { SCSI_REQUEST_SENSE, 0, 0, 0, 18, 0 };
    uint8_t sense_data[18];
    
    int ret = msc_transport(msc, cdb, 6, sense_data, 18, 1);
    if (ret != 0) return ret;
    
    uint8_t sense_key = sense_data[2] & 0x0F;
    uint8_t asc = sense_data[12];
    uint8_t ascq = sense_data[13];
    
    serial_printf("[MSC] Sense: Key=%02X ASC=%02X ASCQ=%02X\n",
                  sense_key, asc, ascq);
    
    return 0;
}

/* ================================================================
 * BLOCK I/O
 * ================================================================ */

/**
 * Read blocks from USB mass storage device
 */
int usb_msc_read(usb_msc_device_t *msc, uint32_t lba, uint32_t count, void *buffer) {
    if (!msc || !msc->ready || !buffer) return -1;
    if (lba + count > msc->num_blocks) return -1;
    
    uint8_t cdb[10];
    cdb[0] = SCSI_READ_10;
    cdb[1] = 0;
    cdb[2] = (lba >> 24) & 0xFF;
    cdb[3] = (lba >> 16) & 0xFF;
    cdb[4] = (lba >> 8) & 0xFF;
    cdb[5] = lba & 0xFF;
    cdb[6] = 0;
    cdb[7] = (count >> 8) & 0xFF;
    cdb[8] = count & 0xFF;
    cdb[9] = 0;
    
    uint32_t data_len = count * msc->block_size;
    
    return msc_transport(msc, cdb, 10, buffer, data_len, 1);
}

/**
 * Write blocks to USB mass storage device
 */
int usb_msc_write(usb_msc_device_t *msc, uint32_t lba, uint32_t count, const void *buffer) {
    if (!msc || !msc->ready || !buffer) return -1;
    if (lba + count > msc->num_blocks) return -1;
    
    uint8_t cdb[10];
    cdb[0] = SCSI_WRITE_10;
    cdb[1] = 0;
    cdb[2] = (lba >> 24) & 0xFF;
    cdb[3] = (lba >> 16) & 0xFF;
    cdb[4] = (lba >> 8) & 0xFF;
    cdb[5] = lba & 0xFF;
    cdb[6] = 0;
    cdb[7] = (count >> 8) & 0xFF;
    cdb[8] = count & 0xFF;
    cdb[9] = 0;
    
    uint32_t data_len = count * msc->block_size;
    
    return msc_transport(msc, cdb, 10, (void *)buffer, data_len, 0);
}

/* ================================================================
 * USB MSC CLASS REQUESTS
 * ================================================================ */

static int msc_get_max_lun(usb_msc_device_t *msc) {
    usb_device_t *device = msc->interface->device;
    uint8_t max_lun = 0;
    
    int ret = usb_control_transfer(device,
        USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        MSC_REQ_GET_MAX_LUN,
        0,
        msc->interface->number,
        &max_lun, 1);
    
    if (ret == USB_STATUS_SUCCESS) {
        msc->max_lun = max_lun;
    } else {
        /* Many devices don't support this, default to 0 */
        msc->max_lun = 0;
    }
    
    serial_printf("[MSC] Max LUN: %d\n", msc->max_lun);
    return 0;
}

static int msc_bulk_reset(usb_msc_device_t *msc) {
    usb_device_t *device = msc->interface->device;
    
    return usb_control_transfer(device,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        MSC_REQ_BOMSR,
        0,
        msc->interface->number,
        NULL, 0);
}

/* ================================================================
 * USB DRIVER CALLBACKS
 * ================================================================ */

static int usb_msc_probe(usb_interface_t *interface, const usb_device_descriptor_t *desc) {
    (void)desc;
    
    serial_printf("[MSC] Probing interface %d (class=%02X/%02X/%02X)\n",
                  interface->number, interface->class_code,
                  interface->subclass, interface->protocol);
    
    /* Check for Mass Storage class with BBB protocol */
    if (interface->class_code != USB_CLASS_MASS_STORAGE) {
        return -1;
    }
    
    if (interface->protocol != USB_MSC_PROTOCOL_BBB) {
        serial_printf("[MSC] Only BBB protocol supported (got %02X)\n", 
                      interface->protocol);
        return -1;
    }
    
    /* Find bulk endpoints */
    usb_endpoint_t *ep_in = usb_find_endpoint(interface, 
                                              USB_EP_TYPE_BULK, USB_DIR_IN);
    usb_endpoint_t *ep_out = usb_find_endpoint(interface, 
                                               USB_EP_TYPE_BULK, USB_DIR_OUT);
    
    if (!ep_in || !ep_out) {
        serial_printf("[MSC] Missing bulk endpoints\n");
        return -1;
    }
    
    /* Allocate device structure */
    usb_msc_device_t *msc = kmalloc(sizeof(usb_msc_device_t));
    if (!msc) {
        return -1;
    }
    
    msc_memset(msc, 0, sizeof(usb_msc_device_t));
    msc->interface = interface;
    msc->ep_in = ep_in;
    msc->ep_out = ep_out;
    msc->tag = 1;
    
    /* Get max LUN */
    msc_get_max_lun(msc);
    
    /* Initialize device */
    serial_printf("[MSC] Initializing device...\n");
    
    /* INQUIRY command */
    if (msc_inquiry(msc) != 0) {
        serial_printf("[MSC] INQUIRY failed\n");
        kfree(msc);
        return -1;
    }
    
    /* Wait for device to be ready */
    int retries = 10;
    while (retries-- > 0) {
        if (msc_test_unit_ready(msc) == 0) {
            break;
        }
        msc_request_sense(msc);
        /* Wait a bit (would use timer in real implementation) */
        for (volatile int i = 0; i < 1000000; i++);
    }
    
    if (retries <= 0) {
        serial_printf("[MSC] Device not ready\n");
        kfree(msc);
        return -1;
    }
    
    /* Read capacity */
    if (msc_read_capacity(msc) != 0) {
        serial_printf("[MSC] READ CAPACITY failed\n");
        kfree(msc);
        return -1;
    }
    
    msc->ready = 1;
    
    /* Store driver data */
    interface->driver_data = msc;
    
    /* Add to device list */
    msc->next = msc_devices;
    msc_devices = msc;
    
    serial_printf("[MSC] USB Mass Storage device ready\n");
    
    /* TODO: Register with block device layer */
    /* block_register_device("usb0", msc_read_wrapper, msc_write_wrapper, msc); */
    
    return 0;
}

static void usb_msc_disconnect(usb_interface_t *interface) {
    usb_msc_device_t *msc = (usb_msc_device_t *)interface->driver_data;
    
    if (!msc) return;
    
    serial_printf("[MSC] Device disconnected: %s %s\n", msc->vendor, msc->product);
    
    msc->ready = 0;
    
    /* Remove from list */
    if (msc_devices == msc) {
        msc_devices = msc->next;
    } else {
        for (usb_msc_device_t *d = msc_devices; d; d = d->next) {
            if (d->next == msc) {
                d->next = msc->next;
                break;
            }
        }
    }
    
    kfree(msc);
    interface->driver_data = NULL;
}

/* ================================================================
 * USB DRIVER REGISTRATION
 * ================================================================ */

static usb_driver_t usb_msc_driver = {
    .name = "usb_msc",
    .id_vendor = 0,         /* Any vendor */
    .id_product = 0,        /* Any product */
    .class_code = USB_CLASS_MASS_STORAGE,
    .subclass = USB_MSC_SUBCLASS_SCSI,
    .protocol = USB_MSC_PROTOCOL_BBB,
    .probe = usb_msc_probe,
    .disconnect = usb_msc_disconnect,
    .suspend = NULL,
    .resume = NULL,
    .next = NULL
};

/**
 * Initialize USB Mass Storage subsystem
 */
int usb_msc_init(void) {
    serial_printf("[MSC] Initializing USB Mass Storage driver\n");
    
    return usb_register_driver(&usb_msc_driver);
}

/**
 * Get first available MSC device
 */
usb_msc_device_t *usb_msc_get_device(int index) {
    usb_msc_device_t *msc = msc_devices;
    
    while (msc && index > 0) {
        msc = msc->next;
        index--;
    }
    
    return msc;
}

/**
 * Get device capacity info
 */
int usb_msc_get_capacity(usb_msc_device_t *msc, uint32_t *blocks, uint32_t *block_size) {
    if (!msc || !msc->ready) return -1;
    
    if (blocks) *blocks = msc->num_blocks;
    if (block_size) *block_size = msc->block_size;
    
    return 0;
}
