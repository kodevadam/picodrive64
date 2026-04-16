/*
 * PicoDrive N64 - Standalone main with embedded ROM
 * 16-bit renderer + optimized BGR555->RGBA5551 blit
 */
/* Undef NDEBUG so libdragon's debugf() + debug_init_usblog() are real
 * (not compiled to no-ops). Must come before any libdragon include. */
#ifdef NDEBUG
# undef NDEBUG
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pico/pico_int.h>

#include "../common/input_pico.h"
#include "n64.h"
#include "embedded_rom.h"

#include <rdpq.h>
#include <rdpq_attach.h>
#include <rdpq_mode.h>
#include <rdpq_tex.h>
#include "rsp_audio.h"
#include "rsp_fm.h"

/* Profiling counters (written by pico_cmn.c and draw.c, read here) */
unsigned int __attribute__((used)) prof_68k_ticks = 0;
unsigned int __attribute__((used)) prof_vdp_ticks = 0;
unsigned int __attribute__((used)) prof_vdp_layer_ticks = 0;
unsigned int __attribute__((used)) prof_vdp_sprite_ticks = 0;
unsigned int __attribute__((used)) prof_vdp_final_ticks = 0;


/* Globals expected by PicoDrive core */
char **g_argv;
void *g_screen_ptr;
int g_screen_width  = 320;
int g_screen_height = 240;
int g_screen_ppitch = 320;

static uint16_t __attribute__((aligned(16))) screen_buffer[328 * 240 / 2];
/* Rendered as CI8 (1 byte/pixel) with 328-byte stride:
 *   +  0..7   : HighCol 8-byte left margin (VDP internal, not displayed)
 *   +  8..327 : 320 visible pixels
 * Declared uint16_t to preserve 16-byte alignment guarantees for DMA/RDP,
 * but interpreted as uint8_t everywhere.
 */
#define SCR_PITCH 328

/* Audio: PicoDrive writes 16-bit PCM here each frame.
 * Mono at 11025 Hz = minimum FM synthesis overhead. */
#define SND_RATE 11025
static short __attribute__((aligned(8))) snd_buffer[SND_RATE / 50 + 16];
/* Upmix buffer: mono -> stereo for libdragon (which requires stereo) */
static short __attribute__((aligned(8))) snd_stereo[2 * (SND_RATE / 50 + 16)];

/* Audio health tracking.
 *   total_samples   - samples pushed this second (compared to SND_RATE for drift)
 *   blocked_count   - times audio buffer was full at push time (good: rate limited)
 *   underrun_count  - times audio buffer drained below 1 buffer (BAD: distortion)
 *   latest_fill     - most recent "buffers in flight" value (for per-frame output)
 *
 * libdragon internally has AUDIO_NUM_BUFFERS buffers.  audio_can_write()
 * returns how many are currently free/drained.  0 = all full (blocking push).
 * Full count == NUM_BUFFERS means the DMA drained everything while we weren't
 * feeding it = underrun = audible clicks/pops. */
static unsigned int snd_total_samples = 0;
static unsigned int snd_blocked_count = 0;
static unsigned int snd_underrun_count = 0;
static unsigned int snd_latest_fill = 0;  /* buffers in flight when we pushed */

#define AUDIO_NUM_BUFFERS 4

static void write_sound(int len)
{
	int nsamples = len / 2;
	if (nsamples <= 0) return;
	for (int i = 0; i < nsamples; i++) {
		snd_stereo[i*2]   = snd_buffer[i];
		snd_stereo[i*2+1] = snd_buffer[i];
	}
	int free_bufs = audio_can_write();
	if (free_bufs <= 0)
		snd_blocked_count++;
	/* Underrun: hardware has fully drained - playback gap audible. */
	if (free_bufs >= AUDIO_NUM_BUFFERS)
		snd_underrun_count++;
	/* In-flight = total - free (for reporting). */
	snd_latest_fill = (unsigned)(AUDIO_NUM_BUFFERS - free_bufs);
	snd_total_samples += nsamples;
	audio_push(snd_stereo, nsamples, true);
}

/* Frame skip: 0=none, 1=skip 1, 2=skip 2 */
static int frame_count = 0;
#define FRAME_SKIP 1

/* Precomputed BGR555 -> RGBA5551 lookup table (32768 entries = 64KB) */
static uint16_t bgr555_to_rgba5551[32768];

static void init_color_lut(void)
{
	for (int i = 0; i < 32768; i++) {
		uint16_t r = (i      ) & 0x1f;
		uint16_t g = (i >>  5) & 0x1f;
		uint16_t b = (i >> 10) & 0x1f;
		bgr555_to_rgba5551[i] = (r << 11) | (g << 6) | (b << 1) | 1;
	}
}

/* Fast blit using LUT - one table lookup per pixel, no math */
/* RGBA5551 palette cache for 8-bit alt renderer */
static uint16_t pal_rgba5551[256];

static void update_palette(void)
{
	unsigned short *src = Pico.est.HighPal;
	for (int i = 0; i < 256; i++) {
		uint16_t c = src[i];
		uint16_t r = (c      ) & 0x1f;
		uint16_t g = (c >>  5) & 0x1f;
		uint16_t b = (c >> 10) & 0x1f;
		pal_rgba5551[i] = (r << 11) | (g << 6) | (b << 1) | 1;
	}
}

static void blit_frame(surface_t *fb)
{
	uint16_t *dst = (uint16_t *)fb->buffer;
	int h = g_screen_height;
	int w = g_screen_width;
	int y_off = (240 - h) / 2;

	if (y_off > 0)
		memset(dst, 0, 320 * 240 * 2);

	/* Direct 8-bit HighCol -> RGBA5551 via palette.
	 * HighCol is the per-scanline 8-bit indexed buffer (328 bytes wide,
	 * 8-pixel left margin). Each byte is a palette index.
	 * We skip FinalizeLine entirely (saves 38% of VDP time).
	 * HighCol pointer advances per line during rendering, so we use
	 * the base from est and compute per-line offset. */
	/* BGR555 -> RGBA5551 via LUT */
	uint16_t *src = screen_buffer;
	for (int y = 0; y < h; y++) {
		uint16_t *s = &src[y * 320];
		uint16_t *d = &dst[(y + y_off) * 320];
		for (int x = 0; x < w; x += 4) {
			d[x]   = bgr555_to_rgba5551[s[x]   & 0x7fff];
			d[x+1] = bgr555_to_rgba5551[s[x+1] & 0x7fff];
			d[x+2] = bgr555_to_rgba5551[s[x+2] & 0x7fff];
			d[x+3] = bgr555_to_rgba5551[s[x+3] & 0x7fff];
		}
	}
}

int main(int argc, char *argv[])
{
	unsigned char *rom_copy;

	g_argv = argv;
	g_screen_ptr = screen_buffer;

	debug_init_usblog();
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
	rdpq_init();
	audio_init(SND_RATE, 4);
	rsp_audio_init();  /* Register RSP audio overlay with rspq */
	rsp_fm_init();     /* Register RSP FM synthesis overlay */
	joypad_init();

	/* Boot message */
	console_init();
	console_set_render_mode(RENDER_MANUAL);
	printf("\n\n  PicoDrive64\n\n");
	printf("  ROM: %s (%d KB)\n", EMBEDDED_ROM_NAME, EMBEDDED_ROM_SIZE / 1024);
	printf("  RAM: %d KB\n", get_memory_size() / 1024);
	printf("  Initializing...\n");
	console_render();

	PicoInit();
	init_color_lut();

	/* Enable sound: FM + Z80 + mono at 11025 Hz.
	 * FM uses decomposed 1KB tables (L1 cache friendly) instead
	 * of the default 208KB ym_tl_tab. */
	PicoIn.opt  = POPT_EN_FM | POPT_EN_Z80;
	PicoIn.opt |= POPT_DIS_VDP_FIFO;
	PicoIn.opt |= POPT_DIS_SPRITE_LIM;
	/* Enable 68K idle detection - skips cycles in idle loops */
	PicoIn.sndRate = SND_RATE;
	PicoIn.sndOut = snd_buffer;
	PicoIn.writeSound = write_sound;

	rom_copy = (unsigned char *)malloc(EMBEDDED_ROM_SIZE + 4);
	if (!rom_copy) {
		printf("  ERROR: Out of memory!\n");
		console_render();
		for (;;) {}
	}
	memcpy(rom_copy, embedded_rom_data, EMBEDDED_ROM_SIZE);

	if (PicoCartInsert(rom_copy, EMBEDDED_ROM_SIZE, NULL)) {
		printf("  ERROR: PicoCartInsert failed!\n");
		console_render();
		for (;;) {}
	}

	PicoPower();
	PicoReset();
	PicoLoopPrepare();

	/* 8-bit indexed output. Pitch=320 keeps tile rendering in
	 * PicoDrive's internal 328-byte HighCol (hot in L1 cache).
	 * FinalizeLine copies 320 bytes/line - cheaper than cache misses
	 * from rendering into a full frame buffer. */
	PicoDrawSetOutFormat(PDF_8BIT, 0);
	/* Pitch >= 328 triggers the no-copy mode in draw.c: HighCol is
	 * set to point directly into screen_buffer, so FinalizeLine8bit
	 * skips its per-scanline 320-byte blockcpy. ~14-18% VDP savings. */
	PicoDrawSetOutBuf(screen_buffer, SCR_PITCH);

	printf("  Running!\n");
	console_render();
	for (volatile int i = 0; i < 2000000; i++) {}

	console_close();
	display_close();
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);

	/* FPS counter + profiling */
	int fps_count = 0;
	int fps_display = 0;
	unsigned int fps_timer = timer_ticks();
	char fps_buf[40];

	/* Detailed profiling accumulators (ticks per second, for on-screen) */
	unsigned int prof_68k_acc = 0, prof_vdp_acc = 0;
	unsigned int prof_snd_acc = 0;
	unsigned int prof_frame_acc = 0;
	int prof_68k_pct = 0, prof_vdp_pct = 0, prof_snd_pct = 0;

	/* Target frame time at 60 FPS for percentage calculations.
	 * timer_ticks runs at COUNTS_PER_SECOND, so one 60Hz frame is
	 * COUNTS_PER_SECOND/60 ticks. Values >100% mean the frame
	 * overran its budget (we can't hit 60 FPS). */
	const unsigned int target_frame_ticks = TIMER_TICKS(1000000 / 60);

	/* Frametime min/max tracking over the 1-second window. */
	unsigned int ft_min_us = 0, ft_max_us = 0;

	/* Main loop - pipelined: RDP blit runs in parallel with skip frame */
	surface_t *pending_fb = NULL;

	for (;;) {
		PicoIn.skipFrame = (frame_count < FRAME_SKIP) ? 1 : 0;

		prof_68k_ticks = prof_vdp_ticks = 0;
		prof_vdp_layer_ticks = prof_vdp_sprite_ticks = prof_vdp_final_ticks = 0;
		unsigned int t0 = timer_ticks();

		/* Skip Z80 on non-display frames to save ~10 FPS */
		unsigned int saved_opt = PicoIn.opt;
		if (PicoIn.skipFrame)
			PicoIn.opt &= ~POPT_EN_Z80;
		PicoFrame();
		PicoIn.opt = saved_opt;
		/* During skip frames, the RDP blit from the previous render
		 * frame runs in parallel with PicoFrame(). Skip frames don't
		 * touch screen_buffer (no VDP), so no data race. */
		unsigned int t1 = timer_ticks();
		fps_count++;

		/* Per-frame profile line: instantaneous FPS, frametime, + budget %.
		 * tot>100% on any category means that category alone cannot hit 60 FPS. */
		unsigned int now = t1;
		unsigned int frame_time = t1 - t0;
		unsigned int snd_time = 0;
		if (frame_time > prof_68k_ticks + prof_vdp_ticks)
			snd_time = frame_time - prof_68k_ticks - prof_vdp_ticks;

		/* frame_time_us for ms conversion and instantaneous FPS. */
		unsigned int frame_us = (unsigned int)TIMER_MICROS_LL(frame_time);
		unsigned int inst_fps_x10 = frame_us ? (unsigned int)(10000000ULL / frame_us) : 0;

		/* Percentages of 60Hz frame budget (x100 for 2-decimal print). */
		unsigned int cpu_pct100 = (uint64_t)prof_68k_ticks * 10000 / target_frame_ticks;
		unsigned int draw_pct100 = (uint64_t)prof_vdp_ticks * 10000 / target_frame_ticks;
		unsigned int snd_pct100 = (uint64_t)snd_time * 10000 / target_frame_ticks;
		unsigned int total_pct100 = (uint64_t)frame_time * 10000 / target_frame_ticks;

		/* VDP sub-breakdown: L (layers) / S (sprites) / F (finalize) as
		 * percentages of VDP total.  Useful to target specific hot paths. */
		unsigned int vt = prof_vdp_layer_ticks + prof_vdp_sprite_ticks
		                + prof_vdp_final_ticks;
		unsigned int lp = vt ? (unsigned)((uint64_t)prof_vdp_layer_ticks*100/vt) : 0;
		unsigned int sp = vt ? (unsigned)((uint64_t)prof_vdp_sprite_ticks*100/vt) : 0;
		unsigned int fp = vt ? (unsigned)((uint64_t)prof_vdp_final_ticks*100/vt) : 0;

		unsigned int ft_ms_x10 = frame_us / 100;

		debugf("[F] fps=%u.%u ft=%u.%ums cpu:%u vdp:%u[L%uS%uF%u] snd:%u tot:%u aud:%s PC:%06x\n",
			inst_fps_x10/10, inst_fps_x10%10,
			ft_ms_x10/10, ft_ms_x10%10,
			cpu_pct100/100,
			draw_pct100/100, lp, sp, fp,
			snd_pct100/100,
			total_pct100/100,
			(snd_latest_fill == 0) ? "drain"
				: (snd_latest_fill >= AUDIO_NUM_BUFFERS) ? "full" : "ok",
			(unsigned)SekPc & 0xFFFFFF);

		/* Accumulate for per-second summary. */
		prof_frame_acc += frame_time;
		prof_68k_acc += prof_68k_ticks;
		prof_vdp_acc += prof_vdp_ticks;
		prof_snd_acc += snd_time;
		if (frame_us > ft_max_us) ft_max_us = frame_us;
		if (frame_us < ft_min_us || ft_min_us == 0) ft_min_us = frame_us;

		if (TICKS_TO_MS(now - fps_timer) >= 1000) {
			fps_display = fps_count;
			fps_count = 0;
			if (prof_frame_acc > 0) {
				prof_68k_pct = (int)((uint64_t)prof_68k_acc * 100 / prof_frame_acc);
				prof_vdp_pct = (int)((uint64_t)prof_vdp_acc * 100 / prof_frame_acc);
				prof_snd_pct = (int)((uint64_t)prof_snd_acc * 100 / prof_frame_acc);
			}
			/* Fixed drift calc: signed subtraction (avoids unsigned
			 * underflow when we're slow, which was printing +3e7%). */
			int32_t expected = SND_RATE;
			int32_t actual = (int32_t)snd_total_samples;
			int drift_x100 = (int)((int64_t)(actual - expected) * 10000 / expected);

			unsigned int frames_safe = fps_display ? fps_display : 1;
			unsigned int ft_avg_us = (unsigned int)(TIMER_MICROS_LL(prof_frame_acc) / frames_safe);
			unsigned int avg_ms_x10 = ft_avg_us / 100;
			unsigned int min_ms_x10 = ft_min_us / 100;
			unsigned int max_ms_x10 = ft_max_us / 100;

			debugf("[SEC] fps=%d ft_avg=%u.%ums ft_min=%u.%u ft_max=%u.%u "
			       "audio=%d/%u(%+d.%02d%%) blk=%u undr=%u\n",
				fps_display,
				avg_ms_x10/10, avg_ms_x10%10,
				min_ms_x10/10, min_ms_x10%10,
				max_ms_x10/10, max_ms_x10%10,
				actual, (unsigned)expected,
				drift_x100/100, (drift_x100<0?-drift_x100:drift_x100)%100,
				snd_blocked_count, snd_underrun_count);

			prof_68k_acc = prof_vdp_acc = prof_snd_acc = prof_frame_acc = 0;
			snd_total_samples = 0;
			snd_blocked_count = 0;
			snd_underrun_count = 0;
			ft_min_us = ft_max_us = 0;
			fps_timer = now;
		}

		if (++frame_count > FRAME_SKIP) {
			frame_count = 0;

			/* Finish previous RDP blit (ran during skip frame) */
			if (pending_fb) {
				rspq_wait();
				data_cache_hit_invalidate(pending_fb->buffer, 320 * 240 * 2);
				graphics_set_color(
					graphics_make_color(0xFF,0xFF,0xFF,0xFF),
					graphics_make_color(0,0,0,0xFF));
				sprintf(fps_buf, "%dF 68k%d V%d S%d",
					fps_display, prof_68k_pct, prof_vdp_pct, prof_snd_pct);
				graphics_draw_text(pending_fb, 4, 4, fps_buf);
				data_cache_hit_writeback(pending_fb->buffer, 320 * 24 * 2);
				display_show(pending_fb);
				pending_fb = NULL;
			}

			/* Start new RDP blit (non-blocking) */
			surface_t *fb = display_get();
			if (fb && fb->buffer) {
				int h = g_screen_height;
				int w = g_screen_width;
				int y_off = (240 - h) / 2;

				if (Pico.m.dirtyPal) {
					PicoDrawUpdateHighPal();
					update_palette();
				}

				/* Writeback the full rendered area (328-stride, h rows).
				 * Round to multiple of 16 for cache line alignment. */
				unsigned wb_bytes = (SCR_PITCH * h + 15) & ~15;
				data_cache_hit_writeback(screen_buffer, wb_bytes);
				data_cache_hit_writeback(pal_rgba5551, sizeof(pal_rgba5551));

				/* Source surface: skip the 8-byte left margin, stride 328. */
				surface_t ci8_surf = surface_make(
					(uint8_t *)screen_buffer + 8,
					FMT_CI8, w, h, SCR_PITCH);

				rdpq_attach(fb, NULL);
				rdpq_set_mode_standard();
				rdpq_mode_filter(FILTER_POINT);
				rdpq_mode_tlut(TLUT_RGBA16);
				rdpq_tex_upload_tlut(pal_rgba5551, 0, 256);
				rdpq_tex_blit(&ci8_surf, 0, y_off, NULL);
				rdpq_detach();  /* Non-blocking! RDP runs during next skip frame */
				pending_fb = fb;
			}
		}

		joypad_poll();
		joypad_buttons_t btns = joypad_get_buttons(JOYPAD_PORT_1);
		joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
		unsigned int pad = 0;

		if (btns.d_up || inputs.stick_y > 20)   pad |= 1 << GBTN_UP;
		if (btns.d_down || inputs.stick_y < -20) pad |= 1 << GBTN_DOWN;
		if (btns.d_left || inputs.stick_x < -20) pad |= 1 << GBTN_LEFT;
		if (btns.d_right || inputs.stick_x > 20) pad |= 1 << GBTN_RIGHT;
		if (btns.a)     pad |= 1 << GBTN_B;
		if (btns.b)     pad |= 1 << GBTN_C;
		if (btns.z)     pad |= 1 << GBTN_A;
		if (btns.start) pad |= 1 << GBTN_START;
		if (btns.l)     pad |= 1 << GBTN_X;
		if (btns.r)     pad |= 1 << GBTN_Z;
		if (btns.c_right) pad |= 1 << GBTN_Y;

		PicoIn.pad[0] = pad;
	}
}

/* Platform stubs */
void emu_video_mode_change(int start_line, int line_count, int start_col, int col_count)
{
	g_screen_width = col_count;
	g_screen_height = line_count;
}

void emu_32x_startup(void) { }
void lprintf(const char *fmt, ...) { }

void *plat_mmap(unsigned long addr, size_t size, int need_exec, int is_fixed)
{ return calloc(1, size); }
void *plat_mremap(void *ptr, size_t oldsize, size_t newsize)
{ return realloc(ptr, newsize); }
void plat_munmap(void *ptr, size_t size)
{ free(ptr); }
void *plat_mem_get_for_drc(size_t size) { return NULL; }
int plat_mem_set_exec(void *ptr, size_t size) { return 0; }

void cache_flush_d_inval_i(void *start, void *end)
{
	size_t len = (char *)end - (char *)start;
	if (len > 0) {
		data_cache_hit_writeback(start, len);
		inst_cache_hit_invalidate(start, len);
	}
}

int  mp3_get_bitrate(void *f, int size) { return 0; }
void mp3_start_play(void *f, int pos) { }
void mp3_update(s32 *buffer, int length, int stereo) { }
