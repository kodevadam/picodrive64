/*
 * RSP-assisted tile rendering for PicoDrive N64
 *
 * The RSP decodes 4bpp Genesis tiles into 8-bit pixels in parallel
 * with VR4300 CPU handling sprites, scrolling, and I/O.
 */
#ifndef RSP_RENDER_H
#define RSP_RENDER_H

#include <stdint.h>
#include <libdragon.h>

void rsp_render_init(void);
void rsp_render_tiles(uint32_t *tile_words, uint8_t *palette_bases, int count);
uint8_t *rsp_render_get_output(void);
void rsp_render_wait(void);

#endif
