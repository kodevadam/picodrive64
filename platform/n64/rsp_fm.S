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
    move    s0, a1
    la      s4, %lo(sin_tab)
    li      t0, 511
    li      t1, 0
    jal     DMAIn
    nop
    move    s0, a2
    la      s4, %lo(exp_tab)
    li      t0, 511
    li      t1, 0
    jal     DMAIn
    nop
    jr      ra
    nop

cmd_fm_render:
    andi    s7, a0, 0xffff
    beq     s7, zero, .fm_done
    nop

    # Just DMA in state - nothing else
    move    s0, a1
    la      s4, %lo(chan_state)
    li      t0, 339
    li      t1, 0
    jal     DMAIn
    nop

.fm_done:
    jr      ra
    nop
