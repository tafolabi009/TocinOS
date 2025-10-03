/**
 * TocinOS VESA BIOS Extensions (VBE) Driver
 * 
 * Provides support for VESA graphics modes with higher resolutions
 * and color depths than standard VGA text mode
 */

#ifndef VESA_H
#define VESA_H

#include "../stdint.h"

// VESA VBE info structure
typedef struct {
    char signature[4];          // "VESA"
    uint16_t version;           // VBE version
    uint32_t oem_string;        // OEM string pointer
    uint32_t capabilities;      // Capabilities
    uint32_t video_modes;       // Pointer to video mode list
    uint16_t total_memory;      // Total memory in 64KB blocks
    uint16_t oem_software_rev;  // OEM software revision
    uint32_t oem_vendor_name;   // OEM vendor name pointer
    uint32_t oem_product_name;  // OEM product name pointer
    uint32_t oem_product_rev;   // OEM product revision pointer
    uint8_t reserved[222];      // Reserved
    uint8_t oem_data[256];      // OEM data
} __attribute__((packed)) vbe_info_t;

// VESA mode info structure
typedef struct {
    uint16_t attributes;        // Mode attributes
    uint8_t window_a;           // Window A attributes
    uint8_t window_b;           // Window B attributes
    uint16_t granularity;       // Window granularity
    uint16_t window_size;       // Window size
    uint16_t segment_a;         // Window A segment
    uint16_t segment_b;         // Window B segment
    uint32_t win_func_ptr;      // Window function pointer
    uint16_t pitch;             // Bytes per scan line
    uint16_t width;             // Width in pixels
    uint16_t height;            // Height in pixels
    uint8_t w_char;             // Character width
    uint8_t y_char;             // Character height
    uint8_t planes;             // Number of planes
    uint8_t bpp;                // Bits per pixel
    uint8_t banks;              // Number of banks
    uint8_t memory_model;       // Memory model type
    uint8_t bank_size;          // Bank size in KB
    uint8_t image_pages;        // Number of image pages
    uint8_t reserved0;          // Reserved
    uint8_t red_mask;           // Red mask size
    uint8_t red_position;       // Red field position
    uint8_t green_mask;         // Green mask size
    uint8_t green_position;     // Green field position
    uint8_t blue_mask;          // Blue mask size
    uint8_t blue_position;      // Blue field position
    uint8_t reserved_mask;      // Reserved mask size
    uint8_t reserved_position;  // Reserved field position
    uint8_t direct_color_attributes; // Direct color mode attributes
    uint32_t framebuffer;       // Physical address of framebuffer
    uint32_t off_screen_mem_off;// Off-screen memory offset
    uint16_t off_screen_mem_size;// Off-screen memory size
    uint8_t reserved1[206];     // Reserved
} __attribute__((packed)) vbe_mode_info_t;

// Common VESA modes
#define VESA_MODE_640x480x16    0x111
#define VESA_MODE_800x600x16    0x114
#define VESA_MODE_1024x768x16   0x117
#define VESA_MODE_640x480x24    0x112
#define VESA_MODE_800x600x24    0x115
#define VESA_MODE_1024x768x24   0x118

// Graphics context
typedef struct {
    uint32_t *framebuffer;      // Framebuffer pointer
    uint32_t width;             // Screen width
    uint32_t height;            // Screen height
    uint32_t pitch;             // Bytes per line
    uint8_t bpp;                // Bits per pixel
    uint16_t mode;              // Current mode
} vesa_context_t;

// Color structure
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} color_t;

// VESA driver functions
int vesa_init(void);
int vesa_set_mode(uint16_t mode);
int vesa_get_mode_info(uint16_t mode, vbe_mode_info_t *info);
void vesa_put_pixel(uint32_t x, uint32_t y, color_t color);
void vesa_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, color_t color);
void vesa_draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, color_t color);
void vesa_fill_screen(color_t color);
void vesa_draw_char(uint32_t x, uint32_t y, char c, color_t fg, color_t bg);
void vesa_draw_string(uint32_t x, uint32_t y, const char *str, color_t fg, color_t bg);
vesa_context_t *vesa_get_context(void);

// Helper functions
uint32_t vesa_rgb(uint8_t r, uint8_t g, uint8_t b);
color_t vesa_make_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

#endif // VESA_H
