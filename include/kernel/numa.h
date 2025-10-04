/**
 * TocinOS NUMA Support
 * 
 * Non-Uniform Memory Access support
 */

#ifndef NUMA_H
#define NUMA_H

#include "../stdint.h"

// NUMA configuration
#define MAX_NUMA_NODES 8
#define MAX_NR_ZONES   4

// CPU mask (simplified)
typedef struct {
    uint64_t bits;
} cpumask_t;

// NUMA node structure
typedef struct numa_node {
    uint32_t node_id;               // NUMA node ID
    uint64_t start_pfn;             // Starting page frame number
    uint64_t end_pfn;               // Ending page frame number
    uint64_t present_pages;         // Total pages present
    uint64_t free_pages;            // Free pages
    
    void *zones[MAX_NR_ZONES];      // Memory zones
    void *pgdat;                    // Page data structure
    
    cpumask_t cpumask;              // CPUs in this node
    uint32_t distance[MAX_NUMA_NODES]; // Distance to other nodes
} numa_node_t;

// Function prototypes

/**
 * Initialize NUMA subsystem
 */
void numa_init(void);

/**
 * Allocate memory from preferred NUMA node
 */
void *numa_alloc_onnode(uint64_t size, int node);

/**
 * Get NUMA node for CPU
 */
int numa_cpu_to_node(int cpu);

/**
 * Migrate page to another NUMA node
 */
int numa_migrate_page(void *page, int target_node);

/**
 * Get distance between NUMA nodes
 */
uint32_t numa_distance(int from_node, int to_node);

/**
 * Get number of NUMA nodes
 */
int numa_num_nodes(void);

/**
 * Check if NUMA is enabled
 */
int numa_is_enabled(void);

#endif // NUMA_H
