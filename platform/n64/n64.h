/*
 * PicoDrive N64 platform header
 * Constants, helpers, and hardware definitions for Nintendo 64 port.
 * Targets SummerCart64 flashcart for ROM loading via SD card.
 *
 * (C) 2026
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */

#ifndef __N64_PLATFORM_H__
#define __N64_PLATFORM_H__

#include <libdragon.h>

/* N64 display configuration */
#define N64_SCREEN_WIDTH    320
#define N64_SCREEN_HEIGHT   240
#define N64_BPP             16      /* 16-bit color (RGBA5551) */

/* Audio configuration */
#define N64_AUDIO_RATE      22050   /* balance of quality vs CPU */
#define N64_AUDIO_BUFSIZE   (N64_AUDIO_RATE / 50)

/* Memory budget */
#define N64_RDRAM_SIZE_4MB  (4 * 1024 * 1024)
#define N64_RDRAM_SIZE_8MB  (8 * 1024 * 1024)

/* Memory pool sizes */
#define N64_MEMPOOL_SIZE       (5 * 1024 * 1024)    /* 5 MB with Expansion Pak */
#define N64_MEMPOOL_4MB_SIZE   (1536 * 1024)         /* 1.5 MB without */
#define N64_DRC_POOL_SIZE      (256 * 1024)          /* 256 KB for DRC code cache */

/* Frame buffer size: 320x240x2 bytes = 150KB */
#define N64_FB_SIZE         (N64_SCREEN_WIDTH * N64_SCREEN_HEIGHT * 2)

/* Audio ring buffer: 7 chunks like PSP port */
#define N64_SND_BLOCK_COUNT 7
#define N64_SND_CHUNK_SIZE  (2 * N64_AUDIO_RATE / 50)
#define N64_SND_BUF_SIZE    (N64_SND_CHUNK_SIZE * N64_SND_BLOCK_COUNT)

/* N64 controller button bit positions (for input driver) */
enum {
	N64_BIT_A = 0,
	N64_BIT_B,
	N64_BIT_Z,
	N64_BIT_START,
	N64_BIT_DU,
	N64_BIT_DD,
	N64_BIT_DL,
	N64_BIT_DR,
	N64_BIT_L,
	N64_BIT_R,
	N64_BIT_CU,
	N64_BIT_CD,
	N64_BIT_CL,
	N64_BIT_CR,
	N64_BIT_NUBUP,
	N64_BIT_NUBDOWN,
	N64_BIT_COUNT
};

/* Analog stick deadzone threshold */
#define N64_ANALOG_DEADZONE 20

/* Check if Expansion Pak is present (8 MB RAM) */
static inline int n64_has_expansion_pak(void)
{
	return get_memory_size() >= N64_RDRAM_SIZE_8MB;
}

/* SummerCart64 SD card paths */
#define N64_SD_ROOT     "sd:/"
#define N64_SAVE_DIR    "sd:/picodrive/saves/"
#define N64_CONFIG_DIR  "sd:/picodrive/"
#define N64_ROM_DIR     "sd:/picodrive/roms/"

/* Query functions (implemented in plat.c) */
int    n64_get_expansion_pak(void);
size_t n64_get_max_rom_size(void);

/* Logging */
void lprintf(const char *fmt, ...);

#endif /* __N64_PLATFORM_H__ */
