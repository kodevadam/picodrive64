/*
 * Platform interface functions for N64 PicoDrive frontend
 * Targets SummerCart64 flashcart with SD card filesystem.
 *
 * (C) 2026
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>

#include "../common/emu.h"
#include "../common/input_pico.h"
#include "../libpicofe/plat.h"
#include "../libpicofe/menu.h"

#include <pico/pico_int.h>

#include "n64.h"
#include "in_n64.h"

/*
 * Memory layout (8 MB Expansion Pak):
 *   0 - ~1MB:   PicoDrive code + static data
 *   mem_pool:   5 MB for ROM + emulator allocations
 *   drc_pool:   256 KB for DRC code cache
 *   screen_buffer, menubg_buffer: 150 KB each
 *   Stack + heap for libdragon: remaining
 *
 * Without Expansion Pak (4 MB):
 *   mem_pool shrinks to 1.5 MB (small ROMs only)
 */

/* Expansion Pak state */
static int has_expansion_pak = 0;
static size_t mem_pool_size = 0;

/* Dynamic memory pool - allocated at runtime based on RAM size */
static uint8_t *mem_pool = NULL;
static size_t mem_pool_offset = 0;

/* DRC code cache region */
static uint8_t drc_pool[N64_DRC_POOL_SIZE] __attribute__((aligned(4096)));

/* Screen buffers */
static uint16_t __attribute__((aligned(16))) screen_buffer[N64_SCREEN_WIDTH * N64_SCREEN_HEIGHT];
static uint16_t __attribute__((aligned(16))) menubg_buffer[N64_SCREEN_WIDTH * N64_SCREEN_HEIGHT];

/* Convert RGB565 to RGBA5551 (N64 native format)
 * RGB565:  RRRR RGGG GGGB BBBB
 * RGBA5551: RRRR RGGG GGBB BBBa
 */
static inline uint16_t rgb565_to_rgba5551(uint16_t c)
{
	uint16_t r = (c >> 11) & 0x1f;
	uint16_t g = (c >> 5) & 0x3f;
	uint16_t b = c & 0x1f;
	/* Convert 6-bit green to 5-bit, set alpha=1 */
	return (r << 11) | ((g >> 1) << 6) | (b << 1) | 1;
}

/* Batch convert a line of pixels from RGB565 to RGBA5551 */
static void convert_line_rgb565_to_rgba5551(uint16_t *dst, const uint16_t *src, int width)
{
	for (int x = 0; x < width; x++)
		dst[x] = rgb565_to_rgba5551(src[x]);
}

/* System level initialization */
int plat_target_init(void)
{
	/* Initialize N64 debug output (USB/ISViewer) */
	debug_init_isviewer();
	debug_init_usblog();

	/* Detect Expansion Pak */
	has_expansion_pak = n64_has_expansion_pak();
	if (has_expansion_pak) {
		mem_pool_size = N64_MEMPOOL_SIZE;     /* 5 MB */
		lprintf("N64: Expansion Pak detected (8 MB RDRAM)\n");
	} else {
		mem_pool_size = N64_MEMPOOL_4MB_SIZE; /* 1.5 MB */
		lprintf("N64: No Expansion Pak (4 MB RDRAM) - only small ROMs!\n");
	}

	/* Allocate memory pool */
	mem_pool = (uint8_t *)malloc(mem_pool_size);
	if (!mem_pool) {
		lprintf("N64: FATAL - cannot allocate memory pool!\n");
		return -1;
	}
	memset(mem_pool, 0, mem_pool_size);

	/* Initialize display: 320x240 16-bit */
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);

	/* Initialize timer for timing functions */
	timer_init();

	/* Initialize SummerCart64 SD filesystem */
	if (dfs_init(DFS_DEFAULT_LOCATION) != DFS_ESUCCESS) {
		lprintf("N64: DFS init failed, trying fat filesystem...\n");
	}

	/* Initialize joypad */
	joypad_init();

	/* Buffer resolutions - Genesis native is 320x224/240 */
	g_menuscreen_w = N64_SCREEN_WIDTH;
	g_menuscreen_h = N64_SCREEN_HEIGHT;
	g_menuscreen_pp = N64_SCREEN_WIDTH;

	g_screen_width = N64_SCREEN_WIDTH;
	g_screen_height = N64_SCREEN_HEIGHT;
	g_screen_ppitch = N64_SCREEN_WIDTH;

	g_menubg_src_w = N64_SCREEN_WIDTH;
	g_menubg_src_h = N64_SCREEN_HEIGHT;
	g_menubg_src_pp = N64_SCREEN_WIDTH;

	/* Set up screen buffer */
	g_screen_ptr = screen_buffer;
	g_menubg_ptr = menubg_buffer;

	return 0;
}

/* System level deinitialization */
void plat_target_finish(void)
{
	display_close();
	if (mem_pool) {
		free(mem_pool);
		mem_pool = NULL;
	}
}

/* Called by common frontend to set up the input driver */
void plat_target_setup_input(void)
{
	/* Default bindings are defined in in_n64.c */
	static struct in_default_bind n64_defbinds[] = {
		{ N64_BIT_DU,      IN_BINDTYPE_PLAYER12, GBTN_UP },
		{ N64_BIT_DD,      IN_BINDTYPE_PLAYER12, GBTN_DOWN },
		{ N64_BIT_DL,      IN_BINDTYPE_PLAYER12, GBTN_LEFT },
		{ N64_BIT_DR,      IN_BINDTYPE_PLAYER12, GBTN_RIGHT },
		{ N64_BIT_A,       IN_BINDTYPE_PLAYER12, GBTN_B },
		{ N64_BIT_B,       IN_BINDTYPE_PLAYER12, GBTN_C },
		{ N64_BIT_Z,       IN_BINDTYPE_PLAYER12, GBTN_A },
		{ N64_BIT_L,       IN_BINDTYPE_PLAYER12, GBTN_X },
		{ N64_BIT_R,       IN_BINDTYPE_PLAYER12, GBTN_Z },
		{ N64_BIT_CR,      IN_BINDTYPE_PLAYER12, GBTN_Y },
		{ N64_BIT_START,   IN_BINDTYPE_PLAYER12, GBTN_START },
		{ N64_BIT_CU,      IN_BINDTYPE_EMU, PEVB_MENU },
		{ N64_BIT_CD,      IN_BINDTYPE_EMU, PEVB_STATE_SAVE },
		{ N64_BIT_CL,      IN_BINDTYPE_EMU, PEVB_STATE_LOAD },
		{ 0, 0, 0 }
	};
	in_n64_init(n64_defbinds);
}

/* Display a completed frame buffer with pixel format conversion */
void plat_video_flip(void)
{
	surface_t *fb = display_get();
	if (!fb)
		return;

	uint16_t *src = (uint16_t *)g_screen_ptr;
	uint16_t *dst = (uint16_t *)fb->buffer;
	int src_h = g_screen_height;
	int src_w = g_screen_width;
	int y_offset = (N64_SCREEN_HEIGHT - src_h) / 2;
	int x_offset = (N64_SCREEN_WIDTH - src_w) / 2;

	/* Clear if there are borders */
	if (y_offset > 0 || x_offset > 0)
		memset(dst, 0, N64_FB_SIZE);

	/* Copy with RGB565 -> RGBA5551 conversion */
	for (int y = 0; y < src_h && (y + y_offset) < N64_SCREEN_HEIGHT; y++) {
		convert_line_rgb565_to_rgba5551(
			&dst[(y + y_offset) * N64_SCREEN_WIDTH + x_offset],
			&src[y * g_screen_ppitch],
			src_w);
	}

	display_show(fb);
}

void plat_video_wait_vsync(void)
{
}

void plat_video_menu_update(void)
{
}

void plat_video_menu_enter(int is_rom_loaded)
{
}

void plat_video_menu_begin(void)
{
	g_menuscreen_ptr = screen_buffer;
}

void plat_video_menu_end(void)
{
	plat_video_flip();
	g_menuscreen_ptr = NULL;
}

void plat_video_menu_leave(void)
{
}

void plat_show_cursor(int on)
{
}

int plat_grab_cursor(int on)
{
	return 0;
}

int plat_has_wm(void)
{
	return 0;
}

int plat_parse_arg(int argc, char *argv[], int *x)
{
	return 1;
}

void plat_early_init(void)
{
}

/* Base directory for configuration and save files on SD card */
int plat_get_root_dir(char *dst, int len)
{
	const char *path = "sd:/picodrive/";
	int plen = strlen(path);
	if (len > plen)
		strcpy(dst, path);
	else if (len > 0)
		*dst = 0;
	return strlen(dst);
}

int plat_get_skin_dir(char *dst, int len)
{
	if (len > 5)
		strcpy(dst, "skin/");
	else if (len > 0)
		*dst = 0;
	return strlen(dst);
}

/* Top directory for ROM images on SummerCart64 SD */
int plat_get_data_dir(char *dst, int len)
{
	const char *path = "sd:/picodrive/roms/";
	int plen = strlen(path);
	if (len > plen)
		strcpy(dst, path);
	else if (len > 0)
		*dst = 0;
	return strlen(dst);
}

int plat_is_dir(const char *path)
{
	struct stat st;
	if (stat(path, &st) == 0)
		return S_ISDIR(st.st_mode);
	return 0;
}

unsigned int plat_get_ticks_ms(void)
{
	return TICKS_TO_MS(timer_ticks());
}

unsigned int plat_get_ticks_us(void)
{
	/* Convert ticks to microseconds */
	return (unsigned int)((uint64_t)timer_ticks() * 1000000ULL / TICKS_PER_SECOND);
}

void plat_sleep_ms(int ms)
{
	wait_ms(ms);
}

void plat_wait_till_us(unsigned int us_to)
{
	unsigned int now = plat_get_ticks_us();
	if (us_to > now) {
		unsigned int diff = us_to - now;
		if (diff > 1000)
			wait_ms(diff / 1000);
	}
}

int plat_wait_event(int *fds_hnds, int count, int timeout_ms)
{
	return 0;
}

/* Memory pool allocator */
void *plat_mmap(unsigned long addr, size_t size, int need_exec, int is_fixed)
{
	if (!mem_pool)
		return malloc(size);

	size_t aligned_offset = (mem_pool_offset + 15) & ~15;

	if (aligned_offset + size > mem_pool_size) {
		lprintf("plat_mmap: pool exhausted! need %u, used %u/%u\n",
			(unsigned)size, (unsigned)aligned_offset, (unsigned)mem_pool_size);
		/* Fall back to malloc for small allocations */
		if (size < 65536)
			return calloc(1, size);
		return NULL;
	}

	void *ptr = &mem_pool[aligned_offset];
	mem_pool_offset = aligned_offset + size;
	memset(ptr, 0, size);
	return ptr;
}

void *plat_mremap(void *ptr, size_t oldsize, size_t newsize)
{
	if (newsize <= oldsize)
		return ptr;

	void *new_ptr = plat_mmap(0, newsize, 0, 0);
	if (new_ptr && ptr)
		memcpy(new_ptr, ptr, oldsize);
	return new_ptr;
}

void plat_munmap(void *ptr, size_t size)
{
	/* Check if it's from our pool - if not, it was malloc'd */
	if (mem_pool && ptr >= (void *)mem_pool &&
	    ptr < (void *)(mem_pool + mem_pool_size))
		return; /* pool memory, can't free individually */

	/* Was a malloc fallback allocation */
	free(ptr);
}

void *plat_mem_get_for_drc(size_t size)
{
	if (size > N64_DRC_POOL_SIZE)
		return NULL;
	return drc_pool;
}

int plat_mem_set_exec(void *ptr, size_t size)
{
	return 0; /* bare metal, no memory protection */
}

/* VR4300 cache management - critical for DRC */
void cache_flush_d_inval_i(void *start_addr, void *end_addr)
{
	size_t len = (char *)end_addr - (char *)start_addr;
	if (len > 0) {
		data_cache_hit_writeback(start_addr, len);
		inst_cache_hit_invalidate(start_addr, len);
	}
}

/* Platform capabilities */
static int sound_rates[] = { 11025, 22050, 44100, -1 };

struct plat_target plat_target = {
	.cpu_clock_get    = NULL,
	.cpu_clock_set    = NULL,
	.bat_capacity_get = NULL,
	.sound_rates      = sound_rates,
};

/* Query functions for N64-specific state */
int n64_get_expansion_pak(void)
{
	return has_expansion_pak;
}

size_t n64_get_max_rom_size(void)
{
	/* Leave room for emulator data in the pool */
	if (has_expansion_pak)
		return 4 * 1024 * 1024; /* 4 MB with Expansion Pak */
	else
		return 1024 * 1024;     /* 1 MB without */
}

/* posix_memalign for newlib compatibility */
int posix_memalign(void **p, size_t align, size_t size)
{
	if (p) {
		*p = memalign(align, size);
		if (*p) {
			memset(*p, 0, size);
			return 0;
		}
		return ENOMEM;
	}
	return EINVAL;
}
