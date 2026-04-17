#ifndef RSP_FM_H
#define RSP_FM_H

#include <stdint.h>

/*
 * RSP FM synthesis + envelope advancement.
 *
 * Algorithm routing parameters (computed by CPU, consumed by RSP).
 *
 * c1_mod:    modulation source for C1 (SLOT2): 0=zero, 1=op1_out>>16
 * m2_mod:    modulation source for M2 (SLOT3): 0=zero, 1=prev_mem
 * c2_mod:    modulation source for C2 (SLOT4):
 *              0=zero, 1=M2_out, 2=op1s+M2, 3=mem+M2, 4=op1s
 * out_flags: which operators contribute to output:
 *              bit0=op1s, bit1=C1, bit2=M2, bit3=C2
 * mem_src:   what writes to delay memory:
 *              0=none, 1=C1, 2=op1s+C1, 3=op1s
 *
 * Envelope state (new):
 *   Per slot the RSP runs the YM2612 EG state machine:
 *     volume, state, tl, sl, ssg[n], eg_pack[4] (indexed by state-1).
 *   The CPU seeds these before each render call and reads back the
 *   updated volume/state/vol_out/ssgn after the RSP completes.
 */

/* Per-channel state sent to RSP for FM synthesis + envelope */
struct rsp_fm_chan {
	/* ==== Synthesis path (existing) ==== */
	uint32_t phase[4];     /* 0-15: operator phase counters */
	uint32_t incr[4];      /* 16-31: phase increments */
	uint16_t vol_out[4];   /* 32-39: envelope output (tl + attenuation) */
	uint8_t  c1_mod;       /* 40: C1 modulation source */
	uint8_t  fb_shift;     /* 41: feedback shift (0=off, 1-7) */
	uint8_t  m2_mod;       /* 42: M2 modulation source */
	uint8_t  enabled;      /* 43: channel active */
	int32_t  op1_out;      /* 44-47: operator 1 feedback state */
	int32_t  mem;          /* 48-51: algorithm delay memory */
	uint8_t  c2_mod;       /* 52: C2 modulation type */
	uint8_t  out_flags;    /* 53: output contribution flags */
	uint8_t  mem_src;      /* 54: delay memory source */
	uint8_t  pad55;        /* 55 */

	/* ==== Envelope state (new, 104 bytes) ==== */
	int16_t  eg_volume[4]; /* 56-63: EG accumulator per slot */
	uint16_t eg_tl[4];     /* 64-71: total level per slot */
	uint16_t eg_sl[4];     /* 72-79: sustain level per slot */
	uint8_t  eg_state[4];  /* 80-83: EG phase (0=OFF..4=ATT) */
	uint8_t  eg_ssg[4];    /* 84-87: SSG-EG mode bits */
	uint8_t  eg_ssgn[4];   /* 88-91: SSG-EG inverted flag */
	uint8_t  eg_upd_cnt;   /* 92: shared 3:2 subdivider for eg_cnt ticks */
	uint8_t  eg_ssg_en;    /* 93: (ssg_mask[ch] != 0) && (ST.flags & ST_SSG) */
	uint8_t  pad94;        /* 94 */
	uint8_t  pad95;        /* 95 */
	uint32_t eg_pack[16];  /* 96-159: per-slot eg_pack[state-1], 4*4 words */
}; /* 160 bytes */

/* Full state for all 6 FM channels + envelope globals */
struct rsp_fm_state {
	struct rsp_fm_chan ch[6];  /* 960 bytes */
	uint16_t num_samples;      /* 960 */
	uint16_t eg_ticks;         /* 962: batch-precomputed eg_cnt tick count */
	uint32_t eg_cnt;           /* 964: envelope sample counter (read/write) */
	uint32_t pad968;           /* 968 */
	uint32_t pad972;           /* 972 */
}; /* 976 bytes, 16-byte aligned */

void rsp_fm_init(void);
void rsp_fm_render(struct rsp_fm_state *state, int32_t *out_buf);

#endif
