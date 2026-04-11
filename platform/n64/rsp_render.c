/*
 * RSP-assisted tile rendering for PicoDrive N64
 */
#include <stdio.h>
#include <string.h>
#include <libdragon.h>
#include "rsp_render.h"

DEFINE_RSP_UCODE(rsp_tiles);

/* DMEM addresses matching rsp_tiles.S layout */
#define DMEM_TILE_INPUT   0x000
#define DMEM_PALETTE_INFO 0x200
#define DMEM_TILE_OUTPUT  0x280
#define DMEM_NUM_TILES    0x3F0
#define DMEM_RDRAM_SRC    0x3F4
#define DMEM_RDRAM_DST    0x3F8
#define DMEM_STATUS       0x3FC

/* Aligned buffers for DMA */
static u32 __attribute__((aligned(16))) tile_words_buf[128];
static u8  __attribute__((aligned(16))) palette_buf[128];
static u8  __attribute__((aligned(16))) output_buf[1024];

static int rsp_initialized = 0;

void rsp_render_init(void)
{
	rsp_init();
	rsp_initialized = 1;
}

void rsp_render_tiles_async(const u32 *tile_words, const u8 *palette_bases,
                            u8 *output, int count)
{
	if (!rsp_initialized || count <= 0 || count > 128)
		return;

	/* Copy input to aligned buffers */
	memcpy(tile_words_buf, tile_words, count * 4);
	memcpy(palette_buf, palette_bases, count);

	/* Flush input buffers to RDRAM */
	data_cache_hit_writeback(tile_words_buf, count * 4);
	data_cache_hit_writeback(palette_buf, count);

	/* Load microcode */
	rsp_load(&rsp_tiles);

	/* Write control data to DMEM */
	SP_DMEM[DMEM_NUM_TILES / 4] = count;
	SP_DMEM[DMEM_RDRAM_SRC / 4] = PhysicalAddr(tile_words_buf);
	SP_DMEM[DMEM_RDRAM_DST / 4] = PhysicalAddr(output_buf);
	SP_DMEM[DMEM_STATUS / 4] = 0;

	/* Copy palette info directly to DMEM */
	for (int i = 0; i < (count + 3) / 4; i++)
		SP_DMEM[(DMEM_PALETTE_INFO / 4) + i] = ((u32 *)palette_buf)[i];

	/* Start RSP */
	rsp_run_async();
}

void rsp_render_wait(void)
{
	if (!rsp_initialized)
		return;
	rsp_wait();

	/* Invalidate output cache so CPU reads fresh data */
	data_cache_hit_invalidate(output_buf, sizeof(output_buf));
}
