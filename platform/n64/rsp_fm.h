#ifndef RSP_FM_H
#define RSP_FM_H

#include <stdint.h>

/* Per-channel state sent to RSP for FM synthesis */
struct rsp_fm_chan {
	uint32_t phase[4];     /* operator phase counters */
	uint32_t incr[4];      /* phase increments */
	uint16_t vol_out[4];   /* envelope levels (attenuation) */
	uint8_t  algo;         /* algorithm 0-7 */
	uint8_t  fb_shift;     /* feedback shift (0=off, 1-7) */
	uint8_t  pan;          /* bit0=mono, bit4=R, bit5=L */
	uint8_t  enabled;      /* channel active */
	int32_t  op1_out;      /* operator 1 feedback state */
	int32_t  mem;          /* algorithm delay memory */
}; /* 56 bytes */

/* Full state for all 6 FM channels */
struct rsp_fm_state {
	struct rsp_fm_chan ch[6];  /* 336 bytes */
	uint16_t num_samples;
	uint16_t pad;
}; /* 340 bytes */

void rsp_fm_init(void);
void rsp_fm_render(struct rsp_fm_state *state, int32_t *out_buf);

#endif
