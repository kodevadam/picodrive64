/*
 * PicoDrive N64 menu supplement
 *
 * Provides N64-specific status messages and menu helpers.
 * The main menu system uses libpicofe's text menu (no PNG skins on N64).
 *
 * (C) 2026
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <string.h>

#include "../common/emu.h"
#include "../libpicofe/plat.h"

#include "n64.h"

/* Status message functions needed by the common frontend */
void plat_status_msg_busy_first(const char *msg)
{
	/* On N64, render status text directly to framebuffer */
	surface_t *fb = display_get();
	if (fb) {
		graphics_fill_screen(fb, 0);
		if (msg)
			graphics_draw_text(fb, 20, 112, msg);
		display_show(fb);
	}
}

void plat_status_msg_busy_next(const char *msg)
{
	plat_status_msg_busy_first(msg);
}

void plat_status_msg_busy_done(void)
{
}

void plat_status_msg_clear(void)
{
}

/* Debug output */
void plat_debug_cat(char *str)
{
}
