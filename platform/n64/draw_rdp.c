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

/* Build a 64-entry RGBA5551 TLUT from Genesis CRAM (HighPal[0..63]).
 * Color index 0 of each of the 4 palette banks (entries 0, 16, 32, 48)
 * is Genesis-transparent: on layer B it means "show backdrop"; on
 * layer A it means "show layer B below".  We encode that as alpha=0
 * here so rdpq_mode_alphacompare can reject those pixels during draw. */
void rdp_tiles_build_tlut(uint16_t *tlut_out)
{
	const unsigned short *src = Pico.est.HighPal;
	for (int i = 0; i < 64; i++) {
		uint16_t c = src[i];
		uint16_t r = (c      ) & 0x1f;
		uint16_t g = (c >>  5) & 0x1f;
		uint16_t b = (c >> 10) & 0x1f;
		uint16_t a = ((i & 0x0f) == 0) ? 0 : 1;  /* color 0 = transparent */
		tlut_out[i] = (r << 11) | (g << 6) | (b << 1) | a;
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

/* Draw one Genesis scroll plane (A or B) by iterating its visible
 * nametable slice and issuing one rdpq upload+rect per tile.  Caller
 * sets up rdpq state (attach, combiner, filter, TLUT, alpha compare)
 * and TLUT in TMEM.
 *
 * plane: 0 = Plane A, 1 = Plane B.  nametab base:
 *   plane A: (reg[2] & 0x38) << 9
 *   plane B: (reg[4] & 0x07) << 12
 *
 * Scroll: whole-screen mode only (reg[11] & 3 == 0).  Under that
 * mode hscroll is a single u16 at (reg[13]<<9)+plane in VRAM; vscroll
 * is PicoMem.vsram[plane].  Other scroll modes (2-cell, per-scanline)
 * fall back to zero scroll for now -- handled as a later refinement.
 *
 * Deferred: tile H/V flip, priority, window.
 */
static void draw_plane_rdp(int plane, int cols, int x_off, int y_off)
{
	struct PicoVideo *pv = &Pico.video;
	static const uint8_t plane_shift[4] = {5, 6, 5, 7};

	int nametab;
	if (plane == 0) nametab = (pv->reg[2] & 0x38) << 9;
	else            nametab = (pv->reg[4] & 0x07) << 12;

	int plane_w_bits = plane_shift[pv->reg[16] & 3];
	int x_mask = (1 << plane_w_bits) - 1;
	int plane_h_mask;
	{
		int width  = pv->reg[16] & 3;
		int height = (pv->reg[16] >> 4) & 3;
		plane_h_mask = (height << 5) | 0x1f;
		if (width == 1)     plane_h_mask &= 0x3f;
		else if (width > 1) plane_h_mask  = 0x1f;
	}

	/* Scroll: positive hscroll slides plane to the right on screen,
	 * so the tile at plane column (-hscroll)/8 ends up at screen
	 * column 0.  Positive vscroll slides plane upward, so the tile
	 * at plane row (vscroll)/8 is the first visible row. */
	int hscroll = 0, vscroll = 0;
	if ((pv->reg[11] & 3) == 0) {
		int htab = (pv->reg[13] << 9) + plane;
		hscroll = PicoMem.vram[htab & 0x7fff];
	}
	vscroll = PicoMem.vsram[plane] & 0x3ff;

	/* Signed arithmetic: hscroll as read is effectively negated
	 * (Genesis "shift contents right" vs. our "first visible tile
	 * is at negative plane-x"). */
	int xbase     = -hscroll;
	int xsub      = xbase & 7;
	int first_col = (xbase >> 3);
	int n_cols    = cols + (xsub ? 1 : 0);

	int ybase     = vscroll;
	int ysub      = ybase & 7;
	int first_row = (ybase >> 3);
	int n_rows    = 28 + (ysub ? 1 : 0);

	for (int row = 0; row < n_rows; row++) {
		int ty = (first_row + row) & plane_h_mask;
		int nt_row = nametab + (ty << plane_w_bits);
		int sy = y_off + row * 8 - ysub;

		for (int col = 0; col < n_cols; col++) {
			int tx = (first_col + col) & x_mask;
			uint16_t entry = PicoMem.vram[nt_row + tx];

			int tile_idx = entry & 0x7FF;
			int palette  = (entry >> 13) & 0x03;

			const uint8_t *tile_src = ((const uint8_t *)PicoMem.vram)
			                        + (tile_idx << 5);
			surface_t tile_surf = surface_make((void *)tile_src,
			                                   FMT_CI4, 8, 8, 4);

			rdpq_texparms_t p = { .palette = palette };
			rdpq_tex_upload(TILE0, &tile_surf, &p);

			int sx = x_off + col * 8 - xsub;
			rdpq_texture_rectangle(TILE0, sx, sy, sx + 8, sy + 8, 0, 0);
		}
	}
}

void PicoFrameFullRDP(void)
{
	/* Step 3c: Plane B + Plane A with transparency.  Flip, scroll,
	 * priority, sprites still TODO. */
	struct PicoVideo *pv = &Pico.video;
	struct PicoEState *est = &Pico.est;

	/* ---- Backdrop fill ---- */
	uint16_t bg_bgr = est->HighPal[pv->reg[7] & 0x3f];
	uint8_t r5 = (bg_bgr      ) & 0x1f;
	uint8_t g5 = (bg_bgr >>  5) & 0x1f;
	uint8_t b5 = (bg_bgr >> 10) & 0x1f;
	uint8_t r8 = (r5 << 3) | (r5 >> 2);
	uint8_t g8 = (g5 << 3) | (g5 >> 2);
	uint8_t b8 = (b5 << 3) | (b5 >> 2);
	rdpq_set_mode_fill(RGBA32(r8, g8, b8, 0xFF));
	rdpq_fill_rectangle(0, 0, 320, 240);

	/* ---- TLUT upload ---- */
	static uint16_t tlut[64] __attribute__((aligned(8)));
	rdp_tiles_build_tlut(tlut);
	data_cache_hit_writeback(tlut, sizeof(tlut));

	rdpq_set_mode_standard();
	/* Explicit combiner so the texture alpha (from TLUT) reaches
	 * the alpha-compare stage -- rdpq_set_mode_standard doesn't
	 * guarantee which combiner is active and the default in some
	 * versions doesn't route TEX0 alpha. */
	rdpq_mode_combiner(RDPQ_COMBINER_TEX);
	rdpq_mode_filter(FILTER_POINT);
	rdpq_mode_tlut(TLUT_RGBA16);
	rdpq_mode_alphacompare(1);        /* reject TLUT alpha=0 pixels */
	rdpq_tex_upload_tlut(tlut, 0, 64);

	/* ---- Draw both planes (B first so A overlays) ---- */
	int h40   = (pv->reg[12] & 0x01);
	int cols  = h40 ? 40 : 32;
	int x_off = h40 ? 0 : 32;
	int y_off = (240 - 224) / 2;

	draw_plane_rdp(1, cols, x_off, y_off);  /* Plane B, back */
	draw_plane_rdp(0, cols, x_off, y_off);  /* Plane A, front */
}

#endif /* N64 */
