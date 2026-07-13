/**
 * TocinOS Physical Frame Reference Counts (M2, for COW fork)
 *
 * A shared-frame refcount table covering the PMM window [0, 128MB) —
 * every frame either allocator can hand out lies inside it (the buddy
 * region is capped at 128MB too). 32768 frames x 2 bytes = 64KB of BSS.
 *
 * Convention:
 *   0  = untracked. Frames mapped by pre-VMA code (eager ELF loader,
 *        user stacks) have no entry; they behave as if owned by exactly
 *        one mapping.
 *   n  = n mappings reference the frame.
 *
 * frame_ref_share() promotes an untracked frame to count 2 (the implicit
 * owner plus the new sharer); frame_ref_drop() on an untracked frame
 * returns 0 (last implicit owner released). Frames outside the window
 * (device memory) are never considered droppable.
 */

#include "../include/kernel/memory.h"

#define FRAME_REF_WINDOW_BYTES (128u * 1024u * 1024u)
#define FRAME_REF_ENTRIES      (FRAME_REF_WINDOW_BYTES / PAGE_SIZE)

static uint16_t frame_refs[FRAME_REF_ENTRIES];

static int frame_index(uint32_t phys_addr) {
    if (phys_addr >= FRAME_REF_WINDOW_BYTES) {
        return -1;
    }
    return (int)(phys_addr >> PAGE_SHIFT);
}

void frame_ref_init(void) {
    for (uint32_t i = 0; i < FRAME_REF_ENTRIES; i++) {
        frame_refs[i] = 0;
    }
}

/** Raw count (0 = untracked). Out-of-window frames report 0. */
uint32_t frame_ref_get(uint32_t phys_addr) {
    int i = frame_index(phys_addr);
    return (i < 0) ? 0 : frame_refs[i];
}

/** Set an explicit count (used when a frame is first allocated: 1). */
void frame_ref_set(uint32_t phys_addr, uint32_t count) {
    int i = frame_index(phys_addr);
    if (i >= 0) {
        frame_refs[i] = (count > 0xFFFFu) ? 0xFFFFu : (uint16_t)count;
    }
}

/**
 * Add one sharer. An untracked frame (count 0) is promoted to 2:
 * the pre-existing implicit owner plus the new sharer.
 */
void frame_ref_share(uint32_t phys_addr) {
    int i = frame_index(phys_addr);
    if (i < 0) {
        return;
    }
    uint32_t c = frame_refs[i] ? frame_refs[i] : 1u;
    frame_refs[i] = (c >= 0xFFFFu) ? 0xFFFFu : (uint16_t)(c + 1u);
}

/**
 * Release one reference; returns the number of references remaining.
 * The caller frees the frame if (and only if) the result is 0.
 * Untracked frames return 0 (sole implicit owner released).
 * Out-of-window frames return 1 so they are never freed.
 */
uint32_t frame_ref_drop(uint32_t phys_addr) {
    int i = frame_index(phys_addr);
    if (i < 0) {
        return 1;
    }
    uint32_t c = frame_refs[i] ? frame_refs[i] : 1u;
    c--;
    frame_refs[i] = (uint16_t)c;
    return c;
}
