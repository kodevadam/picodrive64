/*
 * RDP-based tile renderer for PicoDrive N64 (in development).
 *
 * Goal: replace draw2.c's per-tile CPU blit (DrawLayerFull /
 * DrawTilesFromCacheF / DrawSpriteFull -> TileX*Y*) with RDP
 * texture-rectangle draws, backed by TMEM-cached Genesis tiles and
 * TLUT palettes.  Expected win: layer rendering drops from ~8-9 ms
 * to ~1-2 ms per frame, unlocking a true 60 fps budget.
 *
 * Status: scaffolding + helpers.  PicoFrameFullRDP() still delegates
 * to the CPU renderer.  The helpers (rdp_tiles_build_tlut,
 * rdp_tile_draw_demo) are callable from main_n64.c after
 * rdpq_attach, and gated by RDP_TILES_DEMO for validation.
 *
 * Attach-point problem: the main loop currently calls rdpq_attach
 * AFTER PicoFrame() returns.  PicoFrameFull runs inside PicoFrame,
 * so rdpq calls from PicoFrameFullRDP would have no destination
 * surface attached.  Before flipping n64_use_rdp_tiles=1 we'll need
 * to restructure main_n64.c so the framebuffer is attached before
 * PicoFrame (or render into a private intermediate surface).
 *
 * Genesis tile format note: a Genesis tile is 32 bytes, 8 rows of
 * 4 bytes (4bpp, 2 pixels per byte, high nibble = left pixel).
 * This is byte-for-byte identical to RDP CI4 format -- we can
 * upload tile data via DMA without any reformat.  Each tile uses
 * a 4-bit palette (0..15) selecting one of 4 16-color banks in the
 * Genesis CRAM; we map those to rdpq_tile.palette 0..3.
 *
 * Build plan (incremental, each step testable):
 *   1. Scaffold.  [done]
 *   2. Palette/tile helpers + demo draw.  [this commit]
 *   3. Attach-point refactor: attach fb before PicoFrame.
 *   4. PicoFrameFullRDP draws plane B layer via rdpq.
 *   5. Plane A lo, sprites lo, priority cache passes.
 *   6. Window + per-row hscroll variants.
 */
#ifdef N64
#include <stdint.h>
#include <string.h>
#include <libdragon.h>
#include <rdpq.h>
#include <rdpq_mode.h>
#include <rdpq_tex.h>
#include <surface.h>

#include <pico/pico_int.h>

/* Defined in draw2.c — the existing proven CPU renderer. */
extern void PicoFrameFull(void);

/* Build a 64-entry RGBA5551 TLUT from Genesis CRAM (HighPal[0..63]).
 * HighPal is already BGR555 as used for the CI8 blit; we just
 * shuffle channel positions to RGBA5551 for the RDP TLUT.  Called
 * when the palette changes. */
void rdp_tiles_build_tlut(uint16_t *tlut_out)
{
	const unsigned short *src = Pico.est.HighPal;
	for (int i = 0; i < 64; i++) {
		uint16_t c = src[i];
		uint16_t r = (c      ) & 0x1f;
		uint16_t g = (c >>  5) & 0x1f;
		uint16_t b = (c >> 10) & 0x1f;
		tlut_out[i] = (r << 11) | (g << 6) | (b << 1) | 1;
	}
}

/* Demo: draw one Genesis tile via RDP at screen position (x,y) using
 * palette index (0..3).  Call after rdpq_attach and rdpq_set_mode_*.
 * Used only to validate the TMEM/CI4/TLUT plumbing end-to-end; the
 * real renderer will batch upload + batch draw.
 *
 * tile_vram_addr is a byte offset into Pico.vram (which is 64 KB of
 * u16 words for picodrive -- the address here is the raw byte offset
 * matching Genesis nametable tile index * 32). */
void rdp_tile_draw_demo(unsigned tile_vram_byte_addr, int x, int y,
                        int palette_idx, const uint16_t *tlut)
{
	const uint8_t *tile_src = ((const uint8_t *)PicoMem.vram)
	                        + (tile_vram_byte_addr & 0xFFE0);

	/* Genesis tile = 32 bytes = 8x8 CI4.  Build a surface wrapper
	 * over the VRAM bytes; stride is 4 (bytes per row of 8 CI4 px). */
	surface_t tile_surf = surface_make((void *)tile_src,
	                                   FMT_CI4, 8, 8, 4);

	/* TMEM: upload TLUT (64 entries) then tile pixels. */
	rdpq_tex_upload_tlut((void *)tlut, 0, 64);

	rdpq_texparms_t p = { .palette = palette_idx };
	rdpq_tex_upload(TILE0, &tile_surf, &p);

	rdpq_mode_tlut(TLUT_RGBA16);
	rdpq_texture_rectangle(TILE0, x, y, x + 8, y + 8, 0, 0);
}

void PicoFrameFullRDP(void)
{
	/* Stub: just call the CPU renderer while we build out the RDP
	 * path.  Runtime toggle (n64_use_rdp_tiles) stays safe. */
	PicoFrameFull();
}

#endif /* N64 */
