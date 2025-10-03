/**
 * Programmable Interval Timer (PIT) Driver for TocinOS
 * 
 * Provides timer interrupts for preemptive multitasking
 */

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

// PIT frequency (Hz)
#define PIT_FREQUENCY 1193180

// Initialize timer with specified frequency
void timer_init(uint32_t frequency);

// Get number of ticks since boot
uint32_t timer_get_ticks(void);

// Sleep for specified number of ticks
void timer_wait(uint32_t ticks);

#endif // TIMER_H
