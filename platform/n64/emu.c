/*
 * PicoDrive N64 frontend - emulation loop integration
 *
 * (C) 2026
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */
#include <string.h>

#include "../common/emu.h"
#include "../common/input_pico.h"
#include "../libpicofe/input.h"
#include "../libpicofe/plat.h"

#include <pico/pico_int.h>

#include "n64.h"

/* Render buffer for 8-bit mode */
static u16 __attribute__((aligned(16))) localPal[0x100];

/* Audio buffer */
#define SOUND_BLOCK_COUNT   N64_SND_BLOCK_COUNT
#define SOUND_BUFFER_CHUNK  N64_SND_CHUNK_SIZE
static short __attribute__((aligned(4))) sndBuffer[SOUND_BUFFER_CHUNK * SOUND_BLOCK_COUNT];
static int snd_write_pos = 0;

const char *renderer_names[] = { "16bit accurate", " 8bit accurate", " 8bit fast", NULL };
const char *renderer_names32x[] = { "accurate", "faster", "fastest", NULL };
enum renderer_types { RT_16BIT, RT_8BIT_ACC, RT_8BIT_FAST, RT_COUNT };

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

void pemu_prep_defconfig(void)
{
	defaultConfig.s_PsndRate = N64_AUDIO_RATE;
	defaultConfig.renderer = RT_8BIT_FAST; /* use fast renderer by default on N64 */
	defaultConfig.renderer32x = 0;
	defaultConfig.scaling = EOPT_SCALE_NONE;
	defaultConfig.Frameskip = 1; /* allow 1 frame skip for performance */
	defaultConfig.EmuOpt |= EOPT_EN_SOUND;
	defaultConfig.max_skip = 4;
}

void pemu_validate_config(void)
{
	if (currentConfig.renderer >= RT_COUNT)
		currentConfig.renderer = RT_8BIT_FAST;
	if (currentConfig.renderer32x > 2)
		currentConfig.renderer32x = 0;
}

void pemu_loop_prep(void)
{
	apply_renderer();

	/* Initialize audio */
	if (currentConfig.EmuOpt & EOPT_EN_SOUND) {
		PicoIn.sndRate = currentConfig.s_PsndRate;
		audio_init(currentConfig.s_PsndRate, 2);
	}

	snd_write_pos = 0;
	memset(sndBuffer, 0, sizeof(sndBuffer));
}

void pemu_loop_end(void)
{
	audio_close();
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

/* Finalize frame: copy rendered output to display buffer, handle OSD */
void pemu_finalize_frame(const char *fps, const char *notice_msg)
{
	int renderer = get_renderer();

	if (renderer == RT_8BIT_ACC || renderer == RT_8BIT_FAST) {
		/* Convert 8-bit palette output to 16-bit */
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

	/* Draw OSD text */
	if (fps && fps[0])
		emu_osd_text16(4, g_screen_height - 16, fps);
	if (notice_msg && notice_msg[0])
		emu_osd_text16(4, g_screen_height - 32, notice_msg);
}

/* Audio output callback - push samples to N64 audio hardware */
void pemu_sound_start(void)
{
	/* Audio is initialized in pemu_loop_prep */
	PicoIn.sndRate = currentConfig.s_PsndRate;

	/* Point PicoDrive sound output to our buffer */
	PsndOut = sndBuffer;
	snd_write_pos = 0;
}

void emu_sound_wait(void)
{
	/* Write accumulated audio samples to N64 audio hardware */
	if (PsndOut && PsndOut != sndBuffer) {
		int samples = PsndOut - sndBuffer;
		if (samples > 0) {
			audio_write((short *)sndBuffer);
		}
		PsndOut = sndBuffer;
	}
}

/* Input mapping for N64 controller */
static struct in_default_bind in_n64_defbinds[] =
{
	/* D-pad mapping */
	{ N64_BTN_DU,    IN_BINDTYPE_PLAYER12, GBTN_UP },
	{ N64_BTN_DD,    IN_BINDTYPE_PLAYER12, GBTN_DOWN },
	{ N64_BTN_DL,    IN_BINDTYPE_PLAYER12, GBTN_LEFT },
	{ N64_BTN_DR,    IN_BINDTYPE_PLAYER12, GBTN_RIGHT },
	/* Button mapping: A->B, B->C, Z->A */
	{ N64_BTN_A,     IN_BINDTYPE_PLAYER12, GBTN_B },
	{ N64_BTN_B,     IN_BINDTYPE_PLAYER12, GBTN_C },
	{ N64_BTN_Z,     IN_BINDTYPE_PLAYER12, GBTN_A },
	/* Shoulder and C buttons for 6-button */
	{ N64_BTN_L,     IN_BINDTYPE_PLAYER12, GBTN_X },
	{ N64_BTN_R,     IN_BINDTYPE_PLAYER12, GBTN_Z },
	{ N64_BTN_CR,    IN_BINDTYPE_PLAYER12, GBTN_Y },
	/* Start */
	{ N64_BTN_START, IN_BINDTYPE_PLAYER12, GBTN_START },
	/* C-Up for menu */
	{ N64_BTN_CU,    IN_BINDTYPE_EMU, PEVB_MENU },
	/* C-Down for save state */
	{ N64_BTN_CD,    IN_BINDTYPE_EMU, PEVB_STATE_SAVE },
	/* C-Left for load state */
	{ N64_BTN_CL,    IN_BINDTYPE_EMU, PEVB_STATE_LOAD },
	{ 0, 0, 0 }
};

/* Video mode change callback from core */
void emu_video_mode_change(int start_line, int line_count, int start_col, int col_count)
{
	/* Update screen dimensions based on Genesis VDP mode */
	g_screen_width = col_count;
	g_screen_height = line_count;
	g_screen_ppitch = N64_SCREEN_WIDTH;
}

/* 32X startup callback */
void emu_32x_startup(void)
{
	PicoDrawSetOutFormat(PDF_RGB555, 0);
}

/* Logging function */
void lprintf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

/* Toggle renderer from menu */
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

/* Stub for MP3 functions (no Sega CD support on N64) */
int mp3_get_bitrate(void *f, int size) { return 0; }
void mp3_start_play(void *f, int pos) { }
void mp3_update(s32 *buffer, int length, int stereo) { }

/* is_16bit_mode helper */
#define is_16bit_mode() \
	(currentConfig.renderer == RT_16BIT || (PicoIn.AHW & PAHW_32X))
