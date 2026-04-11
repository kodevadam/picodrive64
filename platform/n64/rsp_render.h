/*
 * RSP-assisted tile rendering for PicoDrive N64
 *
 * The RSP decodes 4bpp Genesis tiles into 8-bit pixels in parallel
 * with VR4300 CPU handling sprites, scrolling, and I/O.
 */
#ifndef RSP_RENDER_H
#define RSP_RENDER_H

#include <libdragon.h>

/* Initialize RSP tile renderer */
void rsp_render_init(void);

/* Decode a batch of tile rows using RSP.
 * tile_words: array of 32-bit packed VDP tile data
 * palette_bases: array of palette base values per tile
 * output: array of 8-byte pixel rows (8 pixels per tile row)
 * count: number of tile rows to decode
 *
 * This function is async — it starts the RSP and returns.
 * Call rsp_render_wait() before reading output. */
void rsp_render_tiles_async(const u32 *tile_words, const u8 *palette_bases,
                            u8 *output, int count);

/* Wait for RSP tile decode to complete */
void rsp_render_wait(void);

#endif
