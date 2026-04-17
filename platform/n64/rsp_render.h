#ifndef RSP_RENDER_H
#define RSP_RENDER_H

#include <stdint.h>
#include <libdragon.h>

void rsp_render_init(void);

/* Start async palette conversion: 8-bit indexed -> RGBA5551.
 * src/dst must be in uncached RDRAM or cache-flushed. */
void rsp_render_start(void *src, void *dst,
                      uint16_t *palette_rgba5551, int num_pixels);

void rsp_render_wait(void);

#endif
