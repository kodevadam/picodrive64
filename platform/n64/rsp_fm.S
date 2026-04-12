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

    ##############################################
    # cmd_fm_render: DMA-only test
    #
    # Command word layout (12 bytes):
    #   a0[23:0]  = num_samples
    #   a1        = PhysicalAddr(state)  [344 bytes, 16-byte aligned]
    #   a2        = PhysicalAddr(out_buf)
    ##############################################
cmd_fm_render:
    /* Save ra - DMAIn is a jal target */
    move    v0, ra

    /* DMA state from RDRAM to DMEM */
    move    s0, a1                      /* s0 = RDRAM addr (state) */
    li      s4, %lo(chan_state)         /* s4 = DMEM addr */
    li      t0, DMA_SIZE(344, 1)        /* t0 = size (344-1 encoded) */
    jal     DMAIn
    nop

    /* Return to rspq dispatcher */
    jr      v0
    nop
