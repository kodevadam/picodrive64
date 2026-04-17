// N64-specific display options menu
// Minimal - N64 has fixed 320x240 output, no scaling/filtering options

static const char *men_filter_opts[] = { "nearest", NULL };

#define MENU_OPTIONS_GFX \
	mee_onoff   ("Show FPS",            MA_OPT_SHOW_FPS,    currentConfig.EmuOpt, EOPT_SHOW_FPS),

#define MENU_OPTIONS_ADV

static menu_entry e_menu_sms_options[];
static menu_entry e_menu_keyconfig[];
