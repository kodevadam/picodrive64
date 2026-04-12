/*
 * RSP FM Synthesis - CPU side
 *
 * Extracts YM2612 channel state from PicoDrive, sends to RSP
 * overlay for parallel sample generation. CPU handles Z80 +
 * envelope state machine, RSP handles the per-sample hot loop.
 */
#include <stdio.h>
#include <string.h>
#include <libdragon.h>
#include "rsp_fm.h"
#include <pico/pico_int.h>
#include <pico/sound/ym2612.h>

DEFINE_RSP_UCODE(rsp_fm);

static uint32_t fm_ovl_id;
static int tables_uploaded = 0;

/* Decomposed tables from ym2612.c */
extern UINT16 ym_tl_tab2[];

/* sin_tab is static in ym2612.c, we need our own copy */
static uint16_t rsp_sin_tab[256] __attribute__((aligned(16)));
static uint16_t rsp_exp_tab[256] __attribute__((aligned(16)));

enum {
	CMD_FM_RENDER = 0,
	CMD_FM_INIT_TABLES = 1,
};

void rsp_fm_init(void)
{
	fm_ovl_id = rspq_overlay_register(&rsp_fm);
}

static void upload_tables(void)
{
	if (tables_uploaded) return;

	/* Build sin_tab: copy from ym2612's internal table.
	 * We can't access ym_sin_tab directly (it's static),
	 * so reconstruct it from the init_tables formula. */
	for (int i = 0; i < 256; i++) {
		double m = sin(((i*2)+1) * M_PI / 1024.0);
		double o;
		int n;
		if (m > 0.0)
			o = 8*log(1.0/m)/log(2.0);
		else
			o = 8*log(-1.0/m)/log(2.0);
		o = o / (128.0/1024.0 / 4);
		n = (int)(2.0*o);
		if (n&1) n = (n>>1)+1;
		else     n = n>>1;
		rsp_sin_tab[i] = n;
	}

	/* Build exp_tab: base level of ym_tl_tab2 (first 256 entries) */
	for (int i = 0; i < 256; i++) {
		rsp_exp_tab[i] = ym_tl_tab2[i];
	}

	/* Flush and upload to RSP DMEM */
	data_cache_hit_writeback(rsp_sin_tab, sizeof(rsp_sin_tab));
	data_cache_hit_writeback(rsp_exp_tab, sizeof(rsp_exp_tab));

	rspq_write(fm_ovl_id, CMD_FM_INIT_TABLES,
		   0,
		   PhysicalAddr(rsp_sin_tab),
		   PhysicalAddr(rsp_exp_tab));
	rspq_flush();
	rspq_wait();

	tables_uploaded = 1;
}

/* Extract channel state from PicoDrive's YM2612 into RSP format */
static struct rsp_fm_state __attribute__((aligned(8))) fm_state;

void rsp_fm_test_noop(void)
{
	/* Send a no-op command to test overlay switch */
	rspq_write(fm_ovl_id, CMD_FM_RENDER, 0, 0, 0);
	rspq_flush();
	rspq_wait();
}

void rsp_fm_render(struct rsp_fm_state *state, int32_t *out_buf)
{
	upload_tables();

	data_cache_hit_writeback(state, (sizeof(*state) + 15) & ~15);

	rspq_write(fm_ovl_id, CMD_FM_RENDER,
		   state->num_samples,
		   PhysicalAddr(state),
		   PhysicalAddr(out_buf));
	rspq_flush();
}
