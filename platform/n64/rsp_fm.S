##################################################
# RSP FM Synthesis Overlay for PicoDrive N64
# DEBUG: Minimal overlay to test overlay switching
##################################################

#include <rsp_queue.inc>

    .data

    RSPQ_BeginOverlayHeader
        RSPQ_DefineCommand cmd_fm_render, 12
    RSPQ_EndOverlayHeader

    RSPQ_EmptySavedState

    .text

#####################################################
# cmd_fm_render: NO-OP - just return
#####################################################
cmd_fm_render:
    jr      ra
    nop
