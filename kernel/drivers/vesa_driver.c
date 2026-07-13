/**
 * TocinOS VESA Driver Implementation
 * 
 * Implements VESA graphics mode support with drawing primitives
 */

#include "../include/drivers/vesa.h"
#include "../include/drivers/mdf.h"
#include "../include/boot/boot_info.h"
#include "../include/kernel/bootinfo.h"
#include "../include/kernel/serial.h"

// Global VESA context
static vesa_context_t vesa_ctx = {0};
static int vesa_initialized = 0;

// Simple 8x8 font data (ASCII 32-127)
static uint8_t font8x8[96][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // Space
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // !
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // "
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // #
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, // $
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, // %
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, // &
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // '
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, // (
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, // )
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // *
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, // +
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // ,
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, // -
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // .
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // /
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, // 0
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, // 1
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, // 2
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, // 3
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, // 4
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, // 5
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, // 6
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, // 7
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // 8
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, // 9
    // Add more characters as needed (simplified version)
};

/**
 * Initialize VESA driver from boot info
 *
 * Prefers the TocinBoot GOP framebuffer (docs/BOOT_PROTOCOL.md §5) when the
 * kernel was entered via the TocinBoot protocol; otherwise falls back to the
 * legacy BIOS boot_info_t block unchanged.
 */
int vesa_init(void) {
    const bootinfo_fb_t *fb = bootinfo_framebuffer();
    if (fb) {
        vesa_ctx.framebuffer = (uint32_t *)(unsigned long)fb->base;
        vesa_ctx.width = fb->width;
        vesa_ctx.height = fb->height;
        vesa_ctx.pitch = fb->pitch;
        vesa_ctx.bpp = (uint8_t)fb->bpp;
        vesa_ctx.mode = 0; // Not a VBE mode number (GOP handoff)

        vesa_initialized = 1;
        serial_printf("[VESA] using tocinboot GOP framebuffer %ux%ux%u pitch=%u base=0x%x\n",
                      fb->width, fb->height, fb->bpp, fb->pitch,
                      (uint32_t)fb->base);
        return 0;
    }

    boot_info_t *boot_info = (boot_info_t *)BOOT_INFO_ADDRESS;

    // Check if framebuffer is available
    if (boot_info->framebuffer_addr == 0) {
        return -1; // No framebuffer available
    }
    
    // Initialize context from boot info
    vesa_ctx.framebuffer = (uint32_t *)boot_info->framebuffer_addr;
    vesa_ctx.width = boot_info->framebuffer_width;
    vesa_ctx.height = boot_info->framebuffer_height;
    vesa_ctx.pitch = boot_info->framebuffer_pitch;
    vesa_ctx.bpp = boot_info->framebuffer_bpp;
    vesa_ctx.mode = 0; // Unknown mode
    
    vesa_initialized = 1;
    return 0;
}

/**
 * Set VESA mode (requires BIOS call, only available during boot)
 * This function is a placeholder - actual mode setting happens in bootloader
 */
int vesa_set_mode(uint16_t mode) {
    (void)mode;
    // Mode setting must be done in real mode (bootloader)
    return -1;
}

/**
 * Get mode info (placeholder)
 */
int vesa_get_mode_info(uint16_t mode, vbe_mode_info_t *info) {
    (void)mode;
    (void)info;
    // Mode info retrieval must be done in real mode
    return -1;
}

/**
 * Get VESA context
 */
vesa_context_t *vesa_get_context(void) {
    return &vesa_ctx;
}

/**
 * Put a pixel at the specified location
 */
void vesa_put_pixel(uint32_t x, uint32_t y, color_t color) {
    if (!vesa_initialized || x >= vesa_ctx.width || y >= vesa_ctx.height) {
        return;
    }
    
    uint32_t offset = y * (vesa_ctx.pitch / 4) + x;
    vesa_ctx.framebuffer[offset] = vesa_rgb(color.r, color.g, color.b);
}

/**
 * Draw a filled rectangle
 */
void vesa_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, color_t color) {
    if (!vesa_initialized) {
        return;
    }
    
    uint32_t pixel_color = vesa_rgb(color.r, color.g, color.b);
    
    for (uint32_t py = y; py < y + height && py < vesa_ctx.height; py++) {
        for (uint32_t px = x; px < x + width && px < vesa_ctx.width; px++) {
            uint32_t offset = py * (vesa_ctx.pitch / 4) + px;
            vesa_ctx.framebuffer[offset] = pixel_color;
        }
    }
}

/**
 * Draw a line using Bresenham's algorithm
 */
void vesa_draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, color_t color) {
    if (!vesa_initialized) {
        return;
    }
    
    int dx = x2 > x1 ? x2 - x1 : x1 - x2;
    int dy = y2 > y1 ? y2 - y1 : y1 - y2;
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        vesa_put_pixel(x1, y1, color);
        
        if (x1 == x2 && y1 == y2) {
            break;
        }
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

/**
 * Fill entire screen with a color
 */
void vesa_fill_screen(color_t color) {
    if (!vesa_initialized) {
        return;
    }
    
    vesa_draw_rect(0, 0, vesa_ctx.width, vesa_ctx.height, color);
}

/**
 * Draw a character using 8x8 font
 */
void vesa_draw_char(uint32_t x, uint32_t y, char c, color_t fg, color_t bg) {
    if (!vesa_initialized || c < 32 || c > 127) {
        return;
    }
    
    int index = c - 32;
    
    for (int row = 0; row < 8; row++) {
        uint8_t line = font8x8[index][row];
        for (int col = 0; col < 8; col++) {
            if (line & (0x80 >> col)) {
                vesa_put_pixel(x + col, y + row, fg);
            } else {
                vesa_put_pixel(x + col, y + row, bg);
            }
        }
    }
}

/**
 * Draw a string
 */
void vesa_draw_string(uint32_t x, uint32_t y, const char *str, color_t fg, color_t bg) {
    if (!vesa_initialized || !str) {
        return;
    }
    
    uint32_t cx = x;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += 8;
        } else {
            vesa_draw_char(cx, y, *str, fg, bg);
            cx += 8;
        }
        str++;
    }
}

/**
 * Convert RGB to pixel format
 */
uint32_t vesa_rgb(uint8_t r, uint8_t g, uint8_t b) {
    // Assume 32-bit RGBA format (0xAARRGGBB)
    return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

/**
 * Create a color structure
 */
color_t vesa_make_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    color_t color;
    color.r = r;
    color.g = g;
    color.b = b;
    color.a = a;
    return color;
}

/**
 * VESA driver initialization for MDF
 */
static int vesa_driver_init(void) {
    return vesa_init();
}

/**
 * VESA driver probe
 */
static int vesa_driver_probe(void) {
    if (bootinfo_framebuffer()) {
        return 0; // TocinBoot GOP framebuffer available
    }
    boot_info_t *boot_info = (boot_info_t *)BOOT_INFO_ADDRESS;
    return (boot_info->framebuffer_addr != 0) ? 0 : -1;
}

/**
 * Register VESA driver with MDF
 */
void vesa_driver_register(void) {
    mdf_register_driver("vesa", DRIVER_TYPE_CHARACTER, vesa_driver_init, vesa_driver_probe);
}
