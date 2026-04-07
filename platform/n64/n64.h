/*
 * PicoDrive N64 platform header
 * Constants, helpers, and hardware definitions for Nintendo 64 port
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
#define N64_AUDIO_BUFSIZE   (N64_AUDIO_RATE / 50) /* ~441 samples per frame */

/* Memory budget (8 MB with Expansion Pak) */
#define N64_RDRAM_SIZE_4MB  (4 * 1024 * 1024)
#define N64_RDRAM_SIZE_8MB  (8 * 1024 * 1024)

/* Memory pool for plat_mmap allocations */
#define N64_MEMPOOL_SIZE    (5 * 1024 * 1024) /* 5 MB for ROM + emulator data */
#define N64_DRC_POOL_SIZE   (256 * 1024)      /* 256 KB for DRC code cache */

/* Frame buffer size: 320x240x2 bytes = 150KB */
#define N64_FB_SIZE         (N64_SCREEN_WIDTH * N64_SCREEN_HEIGHT * 2)

/* Audio ring buffer: 7 chunks like PSP port */
#define N64_SND_BLOCK_COUNT 7
#define N64_SND_CHUNK_SIZE  (2 * N64_AUDIO_RATE / 50) /* stereo samples per frame */
#define N64_SND_BUF_SIZE    (N64_SND_CHUNK_SIZE * N64_SND_BLOCK_COUNT)

/* N64 controller button masks (from libdragon joypad.h) */
/* These map to joypad_buttons_t fields */
#define N64_BTN_A           0x8000
#define N64_BTN_B           0x4000
#define N64_BTN_Z           0x2000
#define N64_BTN_START       0x1000
#define N64_BTN_DU          0x0800
#define N64_BTN_DD          0x0400
#define N64_BTN_DL          0x0200
#define N64_BTN_DR          0x0100
#define N64_BTN_L           0x0020
#define N64_BTN_R           0x0010
#define N64_BTN_CU          0x0008
#define N64_BTN_CD          0x0004
#define N64_BTN_CL          0x0002
#define N64_BTN_CR          0x0001

/* Analog stick deadzone threshold */
#define N64_ANALOG_DEADZONE 20

/* VR4300 cache operations for DRC support */
static inline void n64_dcache_writeback(void *addr, unsigned long len)
{
    data_cache_hit_writeback(addr, len);
}

static inline void n64_icache_invalidate(void *addr, unsigned long len)
{
    inst_cache_hit_invalidate(addr, len);
}

/* Check if Expansion Pak is present (8 MB RAM) */
static inline int n64_has_expansion_pak(void)
{
    return get_memory_size() >= N64_RDRAM_SIZE_8MB;
}

/* ROM filesystem path prefix for flashcart SD access */
#define N64_ROM_PATH_PREFIX "sd:/"

#endif /* __N64_PLATFORM_H__ */
