/*
 * PicoDrive N64 - Standalone main with embedded ROM
 * Uses RDP hardware for palette lookup and framebuffer blit
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pico/pico_int.h>

#include "../common/input_pico.h"
#include "n64.h"
#include "embedded_rom.h"

/* Globals expected by PicoDrive core */
char **g_argv;
void *g_screen_ptr;
int g_screen_width  = 320;
int g_screen_height = 240;
int g_screen_ppitch = 320;

static uint16_t __attribute__((aligned(16))) screen_buffer[320 * 240];

/* RGBA5551 palette for RDP TLUT - aligned for DMA */
static uint16_t __attribute__((aligned(8))) rdp_palette[256];

/* Frame skip */
static int frame_count = 0;
#define FRAME_SKIP 1

/* Convert PicoDrive BGR555 palette to N64 RGBA5551 for RDP TLUT */
static void update_rdp_palette(void)
{
	unsigned short *src = Pico.est.HighPal;
	for (int i = 0; i < 256; i++) {
		uint16_t c = src[i];
		uint16_t r = (c      ) & 0x1f;
		uint16_t g = (c >>  5) & 0x1f;
		uint16_t b = (c >> 10) & 0x1f;
		rdp_palette[i] = (r << 11) | (g << 6) | (b << 1) | 1;
	}
}

/* Blit using RDP hardware: CI8 texture + TLUT palette lookup */
static void rdp_blit_frame(surface_t *fb)
{
	unsigned char *draw2fb = Pico.est.Draw2FB;
	int h = g_screen_height;
	int w = g_screen_width;
	int stride = 328; /* Draw2FB stride (LINE_WIDTH in draw2.c) */
	int y_off = (240 - h) / 2;
	unsigned char *pixels = draw2fb + stride * 8 + 8; /* skip 8-line/8-pixel border */

	/* Flush CPU data cache so RDP can DMA the pixel data */
	data_cache_hit_writeback(pixels, stride * h);

	/* Create a surface wrapping PicoDrive's 8-bit framebuffer */
	surface_t emu_surf = surface_make(pixels, FMT_CI8, w, h, stride);

	/* Attach RDP to the display framebuffer */
	rdpq_attach(fb, NULL);

	/* Clear if there are vertical borders */
	if (y_off > 0) {
		rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
		rdpq_fill_rectangle(0, 0, 320, y_off);
		rdpq_fill_rectangle(0, 240 - y_off, 320, 240);
	}

	/* Upload palette to RDP TLUT */
	rdpq_tex_upload_tlut(rdp_palette, 0, 256);

	/* Set copy mode with palette lookup - fastest for 1:1 blit */
	rdpq_set_mode_copy(false);
	rdpq_mode_tlut(TLUT_RGBA16);

	/* Blit the 8-bit texture - RDP does palette lookup in hardware */
	rdpq_tex_blit(&emu_surf, 0, y_off, NULL);

	/* Detach and show */
	rdpq_detach_show();
}

/* CPU fallback blit for when RDP can't be used */
static void cpu_blit_frame(surface_t *fb)
{
	uint16_t *src = screen_buffer;
	uint16_t *dst = (uint16_t *)fb->buffer;
	int h = g_screen_height;
	int w = g_screen_width;
	int y_off = (240 - h) / 2;

	if (y_off > 0)
		memset(dst, 0, 320 * 240 * 2);

	for (int y = 0; y < h; y++) {
		uint16_t *s = &src[y * 320];
		uint16_t *d = &dst[(y + y_off) * 320];
		for (int x = 0; x < w; x++) {
			uint16_t c = s[x];
			uint16_t r = (c      ) & 0x1f;
			uint16_t g = (c >>  5) & 0x1f;
			uint16_t b = (c >> 10) & 0x1f;
			d[x] = (r << 11) | (g << 6) | (b << 1) | 1;
		}
	}
}

int main(int argc, char *argv[])
{
	unsigned char *rom_copy;
	int use_rdp_blit = 1;

	g_argv = argv;
	g_screen_ptr = screen_buffer;

	display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
	rdpq_init();
	joypad_init();

	/* Boot message */
	console_init();
	console_set_render_mode(RENDER_MANUAL);
	printf("\n\n  PicoDrive64 [RDP accelerated]\n\n");
	printf("  ROM: %s (%d KB)\n", EMBEDDED_ROM_NAME, EMBEDDED_ROM_SIZE / 1024);
	printf("  RAM: %d KB\n", get_memory_size() / 1024);
	printf("  Initializing...\n");
	console_render();

	PicoInit();

	/*
	 * Use 8-bit ALT_RENDERER (fast) + RDP hardware palette blit.
	 * CPU only runs the emulation; RDP handles all display work.
	 */
	PicoIn.opt  = POPT_EN_FM | POPT_EN_PSG | POPT_EN_FM_DAC;
	PicoIn.opt |= POPT_ALT_RENDERER;    /* 8-bit fast renderer */
	PicoIn.opt |= POPT_DIS_VDP_FIFO;    /* skip FIFO timing */
	PicoIn.opt |= POPT_DIS_SPRITE_LIM;
	PicoIn.sndRate = 11025;

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

	/* Set up 8-bit renderer - output goes to Pico.est.Draw2FB */
	PicoDrawSetOutFormat(PDF_NONE, 0);

	/* Also set up 16-bit buffer as fallback */
	PicoDrawSetOutBuf(screen_buffer, 320 * 2);

	printf("  Running! (RDP blit)\n");
	console_render();
	for (volatile int i = 0; i < 2000000; i++) {}

	console_close();
	display_close();
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);

	/* === Main emulation loop === */
	for (;;) {
		/* Run emulation */
		PicoFrame();

		/* Only render to display every N+1 frames */
		if (++frame_count > FRAME_SKIP) {
			frame_count = 0;

			/* Update palette if changed */
			if (Pico.m.dirtyPal) {
				PicoDrawUpdateHighPal();
				update_rdp_palette();
			}

			surface_t *fb = display_get();
			if (fb && fb->buffer) {
				if (use_rdp_blit)
					rdp_blit_frame(fb);
				else {
					cpu_blit_frame(fb);
					display_show(fb);
				}
			}
		}

		/* Input every frame */
		joypad_poll();
		joypad_buttons_t btns = joypad_get_buttons_pressed(JOYPAD_PORT_1);
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
