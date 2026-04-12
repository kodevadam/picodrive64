#include <rsp_queue.inc>

    .data

    RSPQ_BeginOverlayHeader
        RSPQ_DefineCommand cmd_fm_render, 12
        RSPQ_DefineCommand cmd_fm_init_tables, 12
    RSPQ_EndOverlayHeader

    RSPQ_BeginSavedState
    .align 3
sin_tab:    .space 512
exp_tab:    .space 512
    RSPQ_EndSavedState

    .bss
    .align 3
chan_state:  .space 344
work_out:   .space 1024
rdram_state_save: .space 8

    .text

cmd_fm_init_tables:
    jr      ra
    nop

cmd_fm_render:
    jr      ra
    nop
