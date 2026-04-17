/*
 * RDP-based tile renderer for PicoDrive N64 (in development).
 *
 * Goal: replace draw2.c's per-tile CPU blit (DrawLayerFull /
 * DrawTilesFromCacheF / DrawSpriteFull -> TileX*Y*) with RDP
 * texture-rectangle draws, backed by TMEM-cached Genesis tiles and
 * TLUT palettes.
 *
 * Status: validation scaffold.  PicoFrameFullRDP() delegates to the
 * CPU renderer.  The demo hook below lives after rdpq_attach in
 * main_n64.c, gated by RDP_TILES_DEMO, and now draws THREE visible
 * markers to disentangle where the pipeline breaks if the demo
 * doesn't show up:
 *
 *   [1] a bright magenta fill_rectangle at (140,110)-(180,130) --
 *       tests that rdpq commands queued after rdpq_tex_blit are
 *       actually rasterized.  If this is invisible we have a
 *       mode-state or queue-ordering problem.
 *   [2] a CI4 tile drawn from a *synthetic* in-RAM tile buffer
 *       (pixel value = 1 everywhere) with a *synthetic* 64-entry
 *       TLUT (entry 1 = bright cyan).  Bypasses the game's VRAM
 *       + palette so visibility is independent of game state.
 *   [3] a CI4 tile drawn from Genesis VRAM[0] with the game's
 *       actual palette.  If [1] and [2] work but [3] doesn't,
 *       it's the real-data path that needs fixing.
 *
 * Attach-point problem: the main loop calls rdpq_attach AFTER
 * PicoFrame() returns.  Real RDP tile rendering inside
 * PicoFrameFullRDP needs the fb attached first -- that restructure
 * is step 3 of the plan.
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

/* Build a 64-entry RGBA5551 TLUT from Genesis CRAM (HighPal[0..63]). */
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

void rdp_tile_draw_demo(unsigned tile_vram_byte_addr, int x, int y,
                        int palette_idx, const uint16_t *tlut)
{
	/* --- Marker [1]: bright magenta filled rect ---
	 * Switch to fill mode, paint the rect, then switch back to
	 * standard mode for the textured tile draws. */
	rdpq_set_mode_fill(RGBA32(0xFF, 0x00, 0xFF, 0xFF));
	rdpq_fill_rectangle(140, 110, 180, 130);

	rdpq_set_mode_standard();
	rdpq_mode_filter(FILTER_POINT);
	rdpq_mode_tlut(TLUT_RGBA16);

	/* --- Marker [2]: synthetic tile + synthetic TLUT (cyan) ---
	 * Every pixel = index 1.  TLUT entry 1 = cyan.  Independent
	 * of game state -- if this shows, CI4/TMEM/TLUT all work. */
	static uint16_t test_tlut[64] __attribute__((aligned(8)));
	static uint8_t  test_tile[32] __attribute__((aligned(8)));
	test_tlut[1] = (0 << 11) | (31 << 6) | (31 << 1) | 1; /* R=0 G=31 B=31 -> cyan */
	for (int i = 0; i < 32; i++) test_tile[i] = 0x11; /* both nibbles = 1 */
	data_cache_hit_writeback(test_tlut, sizeof(test_tlut));
	data_cache_hit_writeback(test_tile, sizeof(test_tile));

	surface_t test_surf = surface_make(test_tile, FMT_CI4, 8, 8, 4);
	rdpq_tex_upload_tlut(test_tlut, 0, 64);
	rdpq_texparms_t pt = { .palette = 0 };
	rdpq_tex_upload(TILE0, &test_surf, &pt);
	rdpq_texture_rectangle(TILE0, x - 12, y, x - 4, y + 8, 0, 0);

	/* --- Marker [3]: real Genesis tile + real CRAM TLUT --- */
	const uint8_t *tile_src = ((const uint8_t *)PicoMem.vram)
	                        + (tile_vram_byte_addr & 0xFFE0);
	surface_t tile_surf = surface_make((void *)tile_src,
	                                   FMT_CI4, 8, 8, 4);
	rdpq_tex_upload_tlut((void *)tlut, 0, 64);
	rdpq_texparms_t p = { .palette = palette_idx };
	rdpq_tex_upload(TILE0, &tile_surf, &p);
	rdpq_texture_rectangle(TILE0, x + 4, y, x + 12, y + 8, 0, 0);
}

void PicoFrameFullRDP(void)
{
	PicoFrameFull();
}

#endif /* N64 */
