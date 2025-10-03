/**
 * Timer Implementation for TocinOS
 * 
 * PIT-based system timer
 */

#include "../include/kernel/timer.h"
#include "../include/kernel/isr.h"
#include "../include/kernel/idt.h"

// PIT I/O ports
#define PIT_CHANNEL0    0x40
#define PIT_CHANNEL1    0x41
#define PIT_CHANNEL2    0x42
#define PIT_COMMAND     0x43

// Timer tick counter
static volatile uint32_t timer_ticks = 0;

// Port I/O functions
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * Timer interrupt handler
 */
static void timer_handler(registers_t *regs) {
    (void)regs; // Unused parameter
    timer_ticks++;
}

/**
 * Initialize the timer
 */
void timer_init(uint32_t frequency) {
    // Register timer interrupt handler
    isr_register_handler(IRQ_BASE + IRQ_TIMER, timer_handler);
    
    // Calculate divisor
    uint32_t divisor = PIT_FREQUENCY / frequency;
    
    // Send command byte
    outb(PIT_COMMAND, 0x36);
    
    // Send divisor
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

/**
 * Get current tick count
 */
uint32_t timer_get_ticks(void) {
    return timer_ticks;
}

/**
 * Wait for specified number of ticks
 */
void timer_wait(uint32_t ticks) {
    uint32_t end_ticks = timer_ticks + ticks;
    while (timer_ticks < end_ticks) {
        __asm__ volatile ("hlt");
    }
}
