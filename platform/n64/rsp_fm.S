##################################################
# RSP FM Synthesis Overlay for PicoDrive N64
#
# All 8 YM2612 algorithms supported. Routing params
# are embedded in per-channel state (set by CPU),
# so no algo_table needed in DMEM.
##################################################

#include <rsp_queue.inc>

    .data

    RSPQ_BeginOverlayHeader
        RSPQ_DefineCommand cmd_fm_render, 12
        RSPQ_DefineCommand cmd_fm_init_tables, 12
    RSPQ_EndOverlayHeader

    RSPQ_BeginSavedState
    .align 3
sin_tab:    .space 512      # 256 x u16: phase -> log-sine
exp_tab:    .space 512      # 256 x u16: log -> linear
    RSPQ_EndSavedState

    .bss
    .align 3
chan_state:  .space 344      # 6 ch x 56 bytes + 4 hdr, padded to 8
work_out:   .space 1024     # up to 256 mono samples x 4 bytes
rdram_state_save: .space 8  # saved RDRAM state addr for DMA-back

    .text

#####################################################
# cmd_fm_init_tables
# a0 = cmd (low bits unused)
# a1 = RDRAM sin_tab (512B)
# a2 = RDRAM exp_tab (512B)
#####################################################
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

#####################################################
# rsp_op_calc: FM operator calculation
#
# sin = (phase>>16) + (pm>>1)
#
# Input:  t6=phase, t7=env, t8=pm
# Output: v1=result
# Clobbers: t0, t1, t9, s0, s1
#####################################################
rsp_op_calc:
    srl     t9, t6, 16
    sra     t0, t8, 1
    add     t9, t9, t0

    andi    s1, t9, 0x200
    andi    s0, t9, 0x100
    beq     s0, zero, .oc_nf
    nop
    xori    t9, t9, 0xff
.oc_nf:
    andi    t9, t9, 0xff

    andi    t7, t7, 0xfffe
    sll     t7, t7, 2
    sll     t0, t9, 1
    la      t1, %lo(sin_tab)
    add     t0, t1, t0
    lhu     t0, 0(t0)
    add     t7, t7, t0

    li      t0, 3328
    slt     t0, t7, t0
    beq     t0, zero, .oc_sil
    nop

    andi    t0, t7, 0xff
    sll     t0, t0, 1
    la      t1, %lo(exp_tab)
    add     t0, t1, t0
    lhu     t0, 0(t0)
    srl     t1, t7, 8
    srlv    t0, t0, t1

    bne     s1, zero, .oc_neg
    nop
    jr      ra
    move    v1, t0
.oc_neg:
    jr      ra
    sub     v1, zero, t0
.oc_sil:
    jr      ra
    move    v1, zero

#####################################################
# cmd_fm_render: MINIMAL TEST VERSION
# Just DMA in state, zero output, DMA out
# a0 = cmd | num_samples (low 16 bits)
# a1 = RDRAM rsp_fm_state (340 bytes)
# a2 = RDRAM output buffer (s32 mono)
#####################################################
cmd_fm_render:
    andi    s7, a0, 0xffff
    move    s5, a2

    # Guard: skip if no samples
    beq     s7, zero, .fm_done
    nop

    # DMA in channel state (340 bytes, rounded to 344 by HW)
    move    s0, a1
    la      s4, %lo(chan_state)
    li      t0, 339
    li      t1, 0
    jal     DMAIn
    nop

    # Clear output buffer
    la      t0, %lo(work_out)
    sll     t1, s7, 2
    add     t1, t0, t1
.clear_loop:
    sw      zero, 0(t0)
    addiu   t0, t0, 4
    bne     t0, t1, .clear_loop
    nop

    # DMA out zeroed output buffer
    move    s0, s5
    la      s4, %lo(work_out)
    sll     t0, s7, 2
    addiu   t0, t0, -1
    li      t1, 0
    jal     DMAOut
    nop

.fm_done:
    jr      ra
    nop
