/*
 * PicoDrive N64 - Standalone main with embedded ROM
 * Embeds Genesis ROM directly, auto-starts on boot.
 *
 * (C) 2026
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pico/pico_int.h>

#include "../common/input_pico.h"
#include "n64.h"
#include "in_n64.h"
#include "embedded_rom.h"

/* Globals expected by various parts of PicoDrive */
char **g_argv;
void *g_screen_ptr;
int g_screen_width  = 320;
int g_screen_height = 240;
int g_screen_ppitch = 320;

/* Screen buffer */
static uint16_t __attribute__((aligned(16))) screen_buffer[320 * 240];

/* Display an error message on screen and halt */
static void fatal(const char *msg)
{
	console_init();
	console_set_render_mode(RENDER_MANUAL);
	printf("\n\n  PicoDrive64 - FATAL ERROR\n\n  %s\n", msg);
	console_render();
	for (;;) { /* halt */ }
}

/* Convert RGB565 to RGBA5551 */
static inline uint16_t rgb565_to_rgba5551(uint16_t c)
{
	uint16_t r = (c >> 11) & 0x1f;
	uint16_t g = (c >> 5) & 0x3f;
	uint16_t b = c & 0x1f;
	return (r << 11) | ((g >> 1) << 6) | (b << 1) | 1;
}

int main(int argc, char *argv[])
{
	unsigned char *rom_copy;

	g_argv = argv;
	g_screen_ptr = screen_buffer;

	/* === N64 hardware init === */
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
	joypad_init();

	/* Show boot message */
	console_init();
	console_set_render_mode(RENDER_MANUAL);
	printf("\n  PicoDrive64\n  Loading %s...\n", EMBEDDED_ROM_NAME);
	console_render();

	/* === Memory pool - use malloc, not static (ares may have 4MB) === */
	size_t pool_size = (get_memory_size() >= 0x800000) ? (4*1024*1024) : (1024*1024);

	/* === Initialize PicoDrive core === */
	PicoInit();

	/* Configure emulator */
	PicoIn.opt = POPT_EN_FM | POPT_EN_PSG | POPT_EN_STEREO | POPT_EN_FM_DAC;
	PicoIn.opt |= POPT_ALT_RENDERER; /* fast renderer */
	PicoIn.sndRate = 22050;
	PicoIn.regionOverride = 0; /* auto-detect */

	/* === Load ROM from embedded data === */
	rom_copy = (unsigned char *)malloc(EMBEDDED_ROM_SIZE + 4);
	if (!rom_copy)
		fatal("Out of memory allocating ROM buffer!");

	memcpy(rom_copy, embedded_rom_data, EMBEDDED_ROM_SIZE);

	printf("  Inserting cartridge (%d KB)...\n", EMBEDDED_ROM_SIZE / 1024);
	console_render();

	if (PicoCartInsert(rom_copy, EMBEDDED_ROM_SIZE, NULL))
		fatal("PicoCartInsert failed!");

	/* Power on */
	PicoPower();
	PicoReset();
	PicoLoopPrepare();

	/* Set up rendering */
	PicoDrawSetOutFormat(PDF_RGB555, 0);
	PicoDrawSetOutBuf(screen_buffer, 320 * 2);

	printf("  Starting emulation!\n");
	console_render();

	/* Brief delay so user can see the boot messages */
	for (volatile int i = 0; i < 5000000; i++) {}

	/* Close console, switch to framebuffer mode */
	console_close();

	/* === Main emulation loop === */
	for (;;) {
		/* Run one frame */
		PicoFrame();

		/* Blit to N64 display with pixel format conversion */
		surface_t *fb = display_get();
		if (fb) {
			uint16_t *src = screen_buffer;
			uint16_t *dst = (uint16_t *)fb->buffer;
			int h = 224; /* Genesis typical height */
			int y_off = (240 - h) / 2;

			if (y_off > 0)
				memset(dst, 0, 320 * 240 * 2);

			for (int y = 0; y < h; y++) {
				uint16_t *s = &src[y * 320];
				uint16_t *d = &dst[(y + y_off) * 320];
				for (int x = 0; x < 320; x++)
					d[x] = rgb565_to_rgba5551(s[x]);
			}
			display_show(fb);
		}

		/* Read controller input */
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

/* === Stubs required by PicoDrive core === */
void emu_video_mode_change(int start_line, int line_count, int start_col, int col_count)
{
	/* Update visible area */
	g_screen_width = col_count;
	g_screen_height = line_count;
}

void emu_32x_startup(void) { }

void lprintf(const char *fmt, ...)
{
	/* In standalone mode, debug output goes nowhere visible */
}

/* Memory functions for PicoDrive core */
void *plat_mmap(unsigned long addr, size_t size, int need_exec, int is_fixed)
{
	return calloc(1, size);
}

void *plat_mremap(void *ptr, size_t oldsize, size_t newsize)
{
	return realloc(ptr, newsize);
}

void plat_munmap(void *ptr, size_t size)
{
	free(ptr);
}

void *plat_mem_get_for_drc(size_t size)
{
	return NULL; /* no DRC in standalone mode */
}

int plat_mem_set_exec(void *ptr, size_t size)
{
	return 0;
}

void cache_flush_d_inval_i(void *start, void *end)
{
	size_t len = (char *)end - (char *)start;
	if (len > 0) {
		data_cache_hit_writeback(start, len);
		inst_cache_hit_invalidate(start, len);
	}
}

/* MP3 stubs */
int  mp3_get_bitrate(void *f, int size) { return 0; }
void mp3_start_play(void *f, int pos) { }
void mp3_update(s32 *buffer, int length, int stereo) { }
