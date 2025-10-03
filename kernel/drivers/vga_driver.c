/**
 * Example VGA Driver for TocinOS
 * 
 * Demonstrates the Modular Driver Framework (MDF)
 * This is a simple character device driver for VGA text mode
 */

#include "../include/drivers/mdf.h"

#define VGA_BUFFER 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static volatile unsigned short *vga_buffer = (unsigned short *)VGA_BUFFER;
static unsigned int cursor_x = 0;
static unsigned int cursor_y = 0;

/**
 * Initialize the VGA driver
 */
static int vga_init(void) {
    // Clear screen
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (0x0F << 8) | ' ';
    }
    cursor_x = 0;
    cursor_y = 0;
    return 0;
}

/**
 * Probe for VGA hardware
 */
static int vga_probe(void) {
    // VGA is always present in x86 systems
    return 0;
}

/**
 * Open the VGA device
 */
static int vga_open(void) {
    // Nothing to do
    return 0;
}

/**
 * Close the VGA device
 */
static int vga_close(void) {
    // Nothing to do
    return 0;
}

/**
 * Write to VGA device
 */
static int vga_write(const void *buffer, unsigned int size) {
    const char *text = (const char *)buffer;
    
    for (unsigned int i = 0; i < size; i++) {
        if (text[i] == '\n') {
            cursor_x = 0;
            cursor_y++;
        } else if (text[i] == '\r') {
            cursor_x = 0;
        } else {
            int offset = cursor_y * VGA_WIDTH + cursor_x;
            if (offset < VGA_WIDTH * VGA_HEIGHT) {
                vga_buffer[offset] = (0x0F << 8) | text[i];
            }
            cursor_x++;
            if (cursor_x >= VGA_WIDTH) {
                cursor_x = 0;
                cursor_y++;
            }
        }
        
        // Scroll if necessary
        if (cursor_y >= VGA_HEIGHT) {
            cursor_y = VGA_HEIGHT - 1;
            // Scroll screen up
            for (int i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
                vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
            }
            // Clear last line
            for (int i = VGA_WIDTH * (VGA_HEIGHT - 1); i < VGA_WIDTH * VGA_HEIGHT; i++) {
                vga_buffer[i] = (0x0F << 8) | ' ';
            }
        }
    }
    
    return size;
}

/**
 * IOCTL for VGA device
 */
static int vga_ioctl(unsigned int cmd, void *arg) {
    switch (cmd) {
        case 0: // Clear screen
            vga_init();
            return 0;
        case 1: // Get cursor position
            if (arg) {
                unsigned int *pos = (unsigned int *)arg;
                pos[0] = cursor_x;
                pos[1] = cursor_y;
            }
            return 0;
        case 2: // Set cursor position
            if (arg) {
                unsigned int *pos = (unsigned int *)arg;
                cursor_x = pos[0];
                cursor_y = pos[1];
            }
            return 0;
        default:
            return -1;
    }
}

/**
 * Register the VGA driver with MDF
 */
void vga_driver_register(void) {
    mdf_register_driver("vga", DRIVER_TYPE_CHARACTER, vga_init, vga_probe);
}
