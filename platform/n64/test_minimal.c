/* Absolute minimal N64 test - just print text */
#include <libdragon.h>
#include <stdio.h>

int main(void)
{
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    console_init();
    console_set_render_mode(RENDER_MANUAL);
    printf("\n\n  Hello from PicoDrive64!\n\n");
    printf("  RAM: %d KB\n", get_memory_size() / 1024);
    printf("  If you see this, libdragon works.\n");
    console_render();
    for (;;) {}
}
