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
rdram_state: .space 4
rdram_out:   .space 4

    .text

cmd_fm_init_tables:
    jr      ra
    nop

cmd_fm_render:
    move    v0, ra

    /* Save RDRAM addresses */
    sw      a1, %lo(rdram_state)
    sw      a2, %lo(rdram_out)
    andi    k1, a0, 0xFFFF

    /* DMA state IN */
    move    s0, a1
    li      s4, %lo(chan_state)
    li      t0, DMA_SIZE(344, 1)
    jal     DMAIn
    nop

    /* DMA state back OUT (unchanged) */
    lw      s0, %lo(rdram_state)
    li      s4, %lo(chan_state)
    li      t0, DMA_SIZE(344, 1)
    jal     DMAOut
    nop

    /* DMA output buffer OUT (zeroed/garbage, doesn't matter) */
    lw      s0, %lo(rdram_out)
    li      s4, %lo(work_out)
    sll     t0, k1, 2
    addiu   t0, t0, 7
    andi    t0, t0, 0xFFF8
    addiu   t0, t0, -1
    jal     DMAOut
    nop

    jr      v0
    nop
