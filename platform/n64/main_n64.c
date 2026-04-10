/*
 * PicoDrive N64 - Standalone main with embedded ROM
 *
 * Bypasses the file-based ROM loading entirely. The Genesis ROM
 * is compiled directly into the N64 binary and loaded from memory.
 * This is for testing without SD card / filesystem support.
 *
 * (C) 2026
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../libpicofe/input.h"
#include "../libpicofe/plat.h"
#include "../libpicofe/sndout.h"
#include "../common/emu.h"
#include "../common/input_pico.h"
#include "../common/version.h"

#include <pico/pico_int.h>

#include "n64.h"
#include "in_n64.h"
#include "embedded_rom.h"

char **g_argv;

int main(int argc, char *argv[])
{
	unsigned char *rom_copy;

	g_argv = argv;

	/* Early platform init */
	plat_early_init();

	/* Initialize input system */
	in_init();

	/* Initialize N64 hardware (display, joypad, etc.) */
	plat_target_init();

	/* Initialize platform */
	plat_init();

	/* Initialize sound output */
	sndout_init();

	/* Setup input driver */
	plat_target_setup_input();

	/* Set default configuration tuned for N64 */
	emu_prep_defconfig();
	emu_set_defconfig();

	/* Initialize PicoDrive core */
	PicoInit();

	/* Configure for Genesis/Mega Drive */
	PicoIn.opt |= POPT_EN_FM | POPT_EN_PSG | POPT_EN_STEREO;
	PicoIn.opt |= POPT_EN_DRC;
	PicoIn.sndRate = currentConfig.s_PsndRate;

	/* Copy embedded ROM to an allocated buffer (PicoCartInsert takes ownership) */
	rom_copy = (unsigned char *)plat_mmap(0, EMBEDDED_ROM_SIZE, 0, 0);
	if (!rom_copy) {
		lprintf("Failed to allocate ROM buffer!\n");
		return 1;
	}
	memcpy(rom_copy, embedded_rom_data, EMBEDDED_ROM_SIZE);

	/* Insert the ROM cartridge */
	lprintf("Loading embedded ROM: %s (%d bytes)\n", EMBEDDED_ROM_NAME, EMBEDDED_ROM_SIZE);
	if (PicoCartInsert(rom_copy, EMBEDDED_ROM_SIZE, NULL)) {
		lprintf("PicoCartInsert failed!\n");
		return 1;
	}

	/* Power on and reset */
	PicoPower();
	PicoReset();
	PicoLoopPrepare();

	/* Set up video output */
	PicoDrawSetOutFormat(PDF_RGB555, 0);
	PicoDrawSetOutBuf(g_screen_ptr, g_screen_ppitch * 2);

	/* Start sound */
	sndout_start(currentConfig.s_PsndRate, 1);

	lprintf("Starting emulation: %s\n", EMBEDDED_ROM_NAME);

	/* Main emulation loop */
	for (;;) {
		PicoFrame();
		plat_video_flip();

		/* Check for input */
		joypad_poll();
		joypad_buttons_t btns = joypad_get_buttons_pressed(JOYPAD_PORT_1);

		/* D-pad + buttons -> Genesis pad */
		unsigned int pad = 0;
		joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);

		if (btns.d_up || inputs.stick_y > N64_ANALOG_DEADZONE)  pad |= 1 << GBTN_UP;
		if (btns.d_down || inputs.stick_y < -N64_ANALOG_DEADZONE) pad |= 1 << GBTN_DOWN;
		if (btns.d_left || inputs.stick_x < -N64_ANALOG_DEADZONE) pad |= 1 << GBTN_LEFT;
		if (btns.d_right || inputs.stick_x > N64_ANALOG_DEADZONE) pad |= 1 << GBTN_RIGHT;
		if (btns.a)     pad |= 1 << GBTN_B;
		if (btns.b)     pad |= 1 << GBTN_C;
		if (btns.z)     pad |= 1 << GBTN_A;
		if (btns.start) pad |= 1 << GBTN_START;
		if (btns.l)     pad |= 1 << GBTN_X;
		if (btns.r)     pad |= 1 << GBTN_Z;
		if (btns.c_right) pad |= 1 << GBTN_Y;

		PicoIn.pad[0] = pad;

		/* Audio output */
		if (PicoIn.sndOut) {
			sndout_write_nb(PicoIn.sndOut, (PicoIn.sndRate / 60) * 4);
		}
	}

	return 0;
}
