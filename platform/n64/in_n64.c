/*
 * PicoDrive N64 input driver
 *
 * Maps N64 controller buttons to Genesis/Mega Drive gamepad
 * using libdragon's joypad API.
 *
 * (C) 2026
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */
#include <string.h>

#include "../common/emu.h"
#include "../common/input_pico.h"
#include "../libpicofe/input.h"

#include <pico/pico_int.h>

#include "n64.h"

/* Read N64 controller state and update PicoDrive input */
void emu_update_input(void)
{
	joypad_poll();

	/* Read up to 2 controllers for multiplayer */
	int num_pads = (joypad_get_style(JOYPAD_PORT_1) != JOYPAD_STYLE_NONE) ? 1 : 0;
	if (joypad_get_style(JOYPAD_PORT_2) != JOYPAD_STYLE_NONE)
		num_pads = 2;

	for (int i = 0; i < num_pads && i < 2; i++) {
		joypad_port_t port = (i == 0) ? JOYPAD_PORT_1 : JOYPAD_PORT_2;
		joypad_buttons_t btns = joypad_get_buttons_pressed(port);
		joypad_inputs_t inputs = joypad_get_inputs(port);
		unsigned int pad = 0;

		/* Digital D-pad */
		if (btns.d_up)    pad |= 1 << GBTN_UP;
		if (btns.d_down)  pad |= 1 << GBTN_DOWN;
		if (btns.d_left)  pad |= 1 << GBTN_LEFT;
		if (btns.d_right) pad |= 1 << GBTN_RIGHT;

		/* Analog stick as D-pad with deadzone */
		if (inputs.stick_y > N64_ANALOG_DEADZONE)  pad |= 1 << GBTN_UP;
		if (inputs.stick_y < -N64_ANALOG_DEADZONE) pad |= 1 << GBTN_DOWN;
		if (inputs.stick_x < -N64_ANALOG_DEADZONE) pad |= 1 << GBTN_LEFT;
		if (inputs.stick_x > N64_ANALOG_DEADZONE)  pad |= 1 << GBTN_RIGHT;

		/* Face buttons */
		if (btns.a)     pad |= 1 << GBTN_B;     /* A -> Genesis B */
		if (btns.b)     pad |= 1 << GBTN_C;     /* B -> Genesis C */
		if (btns.z)     pad |= 1 << GBTN_A;     /* Z -> Genesis A */
		if (btns.start) pad |= 1 << GBTN_START;

		/* Shoulder buttons and C-buttons for 6-button mode */
		if (btns.l)     pad |= 1 << GBTN_X;     /* L -> Genesis X */
		if (btns.r)     pad |= 1 << GBTN_Z;     /* R -> Genesis Z */
		if (btns.c_right) pad |= 1 << GBTN_Y;   /* C-Right -> Genesis Y */

		PicoIn.pad[i] = pad;
	}

	/* C-Up for menu access */
	joypad_buttons_t p1 = joypad_get_buttons_pressed(JOYPAD_PORT_1);
	if (p1.c_up) {
		engineState = PGS_Menu;
	}
}
