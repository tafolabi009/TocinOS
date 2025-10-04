/**
 * @file profiling.h
 * @brief Kernel Profiling Infrastructure
 * @author TocinOS Team
 * 
 * This file provides kernel profiling capabilities for performance analysis.
 * Collects samples of instruction pointers during timer interrupts to identify
 * performance bottlenecks and hot code paths.
 */

#ifndef PROFILING_H
#define PROFILING_H

#include "../stdint.h"

/** Maximum number of samples to collect */
#define MAX_PROFILE_SAMPLES 10000

/**
 * @brief Profile sample structure
 * 
 * Records a single profiling sample taken during a timer interrupt.
 */
typedef struct {
    uint64_t timestamp;           /**< Timestamp when sample was taken */
    void *instruction_pointer;    /**< Instruction pointer at sample time */
    int cpu_id;                   /**< CPU ID where sample was taken */
    int task_id;                  /**< Task ID that was running */
} profile_sample_t;

/**
 * @brief Profiler state structure
 * 
 * Maintains the state of the kernel profiler.
 */
typedef struct {
    profile_sample_t *samples;    /**< Array of samples */
    uint32_t sample_count;        /**< Number of samples collected */
    uint32_t max_samples;         /**< Maximum samples capacity */
    int enabled;                  /**< Profiler enabled flag */
    uint64_t start_time;          /**< Profiling start time */
    uint64_t total_samples;       /**< Total samples attempted */
    uint32_t dropped_samples;     /**< Samples dropped (buffer full) */
} profiler_t;

/**
 * @brief Start profiling
 * 
 * Initializes the profiler and begins collecting samples.
 * Should be called before the code section you want to profile.
 * 
 * @return 0 on success, -1 on error
 * 
 * @code
 * profile_start();
 * // ... code to profile ...
 * profile_stop();
 * @endcode
 */
int profile_start(void);

/**
 * @brief Stop profiling and dump results
 * 
 * Stops collecting samples and outputs profiling data.
 * Results can be analyzed with tools/analyze_profile.py
 * 
 * @return 0 on success, -1 on error
 */
int profile_stop(void);

/**
 * @brief Record a profile sample
 * 
 * Called from timer interrupt handler to record current state.
 * Should not be called directly by user code.
 * 
 * @param ip Instruction pointer to record
 */
void profile_sample(void *ip);

/**
 * @brief Get profiler statistics
 * 
 * @param stats Pointer to profiler_t structure to fill
 * @return 0 on success, -1 on error
 */
int profile_get_stats(profiler_t *stats);

/**
 * @brief Reset profiler
 * 
 * Clears all collected samples and resets counters.
 */
void profile_reset(void);

/**
 * @brief Check if profiling is enabled
 * 
 * @return 1 if enabled, 0 if disabled
 */
int profile_is_enabled(void);

#endif // PROFILING_H
