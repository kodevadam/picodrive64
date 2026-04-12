##################################################
# RSP FM Synthesis Overlay for PicoDrive N64
#
# All 8 YM2612 algorithms via table-driven routing.
# Routing params embedded in per-channel state struct.
##################################################

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

cmd_fm_render:
    andi    s7, a0, 0xffff
    move    s5, a2

    beq     s7, zero, .fm_done
    nop

    la      t0, %lo(rdram_state_save)
    sw      a1, 0(t0)

    move    s0, a1
    la      s4, %lo(chan_state)
    li      t0, 339
    li      t1, 0
    jal     DMAIn
    nop

    la      t0, %lo(work_out)
    sll     t1, s7, 2
    add     t1, t0, t1
.clear_loop:
    sw      zero, 0(t0)
    addiu   t0, t0, 4
    bne     t0, t1, .clear_loop
    nop

    li      s6, 0

.chan_loop:
    sll     t0, s6, 5
    sll     t1, s6, 4
    add     t0, t0, t1
    sll     t1, s6, 3
    add     t0, t0, t1
    la      t1, %lo(chan_state)
    add     s3, t1, t0

    lbu     t0, 43(s3)
    beq     t0, zero, .next_chan
    nop

    lbu     t3, 41(s3)
    lw      t4, 44(s3)
    lw      t5, 48(s3)

    li      s2, 0

.sample_loop:
    # Op1 with feedback (op_calc1: sin = (phase+pm)>>16)
    lw      t6, 0(s3)
    lhu     t7, 32(s3)
    beq     t3, zero, .no_fb
    move    t8, zero
    sll     t9, t4, 16
    add     t9, t4, t9
    sra     t8, t9, 16
    sllv    t8, t8, t3
.no_fb:
    add     t9, t6, t8
    srl     t9, t9, 16
    andi    s1, t9, 0x200
    andi    s0, t9, 0x100
    beq     s0, zero, .op1_nf
    nop
    xori    t9, t9, 0xff
.op1_nf:
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
    beq     t0, zero, .op1_sil
    nop
    andi    t0, t7, 0xff
    sll     t0, t0, 1
    la      t1, %lo(exp_tab)
    add     t0, t1, t0
    lhu     t0, 0(t0)
    srl     t1, t7, 8
    srlv    t0, t0, t1
    bne     s1, zero, .op1_neg
    nop
    move    a3, t0
    b       .op1_done
    nop
.op1_neg:
    b       .op1_done
    sub     a3, zero, t0
.op1_sil:
    move    a3, zero
.op1_done:
    sll     t4, t4, 16
    andi    t0, a3, 0xffff
    or      t4, t4, t0
    sra     $26, t4, 16

    # C1 (SLOT2): mod from offset 40 (c1_mod)
    lbu     t0, 40(s3)
    beq     t0, zero, .c1_call
    move    t8, zero
    move    t8, $26
.c1_call:
    lw      t6, 4(s3)
    lhu     t7, 34(s3)
    jal     rsp_op_calc
    nop
    move    $27, v1

    # M2 (SLOT3): mod from offset 42 (m2_mod)
    lbu     t0, 42(s3)
    beq     t0, zero, .m2_call
    move    t8, zero
    move    t8, t5
.m2_call:
    lw      t6, 8(s3)
    lhu     t7, 36(s3)
    jal     rsp_op_calc
    nop
    move    $30, v1

    # C2 (SLOT4): mod from offset 52 (c2_mod)
    lbu     t0, 52(s3)
    beq     t0, zero, .c2_call
    move    t8, zero
    li      t1, 1
    beq     t0, t1, .c2_m2
    li      t1, 2
    beq     t0, t1, .c2_op1m2
    li      t1, 3
    beq     t0, t1, .c2_memm2
    nop
    b       .c2_call
    move    t8, $26
.c2_m2:
    b       .c2_call
    move    t8, $30
.c2_op1m2:
    b       .c2_call
    add     t8, $26, $30
.c2_memm2:
    add     t8, t5, $30
.c2_call:
    lw      t6, 12(s3)
    lhu     t7, 38(s3)
    jal     rsp_op_calc
    nop

    # Output: offset 53 (out_flags)
    lbu     t0, 53(s3)
    move    a3, zero
    andi    t1, t0, 1
    beq     t1, zero, .no_o1
    nop
    add     a3, a3, $26
.no_o1:
    andi    t1, t0, 2
    beq     t1, zero, .no_oc1
    nop
    add     a3, a3, $27
.no_oc1:
    andi    t1, t0, 4
    beq     t1, zero, .no_om2
    nop
    add     a3, a3, $30
.no_om2:
    andi    t1, t0, 8
    beq     t1, zero, .no_oc2
    nop
    add     a3, a3, v1
.no_oc2:

    # Update mem: offset 54 (mem_src)
    lbu     t0, 54(s3)
    beq     t0, zero, .no_mem
    nop
    li      t1, 1
    beq     t0, t1, .mem_c1
    li      t1, 2
    beq     t0, t1, .mem_oc1
    nop
    b       .no_mem
    move    t5, $26
.mem_c1:
    b       .no_mem
    move    t5, $27
.mem_oc1:
    add     t5, $26, $27
.no_mem:

    # Write to output
    sll     t0, s2, 2
    la      t1, %lo(work_out)
    add     t0, t1, t0
    lw      t1, 0(t0)
    add     t1, t1, a3
    sw      t1, 0(t0)

    # Advance phases
    lw      t0, 0(s3)
    lw      t1, 16(s3)
    add     t0, t0, t1
    sw      t0, 0(s3)
    lw      t0, 4(s3)
    lw      t1, 20(s3)
    add     t0, t0, t1
    sw      t0, 4(s3)
    lw      t0, 8(s3)
    lw      t1, 24(s3)
    add     t0, t0, t1
    sw      t0, 8(s3)
    lw      t0, 12(s3)
    lw      t1, 28(s3)
    add     t0, t0, t1
    sw      t0, 12(s3)

    addiu   s2, s2, 1
    bne     s2, s7, .sample_loop
    nop

    sw      t4, 44(s3)
    sw      t5, 48(s3)

.next_chan:
    addiu   s6, s6, 1
    li      t0, 6
    bne     s6, t0, .chan_loop
    nop

    # DMA out work_out
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
