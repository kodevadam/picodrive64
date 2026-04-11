/*
 * RSP-assisted tile rendering for PicoDrive N64
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <libdragon.h>
#include "rsp_render.h"

DEFINE_RSP_UCODE(rsp_tiles);

/* DMEM offsets matching rsp_tiles.S */
#define DMEM_NUM_TILES    0x000
#define DMEM_RDRAM_SRC    0x004
#define DMEM_RDRAM_DST    0x008
#define DMEM_STATUS       0x00C
#define DMEM_TILE_INPUT   0x010
#define DMEM_PAL_INFO     0x210
#define DMEM_TILE_OUTPUT  0x290

/* Aligned buffers for DMA */
static uint32_t __attribute__((aligned(16))) tile_words_buf[128];
static uint8_t  __attribute__((aligned(16))) palette_buf[128];
static uint8_t  __attribute__((aligned(16))) output_buf[1024];

static int rsp_inited = 0;

void rsp_render_init(void)
{
	rsp_init();
	rsp_inited = 1;
}

void rsp_render_tiles(uint32_t *tile_words, uint8_t *palette_bases, int count)
{
	if (!rsp_inited || count <= 0 || count > 128)
		return;

	/* Copy to aligned buffers */
	memcpy(tile_words_buf, tile_words, count * 4);
	memcpy(palette_buf, palette_bases, count);

	/* Flush to RDRAM */
	data_cache_hit_writeback(tile_words_buf, count * 4);
	data_cache_hit_writeback(palette_buf, count);

	/* Load RSP ucode */
	rsp_load(&rsp_tiles);

	/* Write control to DMEM */
	SP_DMEM[DMEM_NUM_TILES / 4] = count;
	SP_DMEM[DMEM_RDRAM_SRC / 4] = PhysicalAddr(tile_words_buf);
	SP_DMEM[DMEM_RDRAM_DST / 4] = PhysicalAddr(output_buf);
	SP_DMEM[DMEM_STATUS / 4] = 0;

	/* Copy palette info to DMEM */
	for (int i = 0; i < (count + 3) / 4; i++)
		SP_DMEM[(DMEM_PAL_INFO / 4) + i] = ((uint32_t *)palette_buf)[i];

	/* Run RSP */
	rsp_run_async();
}

uint8_t *rsp_render_get_output(void)
{
	return output_buf;
}

void rsp_render_wait(void)
{
	if (!rsp_inited)
		return;
	rsp_wait();
	data_cache_hit_invalidate(output_buf, sizeof(output_buf));
}
