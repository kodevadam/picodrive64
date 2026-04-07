/*
 * PicoDrive N64 frontend - emulation integration
 * Handles video output, audio, renderer config, and save persistence.
 * Targets SummerCart64 flashcart.
 *
 * (C) 2026
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

#include "../common/emu.h"
#include "../common/input_pico.h"
#include "../libpicofe/input.h"
#include "../libpicofe/plat.h"

#include <pico/pico_int.h>

#include "n64.h"

const char *renderer_names[] = { "16bit accurate", " 8bit accurate", " 8bit fast", NULL };
const char *renderer_names32x[] = { "accurate", "faster", "fastest", NULL };
enum renderer_types { RT_16BIT, RT_8BIT_ACC, RT_8BIT_FAST, RT_COUNT };

#define is_16bit_mode() \
	(currentConfig.renderer == RT_16BIT || (PicoIn.AHW & PAHW_32X))

static int get_renderer(void)
{
	if (PicoIn.AHW & PAHW_32X)
		return currentConfig.renderer32x;
	return currentConfig.renderer;
}

static void change_renderer(int diff)
{
	int *r;
	if (PicoIn.AHW & PAHW_32X)
		r = &currentConfig.renderer32x;
	else
		r = &currentConfig.renderer;
	*r += diff;
	if (*r >= RT_COUNT)
		*r = 0;
	else if (*r < 0)
		*r = RT_COUNT - 1;
}

static void apply_renderer(void)
{
	PicoIn.opt &= ~(POPT_ALT_RENDERER | POPT_EN_SOFTSCALE);
	PicoIn.opt |= POPT_DIS_32C_BORDER;

	switch (get_renderer()) {
	case RT_16BIT:
		PicoDrawSetOutFormat(PDF_RGB555, 0);
		break;
	case RT_8BIT_ACC:
		PicoDrawSetOutFormat(PDF_8BIT, 0);
		break;
	case RT_8BIT_FAST:
		PicoIn.opt |= POPT_ALT_RENDERER;
		PicoDrawSetOutFormat(PDF_NONE, 0);
		break;
	}
}

/* N64-specific default configuration */
void pemu_prep_defconfig(void)
{
	defaultConfig.s_PsndRate = N64_AUDIO_RATE;
	defaultConfig.renderer = RT_8BIT_FAST;  /* fast renderer for N64 perf */
	defaultConfig.renderer32x = 0;
	defaultConfig.scaling = EOPT_SCALE_NONE;
	defaultConfig.Frameskip = 1;            /* allow 1 frame skip */
	defaultConfig.EmuOpt |= EOPT_EN_SOUND;
	defaultConfig.max_skip = 4;

	/* Disable heavy features by default */
	defaultConfig.s_PicoOpt |= POPT_EN_FM | POPT_EN_PSG | POPT_EN_STEREO;
	defaultConfig.s_PicoOpt &= ~(POPT_EN_MCD_GFX | POPT_EN_MCD_CDDA);
}

void pemu_validate_config(void)
{
	if (currentConfig.renderer >= RT_COUNT)
		currentConfig.renderer = RT_8BIT_FAST;
	if (currentConfig.renderer32x > 2)
		currentConfig.renderer32x = 0;

	/* Clamp audio rate to N64 capabilities */
	if (currentConfig.s_PsndRate > 44100)
		currentConfig.s_PsndRate = 44100;
	if (currentConfig.s_PsndRate < 11025)
		currentConfig.s_PsndRate = 11025;
}

void pemu_loop_prep(void)
{
	apply_renderer();
}

void pemu_loop_end(void)
{
}

void pemu_forced_frame(int no_scale, int do_emu)
{
	PicoDrawSetOutFormat(PDF_RGB555, 0);
	PicoDrawSetOutBuf(g_screen_ptr, g_screen_ppitch * 2);
	PicoDraw32xSetFrameMode(0, 0);
	PicoIn.opt &= ~POPT_ALT_RENDERER;

	if (do_emu)
		PicoFrame();
}

/* Finalize frame: 8-bit palette conversion + OSD overlay */
void pemu_finalize_frame(const char *fps, const char *notice_msg)
{
	int renderer = get_renderer();

	if (renderer == RT_8BIT_ACC || renderer == RT_8BIT_FAST) {
		/* Convert 8-bit indexed output to 16-bit RGB */
		unsigned short *pd = (unsigned short *)g_screen_ptr;
		unsigned char *ps = Pico.est.Draw2FB + 328 * 8 + 8;
		unsigned short *pal = Pico.est.HighPal;
		int y, x;

		if (Pico.m.dirtyPal)
			PicoDrawUpdateHighPal();

		for (y = 0; y < g_screen_height; y++) {
			for (x = 0; x < g_screen_width; x++)
				pd[x] = pal[ps[x]];
			ps += 328;
			pd += g_screen_ppitch;
		}
	}

	if (fps && fps[0])
		emu_osd_text16(4, g_screen_height - 16, fps);
	if (notice_msg && notice_msg[0])
		emu_osd_text16(4, g_screen_height - 32, notice_msg);
}

void pemu_sound_start(void)
{
	/* Sound is managed by the common emu.c via sndout driver */
}

/* Video mode change callback from Genesis VDP */
void emu_video_mode_change(int start_line, int line_count, int start_col, int col_count)
{
	g_screen_width = col_count;
	g_screen_height = line_count;
	g_screen_ppitch = N64_SCREEN_WIDTH;
}

/* 32X startup - switch to 16-bit mode */
void emu_32x_startup(void)
{
	PicoDrawSetOutFormat(PDF_RGB555, 0);
}

/* Logging */
void lprintf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

/* Video control functions for common frontend */
void plat_video_toggle_renderer(int change, int menu_call)
{
	change_renderer(change);
	if (!menu_call)
		apply_renderer();
}

void plat_video_loop_prepare(void)
{
	apply_renderer();
}

void plat_video_set_buffer(void *buf)
{
	if (is_16bit_mode())
		PicoDrawSetOutBuf(buf, g_screen_ppitch * 2);
}

void plat_video_set_size(int w, int h)
{
}

void plat_video_set_shadow(int w, int h)
{
}

void plat_video_clear_status(void)
{
}

void plat_video_clear_buffers(void)
{
	memset(g_screen_ptr, 0, N64_FB_SIZE);
}

void plat_update_volume(int has_changed, int is_up)
{
}

/* MP3 stubs - no Sega CD audio on N64 */
int  mp3_get_bitrate(void *f, int size) { return 0; }
void mp3_start_play(void *f, int pos) { }
void mp3_update(s32 *buffer, int length, int stereo) { }

/* Ensure save directory exists on SD card */
static void ensure_save_dir(void)
{
	mkdir(N64_CONFIG_DIR, 0755);
	mkdir(N64_SAVE_DIR, 0755);
}

/* Called by common emu.c at startup */
void plat_init(void)
{
	ensure_save_dir();
}

void plat_finish(void)
{
}
