/**
 * TocinOS framebuffer splash console (fbcon) — M1 "graphical splash"
 *
 * Minimal boot-time renderer for the TocinBoot GOP framebuffer
 * (docs/BOOT_PROTOCOL.md §5, docs/ROADMAP.md M1). Draws the TocinOS
 * splash once and then appends kernel boot-log lines beneath it.
 *
 * This is deliberately NOT a full framebuffer console (that is an M3
 * item): there is no scrollback — when the log region fills, it is
 * cleared and writing restarts at the top of the region, keeping the
 * splash intact. On legacy boot paths (no tocinboot framebuffer) every
 * entry point is a safe no-op and the VGA text path is untouched.
 */

#ifndef DRIVERS_FBCON_H
#define DRIVERS_FBCON_H

/**
 * Initialize the framebuffer console.
 *
 * Call exactly once, AFTER vmm_init(): the GOP framebuffer lives above
 * the identity-mapped low 4MB (0x80000000 on OVMF), so fbcon_init()
 * identity-maps the framebuffer range itself via vmm_map_page() before
 * touching it. Requires bootinfo_init() to have run (it always has —
 * it runs before paging is enabled).
 *
 * On success renders the splash and emits one serial line:
 *   "[FBCON] splash rendered WxHxbpp @ 0xBASE"
 *
 * @return 0 on success, -1 when no framebuffer was handed off or its
 *         pixel format is unsupported (not 32bpp) — fbcon stays inactive.
 */
int fbcon_init(void);

/**
 * Append text under the splash. Handles '\n', '\r', '\b' and '\t';
 * long lines wrap. No-op while fbcon is inactive.
 */
void fbcon_puts(const char *str);

/** @return 1 once fbcon_init() succeeded, otherwise 0. */
int fbcon_active(void);

#endif /* DRIVERS_FBCON_H */
