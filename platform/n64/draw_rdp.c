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

/* Pre-flip a 32-byte Genesis tile (8 rows x 4 bytes of 4bpp) into
 * a 32-byte output buffer.  Genesis CI4 tile layout: each byte
 * packs two pixels, high nibble = left pixel, low nibble = right.
 *
 * Vflip: reverse row order (top<->bottom).
 * Hflip: within each row, reverse the 4 bytes AND swap the two
 *        nibbles of each byte (so leftmost pixel ends up rightmost).
 *
 * Used by both plane and sprite draws when the entry's flip bits
 * are set, because rdpq_texture_rectangle doesn't expose negative
 * sampling increments in the high-level API.  Out buffer must be
 * 8-byte aligned.
 */
static void flip_tile_data(const uint8_t *src, uint8_t *dst,
                           int hflip, int vflip)
{
	for (int r = 0; r < 8; r++) {
		int sr = vflip ? (7 - r) : r;
		const uint8_t *srow = src + sr * 4;
		uint8_t *drow = dst + r * 4;
		if (hflip) {
			/* reverse 4 bytes AND swap nibbles within each byte */
			for (int b = 0; b < 4; b++) {
				uint8_t x = srow[3 - b];
				drow[b] = ((x & 0x0f) << 4) | ((x & 0xf0) >> 4);
			}
		} else {
			drow[0] = srow[0]; drow[1] = srow[1];
			drow[2] = srow[2]; drow[3] = srow[3];
		}
	}
}


/* Draw one Genesis scroll plane (A or B) at one priority level.
 * Caller sets up rdpq state and TLUT.  Nametable-entry bit 15 is the
 * priority flag (0 = low-priority, 1 = high-priority); we skip tiles
 * whose priority != `want_prio` so the caller can render the four
 * passes in the correct Genesis compositing order:
 *
 *     B low -> A low -> sprites low -> B high -> A high -> sprites high
 *
 * Scroll: whole-screen mode only (reg[11] & 3 == 0).  Other scroll
 * modes fall back to zero scroll for now.
 *
 * Deferred: tile H/V flip, window plane, sprites.
 */
static void draw_plane_rdp(int plane, int want_prio,
                           int cols, int x_off, int y_off)
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

	/* Scroll.  Genesis supports three hscroll modes selected by
	 * reg[11] bits 1:0:
	 *   0: one hscroll value for the whole plane (full-screen)
	 *   2: one per 8-pixel row (cell-based)
	 *   3: one per scanline (we approximate via row start)
	 * Vscroll: full-screen mode (PicoMem.vsram[plane]).  2-cell
	 * vscroll (reg[11] bit 2) not yet supported. */
	int hscroll_mode = pv->reg[11] & 3;
	int htab_base    = pv->reg[13] << 9;
	int vscroll      = PicoMem.vsram[plane] & 0x3ff;
	int ybase        = vscroll;
	int ysub         = ybase & 7;
	int first_row    = (ybase >> 3);
	int n_rows       = 28 + (ysub ? 1 : 0);

	for (int row = 0; row < n_rows; row++) {
		int ty = (first_row + row) & plane_h_mask;
		int nt_row = nametab + (ty << plane_w_bits);
		int sy = y_off + row * 8 - ysub;

		/* Per-row hscroll lookup by mode. */
		int hscroll;
		if (hscroll_mode == 0) {
			hscroll = PicoMem.vram[(htab_base + plane) & 0x7fff];
		} else if (hscroll_mode == 2) {
			/* cell-based: htab stride = 16 words per row (pair of
			 * plane A/B values per cell of 8 lines).  draw2 uses:
			 *   htab + (trow << 4) + plane */
			hscroll = PicoMem.vram[(htab_base + (row << 4) + plane) & 0x7fff];
		} else {
			/* per-scanline (mode 3) -- approximate as the value
			 * at the first line of this tile row. */
			hscroll = PicoMem.vram[(htab_base + (row * 8 << 1) + plane) & 0x7fff];
		}
		int xbase     = -hscroll;
		int xsub      = xbase & 7;
		int first_col = (xbase >> 3);
		int n_cols    = cols + (xsub ? 1 : 0);

		/* CI8 row-atlas batching.  For each row we deduplicate the
		 * (tile_idx, palette, flip) tuples in use, convert them to
		 * a CI8 tile (upper nibble OR'd with palette bank * 16, so
		 * the single shared 64-entry TLUT gives the right color).
		 * Then one upload of the whole atlas and one texture_rect
		 * per visible column referencing its slot.  Cuts rdpq
		 * uploads from ~40 per row to 1 per row, or ~2 per row if
		 * the row's unique-tile count exceeds TMEM capacity.
		 *
		 * TMEM holds 2 KB of texture data in CI8 mode (the upper
		 * 2 KB is TLUT).  At 64 bytes per 8x8 CI8 tile that's
		 * 32 tiles max per batch.  Genesis H40 visible rows can
		 * have up to 41 unique tiles, so flush + restart when we
		 * hit MAX_SLOTS. */
		enum { MAX_SLOTS = 32 };
		static uint8_t  row_atlas[MAX_SLOTS * 64] __attribute__((aligned(8)));
		static uint16_t row_keys [MAX_SLOTS];
		static int8_t   col_slot [48];
		int n_atlas = 0;

		/* Inline-callable flush: upload current atlas, draw rects
		 * for every col that has a non-negative slot, then reset. */
		#define FLUSH_ATLAS() do { \
			if (n_atlas > 0) { \
				data_cache_hit_writeback(row_atlas, n_atlas * 64); \
				surface_t atlas_surf = surface_make(row_atlas, \
				                                    FMT_CI8, \
				                                    n_atlas * 8, 8, \
				                                    n_atlas * 8); \
				rdpq_tex_upload(TILE0, &atlas_surf, NULL); \
				for (int cc = 0; cc < n_cols; cc++) { \
					int slot = col_slot[cc]; \
					if (slot < 0) continue; \
					int sx_ = x_off + cc * 8 - xsub; \
					int s0 = slot * 8; \
					rdpq_texture_rectangle(TILE0, sx_, sy, sx_ + 8, sy + 8, s0, 0); \
					col_slot[cc] = -1; /* drawn; don't redraw on next flush */ \
				} \
				n_atlas = 0; \
			} \
		} while(0)

		for (int col = 0; col < n_cols; col++) col_slot[col] = -1;

		for (int col = 0; col < n_cols; col++) {
			int tx = (first_col + col) & x_mask;
			uint16_t entry = PicoMem.vram[nt_row + tx];
			int prio = (entry >> 15) & 1;
			if (prio != want_prio) continue;

			int tile_idx = entry & 0x7FF;
			int palette  = (entry >> 13) & 0x03;
			int hflip    = (entry >> 11) & 1;
			int vflip    = (entry >> 12) & 1;
			uint16_t key = (palette << 13) | (vflip << 12)
			             | (hflip << 11) | tile_idx;

			int slot = -1;
			for (int i = 0; i < n_atlas; i++) {
				if (row_keys[i] == key) { slot = i; break; }
			}
			if (slot < 0) {
				if (n_atlas == MAX_SLOTS) {
					/* Atlas full: flush what we have, then this
					 * column starts a fresh batch. */
					FLUSH_ATLAS();
				}
				slot = n_atlas++;
				row_keys[slot] = key;

				const uint8_t *src = ((const uint8_t *)PicoMem.vram)
				                   + (tile_idx << 5);
				uint8_t flipped[32];
				if (hflip || vflip) {
					flip_tile_data(src, flipped, hflip, vflip);
					src = flipped;
				}
				uint8_t pal_base = palette << 4;
				uint8_t *dst = row_atlas + slot * 64;
				for (int r = 0; r < 8; r++) {
					const uint8_t *srow = src + r * 4;
					uint8_t *drow = dst + r * 8;
					for (int b = 0; b < 4; b++) {
						uint8_t bv = srow[b];
						drow[b*2    ] = ((bv >> 4) & 0x0f) | pal_base;
						drow[b*2 + 1] = ( bv       & 0x0f) | pal_base;
					}
				}
			}
			col_slot[col] = (int8_t)slot;
		}

		FLUSH_ATLAS();
		#undef FLUSH_ATLAS
	}
}

/* Draw one pass of the Genesis sprite list.  want_prio selects the
 * priority bucket (0 = low, 1 = high) so we can interleave sprites
 * with the plane passes in the correct Genesis compositing order.
 *
 * Sprite attribute table at (reg[5] & 0x7f) << 8 (u16 index).  Each
 * entry is 4 u16 words:
 *   [0]           Y  : (val & 0x1ff) - 0x80
 *   [1] size+link  :  bits 15..8 = size (V bits 1..0, H bits 3..2
 *                                        of that byte, count - 1),
 *                     bits 6..0 = next-sprite link
 *   [2]        code  :  b15 prio, b14..13 pal, b12 vflip, b11 hflip,
 *                       b10..0 tile index
 *   [3]           X  : (val & 0x1ff) - 0x80
 *
 * Within a sprite, tiles are stored column-major: tile index
 * increases going DOWN first, then RIGHT.  H/V flip is applied via
 * mirrored iteration order.  Offscreen cull is coarse (bbox vs
 * 320x224 visible rect).  Masking sprites (X=0 sentinels) and MD
 * 80/64 hardware limits are honored via the linked-list walk.
 */
static void draw_sprites_rdp(int want_prio, int y_off)
{
	struct PicoVideo *pv = &Pico.video;
	int h40         = (pv->reg[12] & 0x01);
	int max_sprites = h40 ? 80 : 64;
	int table       = pv->reg[5] & 0x7f;
	if (h40) table &= 0x7e;
	table <<= 8;

	int link = 0;
	for (int u = 0; u < max_sprites; u++) {
		const uint16_t *attr = &PicoMem.vram[(table + link*4) & 0x7ffc];
		uint16_t w0 = attr[0], w1 = attr[1];
		uint16_t w2 = attr[2], w3 = attr[3];

		int sy    = ((int)(w0 & 0x1ff)) - 0x80;
		int size  = (w1 >> 8) & 0xff;
		int w_t   = ((size >> 2) & 3) + 1;
		int h_t   = (size & 3) + 1;
		int sx    = ((int)(w3 & 0x1ff)) - 0x80;
		int tile0 = w2 & 0x7ff;
		int pal   = (w2 >> 13) & 3;
		int hflip = (w2 >> 11) & 1;
		int vflip = (w2 >> 12) & 1;
		int prio  = (w2 >> 15) & 1;
		int next  = w1 & 0x7f;

		if (prio != want_prio) goto next;

		int sw = w_t * 8, sh = h_t * 8;
		if (sx + sw <= 0 || sx >= 320) goto next;
		if (sy + sh <= 0 || sy >= 224) goto next;

		for (int tc = 0; tc < w_t; tc++) {
			int scol = hflip ? (w_t - 1 - tc) : tc;
			for (int tr = 0; tr < h_t; tr++) {
				int srow = vflip ? (h_t - 1 - tr) : tr;
				int tile_idx = (tile0 + scol * h_t + srow) & 0x7ff;

				const uint8_t *tile_src = ((const uint8_t *)PicoMem.vram)
				                        + (tile_idx << 5);
				/* Pre-flip the 8x8 pixel data too, otherwise a
				 * flipped sprite would have tiles in the right
				 * order but each tile's pixels still unflipped --
				 * which is what produced the "enemy ships look
				 * wrong" report. */
				static uint8_t flip_buf[32] __attribute__((aligned(8)));
				if (hflip || vflip) {
					flip_tile_data(tile_src, flip_buf, hflip, vflip);
					data_cache_hit_writeback(flip_buf, sizeof(flip_buf));
					tile_src = flip_buf;
				}
				surface_t tile_surf = surface_make((void *)tile_src,
				                                   FMT_CI4, 8, 8, 4);

				rdpq_texparms_t p = { .palette = pal };
				rdpq_tex_upload(TILE0, &tile_surf, &p);

				int psx = sx + tc * 8;
				int psy = y_off + sy + tr * 8;
				rdpq_texture_rectangle(TILE0,
				                       psx, psy, psx + 8, psy + 8, 0, 0);
			}
		}

	next:
		if (next == 0) break;
		link = next;
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
	/* Genesis-style transparency over the backdrop/plane-B we
	 * already drew.  rdpq_set_mode_standard leaves the blender
	 * disabled and the framebuffer-read disabled -- so our
	 * MULTIPLY-style blender had no MEMORY_RGB to work with.
	 * rdpq_mode_antialias enables coverage/read-back and is the
	 * officially supported way to get proper alpha blending to
	 * work (see comment in rdpq_mode_antialias docs).  With it on,
	 * TLUT alpha=0 (color index 0 of each palette bank) blends to
	 * zero opacity -> framebuffer contents preserved. */
	rdpq_mode_antialias(AA_STANDARD);
	rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
	rdpq_mode_alphacompare(255);
	rdpq_mode_filter(FILTER_POINT);
	rdpq_mode_tlut(TLUT_RGBA16);
	rdpq_tex_upload_tlut(tlut, 0, 64);

	/* ---- Draw both planes (B first so A overlays) ---- */
	int h40   = (pv->reg[12] & 0x01);
	int cols  = h40 ? 40 : 32;
	int x_off = h40 ? 0 : 32;
	int y_off = (240 - 224) / 2;

	/* Genesis compositing order:
	 *   backdrop < B-lo < A-lo < sprites-lo < B-hi < A-hi < sprites-hi */
	draw_plane_rdp  (1, 0, cols, x_off, y_off);  /* Plane B low  */
#ifndef N64_RDP_DEBUG_DISABLE_PLANE_A
	draw_plane_rdp  (0, 0, cols, x_off, y_off);  /* Plane A low  */
#endif
#ifndef N64_RDP_DEBUG_DISABLE_SPRITES
	draw_sprites_rdp(   0,              y_off);  /* sprites low  */
#endif
	draw_plane_rdp  (1, 1, cols, x_off, y_off);  /* Plane B high */
#ifndef N64_RDP_DEBUG_DISABLE_PLANE_A
	draw_plane_rdp  (0, 1, cols, x_off, y_off);  /* Plane A high */
#endif
#ifndef N64_RDP_DEBUG_DISABLE_SPRITES
	draw_sprites_rdp(   1,              y_off);  /* sprites high */
#endif
}

#endif /* N64 */
