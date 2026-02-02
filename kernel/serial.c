/**
 * Serial Port Driver Implementation
 * 
 * Provides COM port communication
 */

#include "../include/kernel/serial.h"

// Port I/O functions
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * Initialize serial port
 * Returns 0 on success, -1 on failure
 */
int serial_init(uint16_t port) {
    // Disable all interrupts
    outb(port + 1, 0x00);
    
    // Enable DLAB (set baud rate divisor)
    outb(port + 3, 0x80);
    
    // Set divisor to 3 (38400 baud)
    outb(port + 0, 0x03);
    outb(port + 1, 0x00);
    
    // 8 bits, no parity, one stop bit
    outb(port + 3, 0x03);
    
    // Enable FIFO, clear them, with 14-byte threshold
    outb(port + 2, 0xC7);
    
    // IRQs enabled, RTS/DSR set
    outb(port + 4, 0x0B);
    
    // Test serial chip (loopback mode)
    outb(port + 4, 0x1E);
    
    // Test serial chip by sending byte 0xAE
    outb(port + 0, 0xAE);
    
    // Check if serial is faulty (i.e., not same byte as sent)
    if (inb(port + 0) != 0xAE) {
        return -1;
    }
    
    // If serial is not faulty, set it in normal operation mode
    // (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
    outb(port + 4, 0x0F);
    
    return 0;
}

/**
 * Check if transmit buffer is empty
 */
int serial_is_transmit_empty(uint16_t port) {
    return inb(port + 5) & 0x20;
}

/**
 * Write character to serial port
 */
void serial_putchar(uint16_t port, char c) {
    // Wait for transmit buffer to be empty
    while (serial_is_transmit_empty(port) == 0);
    
    // Send character
    outb(port, c);
}

/**
 * Write string to serial port
 */
void serial_write(uint16_t port, const char *str) {
    while (*str) {
        serial_putchar(port, *str);
        str++;
    }
}

/**
 * Check if data is available
 */
int serial_received(uint16_t port) {
    return inb(port + 5) & 1;
}

/**
 * Read character from serial port (blocking)
 */
char serial_getchar(uint16_t port) {
    // Wait for data to be available
    while (serial_received(port) == 0);
    
    // Read and return character
    return inb(port);
}

/**
 * Read multiple bytes from serial port (non-blocking)
 */
int serial_read(uint16_t port, char *data, int len) {
    int count = 0;
    while (count < len && serial_received(port)) {
        data[count++] = inb(port);
    }
    return count;
}

/**
 * Print a number in hex
 */
static void serial_print_hex(uint16_t port, uint32_t num) {
    const char hex_chars[] = "0123456789ABCDEF";
    char buf[9];
    buf[8] = 0;
    
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex_chars[num & 0xF];
        num >>= 4;
    }
    serial_write(port, buf);
}

/**
 * Print a number in decimal
 */
static void serial_print_dec(uint16_t port, int32_t num) {
    char buf[12];
    int i = 10;
    int is_neg = 0;
    
    buf[11] = 0;
    
    if (num == 0) {
        serial_putchar(port, '0');
        return;
    }
    
    if (num < 0) {
        is_neg = 1;
        num = -num;
    }
    
    while (num > 0 && i >= 0) {
        buf[i--] = '0' + (num % 10);
        num /= 10;
    }
    
    if (is_neg) {
        buf[i--] = '-';
    }
    
    serial_write(port, &buf[i + 1]);
}

/**
 * Print unsigned number in decimal
 */
static void serial_print_udec(uint16_t port, uint32_t num) {
    char buf[12];
    int i = 10;
    
    buf[11] = 0;
    
    if (num == 0) {
        serial_putchar(port, '0');
        return;
    }
    
    while (num > 0 && i >= 0) {
        buf[i--] = '0' + (num % 10);
        num /= 10;
    }
    
    serial_write(port, &buf[i + 1]);
}

/**
 * Simple printf for serial output
 * Supports: %s (string), %d (decimal), %u (unsigned), %x (hex), %c (char), %p (pointer)
 */
void serial_printf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 's': {
                    const char *s = __builtin_va_arg(args, const char *);
                    if (s) serial_write(COM1, s);
                    else serial_write(COM1, "(null)");
                    break;
                }
                case 'd': {
                    int32_t d = __builtin_va_arg(args, int32_t);
                    serial_print_dec(COM1, d);
                    break;
                }
                case 'u': {
                    uint32_t u = __builtin_va_arg(args, uint32_t);
                    serial_print_udec(COM1, u);
                    break;
                }
                case 'x': {
                    uint32_t x = __builtin_va_arg(args, uint32_t);
                    serial_print_hex(COM1, x);
                    break;
                }
                case 'p': {
                    void *p = __builtin_va_arg(args, void *);
                    serial_write(COM1, "0x");
                    serial_print_hex(COM1, (uint32_t)p);
                    break;
                }
                case 'c': {
                    char c = (char)__builtin_va_arg(args, int);
                    serial_putchar(COM1, c);
                    break;
                }
                case '%': {
                    serial_putchar(COM1, '%');
                    break;
                }
                default:
                    serial_putchar(COM1, '%');
                    serial_putchar(COM1, *fmt);
                    break;
            }
        } else {
            serial_putchar(COM1, *fmt);
        }
        fmt++;
    }
    
    __builtin_va_end(args);
}
