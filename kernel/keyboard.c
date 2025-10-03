/**
 * Keyboard Driver Implementation for TocinOS
 * 
 * PS/2 keyboard driver with interrupt support
 */

#include "../include/kernel/keyboard.h"
#include "../include/kernel/isr.h"
#include "../include/kernel/idt.h"

// Keyboard I/O port
#define KEYBOARD_DATA_PORT 0x60

// Keyboard buffer
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static uint32_t buffer_read_pos = 0;
static uint32_t buffer_write_pos = 0;

// US QWERTY keyboard scancode to ASCII table (scancode set 1)
static const char scancode_to_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*',  0,   ' '
};

// Shifted characters
static const char scancode_to_ascii_shifted[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*',  0,   ' '
};

// Shift state
static int shift_pressed = 0;

// Port I/O functions
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * Keyboard interrupt handler
 */
static void keyboard_handler(registers_t *regs) {
    (void)regs; // Unused
    
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    // Handle shift keys
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return;
    } else if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = 0;
        return;
    }
    
    // Ignore key releases (high bit set)
    if (scancode & 0x80) {
        return;
    }
    
    // Convert scancode to ASCII
    char ascii = 0;
    if (scancode < sizeof(scancode_to_ascii)) {
        if (shift_pressed) {
            ascii = scancode_to_ascii_shifted[scancode];
        } else {
            ascii = scancode_to_ascii[scancode];
        }
    }
    
    // Add to buffer if valid
    if (ascii != 0) {
        uint32_t next_pos = (buffer_write_pos + 1) % KEYBOARD_BUFFER_SIZE;
        if (next_pos != buffer_read_pos) {
            keyboard_buffer[buffer_write_pos] = ascii;
            buffer_write_pos = next_pos;
        }
    }
}

/**
 * Initialize keyboard driver
 */
void keyboard_init(void) {
    // Register keyboard interrupt handler
    isr_register_handler(IRQ_BASE + IRQ_KEYBOARD, keyboard_handler);
}

/**
 * Get character from keyboard (blocking)
 */
char keyboard_getchar(void) {
    while (buffer_read_pos == buffer_write_pos) {
        __asm__ volatile ("hlt");
    }
    
    char c = keyboard_buffer[buffer_read_pos];
    buffer_read_pos = (buffer_read_pos + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

/**
 * Check if keyboard has data
 */
int keyboard_has_data(void) {
    return buffer_read_pos != buffer_write_pos;
}
