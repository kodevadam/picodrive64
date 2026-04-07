/*
 * Platform interface functions for N64 PicoDrive frontend
 *
 * (C) 2026
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../common/emu.h"
#include "../libpicofe/menu.h"
#include "../libpicofe/plat.h"

#include <pico/pico_int.h>

#include "n64.h"

/* Simple memory pool for plat_mmap allocations */
static uint8_t mem_pool[N64_MEMPOOL_SIZE] __attribute__((aligned(16)));
static size_t mem_pool_offset = 0;

/* DRC code cache region */
static uint8_t drc_pool[N64_DRC_POOL_SIZE] __attribute__((aligned(4096)));

/* Screen buffer */
static uint16_t __attribute__((aligned(16))) screen_buffer[N64_SCREEN_WIDTH * N64_SCREEN_HEIGHT];

/* Menu background buffer */
static uint16_t __attribute__((aligned(16))) menubg_buffer[N64_SCREEN_WIDTH * N64_SCREEN_HEIGHT];

/* System level initialization */
int plat_target_init(void)
{
	/* Initialize N64 subsystems */
	debug_init_isviewer();
	debug_init_usblog();

	/* Initialize display: 320x240 16-bit */
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);

	/* Initialize filesystem for ROM loading from SD card */
	dfs_init(DFS_DEFAULT_LOCATION);

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
}

/* Display a completed frame buffer and prepare a new render buffer */
void plat_video_flip(void)
{
	surface_t *fb = display_get();
	if (fb) {
		/* Copy emulator framebuffer to N64 display surface.
		 * PicoDrive renders RGB555/RGB565, N64 uses RGBA5551.
		 * For now, do a direct copy assuming compatible format;
		 * pixel format conversion will be added if needed.
		 */
		uint16_t *src = (uint16_t *)g_screen_ptr;
		uint16_t *dst = (uint16_t *)fb->buffer;
		int src_h = g_screen_height;
		int y_offset = (N64_SCREEN_HEIGHT - src_h) / 2;

		/* Clear the framebuffer first if there's vertical offset */
		if (y_offset > 0) {
			memset(dst, 0, N64_FB_SIZE);
		}

		/* Copy visible lines */
		for (int y = 0; y < src_h; y++) {
			memcpy(&dst[(y + y_offset) * N64_SCREEN_WIDTH],
			       &src[y * g_screen_ppitch],
			       g_screen_width * sizeof(uint16_t));
		}

		display_show(fb);
	}
}

/* Wait for start of vertical blanking */
void plat_video_wait_vsync(void)
{
	/* libdragon handles vsync internally via display_get() */
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

/* Base directory for configuration and save files */
int plat_get_root_dir(char *dst, int len)
{
	if (len > 4)
		strcpy(dst, "sd:/");
	else if (len > 0)
		*dst = 0;
	return strlen(dst);
}

/* Base directory for emulator resources */
int plat_get_skin_dir(char *dst, int len)
{
	if (len > 5)
		strcpy(dst, "skin/");
	else if (len > 0)
		*dst = 0;
	return strlen(dst);
}

/* Top directory for ROM images */
int plat_get_data_dir(char *dst, int len)
{
	if (len > 9)
		strcpy(dst, "sd:/roms/");
	else if (len > 0)
		*dst = 0;
	return strlen(dst);
}

/* Check if path is a directory */
int plat_is_dir(const char *path)
{
	/* Use standard stat approach */
	struct stat st;
	if (stat(path, &st) == 0)
		return S_ISDIR(st.st_mode);
	return 0;
}

/* Current time in ms */
unsigned int plat_get_ticks_ms(void)
{
	return timer_ticks() / (TICKS_PER_SECOND / 1000);
}

/* Current time in us */
unsigned int plat_get_ticks_us(void)
{
	return timer_ticks() / (TICKS_PER_SECOND / 1000000);
}

/* Sleep for some time in ms */
void plat_sleep_ms(int ms)
{
	wait_ms(ms);
}

/* Sleep for some time in us */
void plat_wait_till_us(unsigned int us_to)
{
	unsigned int now = plat_get_ticks_us();
	if (us_to > now)
		wait_ms((us_to - now) / 1000);
}

/* Wait until some event occurs, or timeout */
int plat_wait_event(int *fds_hnds, int count, int timeout_ms)
{
	return 0; /* unused on N64 */
}

/* Memory mapping functions - simple pool allocator */
void *plat_mmap(unsigned long addr, size_t size, int need_exec, int is_fixed)
{
	/* Align to 16 bytes */
	size_t aligned_offset = (mem_pool_offset + 15) & ~15;

	if (aligned_offset + size > N64_MEMPOOL_SIZE) {
		lprintf("plat_mmap: out of memory! requested %u, used %u/%u\n",
			(unsigned)size, (unsigned)aligned_offset, N64_MEMPOOL_SIZE);
		return NULL;
	}

	void *ptr = &mem_pool[aligned_offset];
	mem_pool_offset = aligned_offset + size;
	memset(ptr, 0, size);

	return ptr;
}

void *plat_mremap(void *ptr, size_t oldsize, size_t newsize)
{
	/* Can't easily remap in a pool allocator - allocate new */
	if (newsize <= oldsize)
		return ptr;

	void *new_ptr = plat_mmap(0, newsize, 0, 0);
	if (new_ptr && ptr)
		memcpy(new_ptr, ptr, oldsize);
	return new_ptr;
}

void plat_munmap(void *ptr, size_t size)
{
	/* Pool allocator - can't free individual allocations.
	 * Memory is reclaimed when the emulator resets.
	 */
}

/* DRC code cache allocation */
void *plat_mem_get_for_drc(size_t size)
{
	if (size > N64_DRC_POOL_SIZE)
		return NULL;
	return drc_pool;
}

int plat_mem_set_exec(void *ptr, size_t size)
{
	/* N64 bare metal - no memory protection to set */
	return 0;
}

/* Cache flush for DRC - critical on VR4300 */
void cache_flush_d_inval_i(void *start_addr, void *end_addr)
{
	size_t len = (char *)end_addr - (char *)start_addr;
	if (len > 0) {
		data_cache_hit_writeback(start_addr, len);
		inst_cache_hit_invalidate(start_addr, len);
	}
}

/* Sound rates available */
static int sound_rates[] = { 11025, 22050, 44100, -1 };

struct plat_target plat_target = {
	.cpu_clock_get = NULL,
	.cpu_clock_set = NULL,
	.bat_capacity_get = NULL,
	.sound_rates = sound_rates,
};

/* Required by some libc/newlib environments */
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
