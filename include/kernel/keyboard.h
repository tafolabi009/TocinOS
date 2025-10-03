/**
 * PS/2 Keyboard Driver for TocinOS
 * 
 * Provides interrupt-driven keyboard input
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Keyboard buffer size
#define KEYBOARD_BUFFER_SIZE 256

// Initialize keyboard driver
void keyboard_init(void);

// Get character from keyboard buffer (blocking)
char keyboard_getchar(void);

// Check if keyboard buffer has data
int keyboard_has_data(void);

#endif // KEYBOARD_H
