/*
 * PicoDrive N64 - Standalone main with embedded ROM
 * Optimized for VR4300 at 93.75 MHz
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

/* Frame skip: 0=none, 1=every other, 2=every 3rd */
static int frame_count = 0;
#define FRAME_SKIP 1

/* Fast RGB555 to RGBA5551: just shift left 1 and set alpha bit */
static void blit_rgb555_to_display(surface_t *fb)
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
		/* RGB555 (0RRRRRGGGGGBBBBB) -> RGBA5551 (RRRRRGGGGBBBBBA) */
		/* Just shift left by 1 and OR with 1 for alpha */
		for (int x = 0; x < w; x += 4) {
			d[x]   = (s[x]   << 1) | 1;
			d[x+1] = (s[x+1] << 1) | 1;
			d[x+2] = (s[x+2] << 1) | 1;
			d[x+3] = (s[x+3] << 1) | 1;
		}
	}
}

int main(int argc, char *argv[])
{
	unsigned char *rom_copy;

	g_argv = argv;
	g_screen_ptr = screen_buffer;

	/* Expansion Pak (8 MB) required */
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
	joypad_init();

	/* Show boot message */
	console_init();
	console_set_render_mode(RENDER_MANUAL);
	printf("\n\n");
	printf("  PicoDrive64\n\n");
	printf("  ROM: %s (%d KB)\n", EMBEDDED_ROM_NAME, EMBEDDED_ROM_SIZE / 1024);
	printf("  RAM: %d KB\n\n", get_memory_size() / 1024);
	printf("  Initializing...\n");
	console_render();

	PicoInit();

	/*
	 * Performance tuning:
	 * - 16-bit accurate renderer (correct colors, writes to screen_buffer)
	 * - FM + PSG enabled (authentic sound)
	 * - Mono 11025 Hz (minimize audio CPU)
	 * - VDP FIFO disabled (skip expensive timing)
	 * - Sprite limit disabled (avoids overhead)
	 * - Frame skip 1 (render every other frame)
	 */
	PicoIn.opt  = POPT_EN_FM | POPT_EN_PSG | POPT_EN_FM_DAC;
	PicoIn.opt |= POPT_DIS_VDP_FIFO;
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

	/* 16-bit RGB555 renderer: writes directly to screen_buffer */
	PicoDrawSetOutFormat(PDF_RGB555, 0);
	PicoDrawSetOutBuf(screen_buffer, 320 * 2);

	printf("  Running!\n");
	console_render();
	for (volatile int i = 0; i < 2000000; i++) {}

	console_close();
	display_close();
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);

	/* === Main emulation loop === */
	for (;;) {
		PicoFrame();

		if (++frame_count > FRAME_SKIP) {
			frame_count = 0;
			surface_t *fb = display_get();
			if (fb && fb->buffer) {
				blit_rgb555_to_display(fb);
				display_show(fb);
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
