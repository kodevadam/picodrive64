/*
 * PicoDrive input driver for Nintendo 64
 *
 * Maps N64 controller buttons to Genesis/Mega Drive gamepad
 * using libdragon's joypad API. Implements libpicofe input driver
 * interface for menu navigation and game input.
 *
 * (C) 2026
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../libpicofe/input.h"
#include "../common/input_pico.h"
#include <pico/pico_int.h>

#include "n64.h"

#define IN_N64_PREFIX "n64:"
#define IN_N64_NBUTTONS 16

static int in_n64_combo_keys = 0;
static int in_n64_combo_acts = 0;

/* Button bit positions matching joypad_buttons_t layout */
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
};

static const char *in_n64_keys[IN_N64_NBUTTONS] = {
	[N64_BIT_A]       = "A",
	[N64_BIT_B]       = "B",
	[N64_BIT_Z]       = "Z",
	[N64_BIT_START]   = "Start",
	[N64_BIT_DU]      = "D-Up",
	[N64_BIT_DD]      = "D-Down",
	[N64_BIT_DL]      = "D-Left",
	[N64_BIT_DR]      = "D-Right",
	[N64_BIT_L]       = "L",
	[N64_BIT_R]       = "R",
	[N64_BIT_CU]      = "C-Up",
	[N64_BIT_CD]      = "C-Down",
	[N64_BIT_CL]      = "C-Left",
	[N64_BIT_CR]      = "C-Right",
	[N64_BIT_NUBUP]   = "Stick Up",
	[N64_BIT_NUBDOWN] = "Stick Down",
};

/* Default bindings: N64 buttons -> Genesis buttons */
static struct in_default_bind in_n64_defbinds[] = {
	/* D-pad */
	{ N64_BIT_DU,      IN_BINDTYPE_PLAYER12, GBTN_UP },
	{ N64_BIT_DD,      IN_BINDTYPE_PLAYER12, GBTN_DOWN },
	{ N64_BIT_DL,      IN_BINDTYPE_PLAYER12, GBTN_LEFT },
	{ N64_BIT_DR,      IN_BINDTYPE_PLAYER12, GBTN_RIGHT },
	/* A->B, B->C, Z->A (natural mapping for 3-button Genesis) */
	{ N64_BIT_A,       IN_BINDTYPE_PLAYER12, GBTN_B },
	{ N64_BIT_B,       IN_BINDTYPE_PLAYER12, GBTN_C },
	{ N64_BIT_Z,       IN_BINDTYPE_PLAYER12, GBTN_A },
	/* Shoulder + C-buttons for 6-button mode */
	{ N64_BIT_L,       IN_BINDTYPE_PLAYER12, GBTN_X },
	{ N64_BIT_R,       IN_BINDTYPE_PLAYER12, GBTN_Z },
	{ N64_BIT_CR,      IN_BINDTYPE_PLAYER12, GBTN_Y },
	/* Start */
	{ N64_BIT_START,   IN_BINDTYPE_PLAYER12, GBTN_START },
	/* Emulator controls */
	{ N64_BIT_CU,      IN_BINDTYPE_EMU, PEVB_MENU },
	{ N64_BIT_CD,      IN_BINDTYPE_EMU, PEVB_STATE_SAVE },
	{ N64_BIT_CL,      IN_BINDTYPE_EMU, PEVB_STATE_LOAD },
	{ 0, 0, 0 }
};

/* Read all button states as a bitmask */
static unsigned in_n64_get_bits(void)
{
	unsigned keys = 0;
	joypad_buttons_t btns;
	joypad_inputs_t inputs;

	joypad_poll();
	btns = joypad_get_buttons_pressed(JOYPAD_PORT_1);
	inputs = joypad_get_inputs(JOYPAD_PORT_1);

	if (btns.a)       keys |= (1 << N64_BIT_A);
	if (btns.b)       keys |= (1 << N64_BIT_B);
	if (btns.z)       keys |= (1 << N64_BIT_Z);
	if (btns.start)   keys |= (1 << N64_BIT_START);
	if (btns.d_up)    keys |= (1 << N64_BIT_DU);
	if (btns.d_down)  keys |= (1 << N64_BIT_DD);
	if (btns.d_left)  keys |= (1 << N64_BIT_DL);
	if (btns.d_right) keys |= (1 << N64_BIT_DR);
	if (btns.l)       keys |= (1 << N64_BIT_L);
	if (btns.r)       keys |= (1 << N64_BIT_R);
	if (btns.c_up)    keys |= (1 << N64_BIT_CU);
	if (btns.c_down)  keys |= (1 << N64_BIT_CD);
	if (btns.c_left)  keys |= (1 << N64_BIT_CL);
	if (btns.c_right) keys |= (1 << N64_BIT_CR);

	/* Analog stick as digital with deadzone */
	if (inputs.stick_y > N64_ANALOG_DEADZONE)  keys |= (1 << N64_BIT_DU);
	if (inputs.stick_y < -N64_ANALOG_DEADZONE) keys |= (1 << N64_BIT_DD);
	if (inputs.stick_x < -N64_ANALOG_DEADZONE) keys |= (1 << N64_BIT_DL);
	if (inputs.stick_x > N64_ANALOG_DEADZONE)  keys |= (1 << N64_BIT_DR);

	return keys;
}

static void in_n64_probe(const in_drv_t *drv)
{
	in_register(IN_N64_PREFIX "N64 pad", -1, NULL,
		IN_N64_NBUTTONS, in_n64_keys, 1);
}

static void in_n64_free(void *drv_data)
{
}

static const char * const *
in_n64_get_key_names(const in_drv_t *drv, int *count)
{
	*count = IN_N64_NBUTTONS;
	return in_n64_keys;
}

static int in_n64_update(void *drv_data, const int *binds, int *result)
{
	int type_start = 0;
	int i, t;
	unsigned keys;

	keys = in_n64_get_bits();

	if (keys & in_n64_combo_keys) {
		result[IN_BINDTYPE_EMU] = in_combos_do(keys, binds, IN_N64_NBUTTONS,
						in_n64_combo_keys, in_n64_combo_acts);
		type_start = IN_BINDTYPE_PLAYER12;
	}

	for (i = 0; keys; i++, keys >>= 1) {
		if (!(keys & 1))
			continue;
		for (t = type_start; t < IN_BINDTYPE_COUNT; t++)
			result[t] |= binds[IN_BIND_OFFS(i, t)];
	}

	return 0;
}

static int in_n64_update_keycode(void *data, int *is_down)
{
	static unsigned old_val = 0;
	unsigned val, diff, i;

	val = in_n64_get_bits();
	diff = val ^ old_val;
	if (diff == 0)
		return -1;

	for (i = 0; i < sizeof(diff)*8; i++)
		if (diff & (1<<i))
			break;

	old_val ^= 1 << i;

	if (is_down)
		*is_down = !!(val & (1<<i));
	return i;
}

/* Menu button mapping: N64 -> libpicofe menu buttons */
static struct {
	unsigned key;
	int pbtn;
} key_pbtn_map[] = {
	{ N64_BIT_DU,    PBTN_UP },
	{ N64_BIT_DD,    PBTN_DOWN },
	{ N64_BIT_DL,    PBTN_LEFT },
	{ N64_BIT_DR,    PBTN_RIGHT },
	{ N64_BIT_A,     PBTN_MOK },
	{ N64_BIT_B,     PBTN_MBACK },
	{ N64_BIT_Z,     PBTN_MA2 },
	{ N64_BIT_START, PBTN_MA3 },
	{ N64_BIT_L,     PBTN_L },
	{ N64_BIT_R,     PBTN_R },
};

#define KEY_PBTN_MAP_SIZE (sizeof(key_pbtn_map) / sizeof(key_pbtn_map[0]))

static int in_n64_menu_translate(void *drv_data, int keycode, char *charcode)
{
	int i;
	if (keycode < 0) {
		keycode = -keycode;
		for (i = 0; i < KEY_PBTN_MAP_SIZE; i++)
			if (key_pbtn_map[i].pbtn == keycode)
				return key_pbtn_map[i].key;
	} else {
		for (i = 0; i < KEY_PBTN_MAP_SIZE; i++)
			if (key_pbtn_map[i].key == (unsigned)keycode)
				return key_pbtn_map[i].pbtn;
	}
	return 0;
}

static int in_n64_clean_binds(void *drv_data, int *binds, int *def_binds)
{
	int i, count = 0;

	for (i = 0; i < IN_N64_NBUTTONS; i++) {
		int t, offs;
		for (t = 0; t < IN_BINDTYPE_COUNT; t++) {
			offs = IN_BIND_OFFS(i, t);
			if (in_n64_keys[i] == NULL)
				binds[offs] = def_binds[offs] = 0;
			if (binds[offs])
				count++;
		}
	}

	in_combos_find(binds, IN_N64_NBUTTONS, &in_n64_combo_keys, &in_n64_combo_acts);
	return count;
}

static const in_drv_t in_n64_drv = {
	.prefix         = IN_N64_PREFIX,
	.probe          = in_n64_probe,
	.free           = in_n64_free,
	.get_key_names  = in_n64_get_key_names,
	.clean_binds    = in_n64_clean_binds,
	.update         = in_n64_update,
	.update_keycode = in_n64_update_keycode,
	.menu_translate = in_n64_menu_translate,
};

void in_n64_init(struct in_default_bind *defbinds)
{
	in_n64_combo_keys = in_n64_combo_acts = 0;
	in_register_driver(&in_n64_drv, defbinds, NULL, NULL);
}

/* Called from common emu.c to read gamepad state for all controllers */
void emu_update_input(void)
{
	/* Input is handled through the libpicofe input driver above.
	 * For multiplayer, we also read controllers 2-4 directly.
	 */
	joypad_poll();

	/* Controllers 2-4 for multiplayer */
	joypad_port_t ports[] = { JOYPAD_PORT_2, JOYPAD_PORT_3, JOYPAD_PORT_4 };
	int pad_idx;

	for (pad_idx = 0; pad_idx < 3; pad_idx++) {
		if (joypad_get_style(ports[pad_idx]) == JOYPAD_STYLE_NONE)
			continue;

		joypad_buttons_t btns = joypad_get_buttons_pressed(ports[pad_idx]);
		joypad_inputs_t inputs = joypad_get_inputs(ports[pad_idx]);
		unsigned int pad = 0;

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

		/* pad_idx 0=port2, 1=port3, 2=port4 -> PicoIn.pad[1..3] */
		PicoIn.pad[pad_idx + 1] = pad;
	}
}
