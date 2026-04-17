/*
 * RDP-based tile renderer for PicoDrive N64 (draft / scaffolding).
 *
 * Goal: replace draw2.c's per-tile CPU blit (DrawLayerFull /
 * DrawTilesFromCacheF / DrawSpriteFull -> TileX*Y*) with RDP
 * texture-rectangle draws, backed by TMEM-cached Genesis tiles and
 * TLUT palettes.  Expected win: layer rendering drops from ~8-9 ms
 * to ~1-2 ms per frame, unlocking a true 60 fps budget.
 *
 * Status: STUB.  PicoFrameFullRDP() currently delegates to the
 * existing CPU PicoFrameFull() so the runtime toggle
 * (n64_use_rdp_tiles) is safe to enable without regressing.
 *
 * Build plan (incremental, each step testable):
 *   1. Toggle scaffold in place (this commit).  Flag off -> CPU;
 *      flag on -> still CPU, but via the new entry.
 *   2. Palette upload path: convert Genesis CRAM -> 4 x 16-color
 *      RGBA16 TLUTs in RAM, keep them writeback'd.
 *   3. Tile cache in TMEM: CI4 format, upload 8x8 tile data on
 *      demand.  Start with a flush-per-tile slow mode to validate
 *      RDP draw correctness on a single plane, then batch.
 *   4. Plane B layer via rdpq_texture_rectangle; plane A lo, sprites
 *      lo, plane B hi cache, plane A hi cache, sprites hi.
 *   5. Window / hscroll variants last.
 *   6. Compare against CPU path via toggle + profile counters.
 */
#ifdef N64
#include <libdragon.h>
#include <rdpq.h>
#include <rdpq_mode.h>
#include <rdpq_tex.h>

#include <pico/pico_int.h>

/* Defined in draw2.c — the existing proven CPU renderer. */
extern void PicoFrameFull(void);

void PicoFrameFullRDP(void)
{
	/* Stub: just call the CPU renderer.  When we start implementing
	 * the RDP path, wrap per-phase code behind further sub-toggles
	 * so partial implementations can fall back per-plane. */
	PicoFrameFull();
}

#endif /* N64 */
