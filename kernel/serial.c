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
