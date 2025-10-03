/**
 * Serial Port Driver for TocinOS
 * 
 * COM1/COM2 serial communication support
 */

#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

// Serial port definitions
#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

// Initialize serial port
int serial_init(uint16_t port);

// Write character to serial port
void serial_putchar(uint16_t port, char c);

// Write string to serial port
void serial_write(uint16_t port, const char *str);

// Read character from serial port (blocking)
char serial_getchar(uint16_t port);

// Check if data is available
int serial_received(uint16_t port);

// Check if transmit buffer is empty
int serial_is_transmit_empty(uint16_t port);

#endif // SERIAL_H
