/*
 * RSP-assisted palette conversion for PicoDrive N64
 *
 * Converts the full 320×224 frame from 8-bit indexed (HighCol/screen_buffer)
 * to 16-bit RGBA5551 (N64 display format) using the RSP while the CPU
 * runs the next frame's emulation.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <libdragon.h>
#include "rsp_render.h"

DEFINE_RSP_UCODE(rsp_tiles);

/* DMEM offsets matching rsp_tiles.S */
#define DMEM_RDRAM_SRC    0x000
#define DMEM_RDRAM_DST    0x004
#define DMEM_NUM_PIXELS   0x008
#define DMEM_STATUS       0x00C
#define DMEM_PALETTE      0x010

static int rsp_inited = 0;

void rsp_render_init(void)
{
	rsp_init();
	rsp_inited = 1;
}

/*
 * Start RSP palette conversion: 8-bit indexed -> 16-bit RGBA5551.
 * src: 8-bit pixel data in RDRAM (must be cache-flushed)
 * dst: 16-bit output in RDRAM (display framebuffer)
 * palette_rgba5551: 256-entry RGBA5551 palette (must be cache-flushed)
 * num_pixels: total pixel count (e.g., 320*224)
 */
void rsp_render_start(void *src, void *dst,
                      uint16_t *palette_rgba5551, int num_pixels)
{
	if (!rsp_inited || num_pixels <= 0)
		return;

	/* Load RSP ucode */
	rsp_load(&rsp_tiles);

	/* Write control to DMEM */
	SP_DMEM[DMEM_RDRAM_SRC / 4] = PhysicalAddr(src);
	SP_DMEM[DMEM_RDRAM_DST / 4] = PhysicalAddr(dst);
	SP_DMEM[DMEM_NUM_PIXELS / 4] = num_pixels;
	SP_DMEM[DMEM_STATUS / 4] = 0;

	/* Copy palette to DMEM (256 entries × 2 bytes = 512 bytes) */
	for (int i = 0; i < 128; i++)
		SP_DMEM[(DMEM_PALETTE / 4) + i] = ((uint32_t *)palette_rgba5551)[i];

	/* Start RSP */
	rsp_run_async();
}

void rsp_render_wait(void)
{
	if (!rsp_inited)
		return;
	rsp_wait();
}

uint8_t *rsp_render_get_output(void)
{
	return NULL; /* output goes directly to display buffer */
}
