/*
 * Stub readpng implementation for N64 (no libpng available)
 *
 * Returns failure for all PNG operations. The emulator menu
 * will work with text-only rendering (no skin images).
 *
 * (C) 2026
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */
#include <stdio.h>
#include "../libpicofe/readpng.h"

int readpng(void *dest, const char *fname, readpng_what what, int req_w, int req_h)
{
	/* No PNG support on N64 - skin images won't load */
	return -1;
}

int writepngpp(const char *fname, unsigned short *src, int w, int h, int pitch)
{
	return -1;
}

int writepng(const char *fname, unsigned short *src, int w, int h)
{
	return -1;
}
