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
    move    s5, a2

    beq     s7, zero, .fm_done
    nop

    # Save RDRAM state addr
    la      t0, %lo(rdram_state_save)
    sw      a1, 0(t0)

    # DMA in chan_state
    move    s0, a1
    la      s4, %lo(chan_state)
    li      t0, 339
    li      t1, 0
    jal     DMAIn
    nop

    # Clear work_out
    la      t0, %lo(work_out)
    sll     t1, s7, 2
    add     t1, t0, t1
.clear_loop:
    sw      zero, 0(t0)
    addiu   t0, t0, 4
    bne     t0, t1, .clear_loop
    nop

    # DMA out work_out (zeroed)
    move    s0, s5
    la      s4, %lo(work_out)
    sll     t0, s7, 2
    addiu   t0, t0, -1
    li      t1, 0
    jal     DMAOut
    nop

    # DMA back chan_state
    la      t0, %lo(rdram_state_save)
    lw      s0, 0(t0)
    la      s4, %lo(chan_state)
    li      t0, 339
    li      t1, 0
    jal     DMAOut
    nop

.fm_done:
    jr      ra
    nop
