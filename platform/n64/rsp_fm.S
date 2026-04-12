#include <rsp_queue.inc>

    .data

    RSPQ_BeginOverlayHeader
        RSPQ_DefineCommand cmd_fm_render, 12
    RSPQ_EndOverlayHeader

    RSPQ_EmptySavedState

    .text

cmd_fm_render:
    jr      ra
    nop
