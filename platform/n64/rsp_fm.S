##################################################
# RSP FM Synthesis Overlay for PicoDrive N64
#
# Generates YM2612 FM audio samples on the RSP,
# freeing the CPU from the synthesis hot loop.
#
# All 8 YM2612 algorithms supported via table-driven
# routing. CPU provides per-channel state (phases,
# increments, envelope levels, algorithm, feedback).
# RSP computes samples using decomposed sin/exp
# tables (1KB in DMEM).
#
# DMEM Layout:
#   Overlay header + algo_table
#   Saved state: sin_tab + exp_tab (1KB)
#   BSS: chan_state + work_out
##################################################

#include <rsp_queue.inc>

    .data

    RSPQ_BeginOverlayHeader
        RSPQ_DefineCommand cmd_fm_render, 12
        RSPQ_DefineCommand cmd_fm_init_tables, 8
    RSPQ_EndOverlayHeader

    ##################################################
    # Algorithm routing table: 8 entries x 8 bytes
    #
    # byte 0: C1_mod    0=zero, 1=op1s
    # byte 1: M2_mod    0=zero, 1=prev_mem
    # byte 2: C2_mod    0=zero, 1=M2, 2=op1s+M2, 3=mem+M2, 4=op1s
    # byte 3: out_flags bit0=op1s bit1=C1 bit2=M2 bit3=C2
    # byte 4: mem_src   0=none, 1=C1, 2=op1s+C1, 3=op1s
    # bytes 5-7: pad
    ##################################################
    .align 3
algo_table:
    .byte 1,1,1, 0x08, 1, 0,0,0    /* algo 0: M1->C1->mem->M2->C2 */
    .byte 0,1,1, 0x08, 2, 0,0,0    /* algo 1: (M1+C1)->mem->M2->C2 */
    .byte 0,1,2, 0x08, 1, 0,0,0    /* algo 2: C1->mem,(M1+M2)->C2 */
    .byte 1,0,3, 0x08, 1, 0,0,0    /* algo 3: M1->C1->mem,(mem+M2)->C2 */
    .byte 1,0,1, 0x0A, 0, 0,0,0    /* algo 4: (M1->C1)+(M2->C2) */
    .byte 1,1,4, 0x0E, 3, 0,0,0    /* algo 5: M1->(C1+M2+C2) */
    .byte 1,0,0, 0x0E, 0, 0,0,0    /* algo 6: (M1->C1)+M2+C2 */
    .byte 0,0,0, 0x0F, 0, 0,0,0    /* algo 7: M1+C1+M2+C2 */

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
# cmd_fm_init_tables: DMA sin_tab and exp_tab
# a1 = RDRAM sin_tab (512B), a2 = RDRAM exp_tab (512B)
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
# rsp_op_calc: FM operator calculation subroutine
#
# sin = (phase>>16) + (pm>>1)
# Lookup sin_tab, exp_tab, apply envelope
#
# Input:  t6=phase, t7=env, t8=pm
# Output: v1=result
# Clobbers: t0, t1, t9, s0, s1
#####################################################
rsp_op_calc:
    srl     t9, t6, 16         # phase >> 16
    sra     t0, t8, 1          # pm >> 1
    add     t9, t9, t0         # sin = (phase>>16) + (pm>>1)

    andi    s1, t9, 0x200      # neg flag
    andi    s0, t9, 0x100      # mirror flag
    beq     s0, zero, .oc_no_flip
    nop
    xori    t9, t9, 0xff
.oc_no_flip:
    andi    t9, t9, 0xff       # sin index

    andi    t7, t7, 0xfffe     # env &= ~1
    sll     t7, t7, 2          # env << 2
    sll     t0, t9, 1          # halfword offset
    la      t1, %lo(sin_tab)
    add     t0, t1, t0
    lhu     t0, 0(t0)
    add     t7, t7, t0         # p = (env<<2) + sin_tab[sin]

    li      t0, 3328
    slt     t0, t7, t0
    beq     t0, zero, .oc_silent
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
.oc_silent:
    jr      ra
    move    v1, zero

#####################################################
# cmd_fm_render: Generate FM audio samples
# a0 = cmd | num_samples (low 16 bits)
# a1 = RDRAM rsp_fm_state (340 bytes)
# a2 = RDRAM output buffer (s32 mono)
#####################################################
cmd_fm_render:
    andi    s7, a0, 0xffff      # s7 = num_samples
    move    s5, a2              # s5 = RDRAM output address

    # Save RDRAM state address for DMA-back
    la      t0, %lo(rdram_state_save)
    sw      a1, 0(t0)

    # DMA in channel state
    move    s0, a1
    la      s4, %lo(chan_state)
    li      t0, 343             # 344 bytes - 1
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

    # Process 6 channels
    li      s6, 0

.chan_loop:
    # s3 = &chan_state[s6]: offset = s6 * 56 = (s6<<5)+(s6<<4)+(s6<<3)
    sll     t0, s6, 5          # *32
    sll     t1, s6, 4          # *16
    add     t0, t0, t1         # *48
    sll     t1, s6, 3          # *8
    add     t0, t0, t1         # *56
    la      t1, %lo(chan_state)
    add     s3, t1, t0

    # Check enabled (offset 43)
    lbu     t0, 43(s3)
    beq     t0, zero, .next_chan
    nop

    # Load channel params
    lbu     t2, 40(s3)         # algo (0-7)
    lbu     t3, 41(s3)         # fb_shift
    lw      t4, 44(s3)         # op1_out
    lw      t5, 48(s3)         # mem

    # Compute algo table pointer -> $28 (gp)
    sll     t0, t2, 3          # algo * 8
    la      t1, %lo(algo_table)
    add     $28, t1, t0

    li      s2, 0              # sample index

.sample_loop:
    ###############################################
    # Op1 (M1) with feedback: op_calc1 formula
    # sin = (phase + pm) >> 16
    ###############################################
    lw      t6, 0(s3)         # phase[0]
    lhu     t7, 32(s3)        # vol_out[0]

    # Feedback: pm = ((op1_out + (op1_out<<16)) >> 16) << fb_shift
    beq     t3, zero, .no_fb
    move    t8, zero
    sll     t9, t4, 16
    add     t9, t4, t9
    sra     t8, t9, 16
    sllv    t8, t8, t3
.no_fb:

    # op_calc1: sin = (phase + pm) >> 16
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

    # Update op1_out: (old << 16) | (new & 0xffff)
    sll     t4, t4, 16
    andi    t0, a3, 0xffff
    or      t4, t4, t0

    # $26 = op1_out >> 16 (signed) - used as modulation source
    sra     $26, t4, 16

    ###############################################
    # C1 (SLOT2): mod from algo_table[0]
    ###############################################
    lbu     t0, 0($28)
    beq     t0, zero, .c1_call
    move    t8, zero           # pm = 0 (delay slot)
    move    t8, $26            # pm = op1s
.c1_call:
    lw      t6, 4(s3)
    lhu     t7, 34(s3)
    jal     rsp_op_calc
    nop
    move    $27, v1            # $27 = C1_out

    ###############################################
    # M2 (SLOT3): mod from algo_table[1]
    ###############################################
    lbu     t0, 1($28)
    beq     t0, zero, .m2_call
    move    t8, zero
    move    t8, t5             # pm = prev_mem
.m2_call:
    lw      t6, 8(s3)
    lhu     t7, 36(s3)
    jal     rsp_op_calc
    nop
    move    $30, v1            # $30 = M2_out

    ###############################################
    # C2 (SLOT4): mod from algo_table[2]
    # 0=zero, 1=M2, 2=op1s+M2, 3=mem+M2, 4=op1s
    ###############################################
    lbu     t0, 2($28)
    beq     t0, zero, .c2_call
    move    t8, zero           # pm = 0

    li      t1, 1
    beq     t0, t1, .c2_m2
    li      t1, 2
    beq     t0, t1, .c2_op1m2
    li      t1, 3
    beq     t0, t1, .c2_memm2
    nop
    # type 4: op1s
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
    # v1 = C2_out

    ###############################################
    # Output accumulation: algo_table[3]
    ###############################################
    lbu     t0, 3($28)
    move    a3, zero           # output accumulator

    andi    t1, t0, 1          # bit0: op1s
    beq     t1, zero, .no_o1
    nop
    add     a3, a3, $26
.no_o1:
    andi    t1, t0, 2          # bit1: C1
    beq     t1, zero, .no_oc1
    nop
    add     a3, a3, $27
.no_oc1:
    andi    t1, t0, 4          # bit2: M2
    beq     t1, zero, .no_om2
    nop
    add     a3, a3, $30
.no_om2:
    andi    t1, t0, 8          # bit3: C2
    beq     t1, zero, .no_oc2
    nop
    add     a3, a3, v1
.no_oc2:

    ###############################################
    # Update mem: algo_table[4]
    # 0=none, 1=C1, 2=op1s+C1, 3=op1s
    ###############################################
    lbu     t0, 4($28)
    beq     t0, zero, .no_mem
    nop
    li      t1, 1
    beq     t0, t1, .mem_c1
    li      t1, 2
    beq     t0, t1, .mem_oc1
    nop
    # type 3: op1s
    b       .no_mem
    move    t5, $26
.mem_c1:
    b       .no_mem
    move    t5, $27
.mem_oc1:
    add     t5, $26, $27
.no_mem:

    ###############################################
    # Write to output buffer
    ###############################################
    sll     t0, s2, 2
    la      t1, %lo(work_out)
    add     t0, t1, t0
    lw      t1, 0(t0)
    add     t1, t1, a3
    sw      t1, 0(t0)

    ###############################################
    # Advance phases
    ###############################################
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

    # Next sample
    addiu   s2, s2, 1
    bne     s2, s7, .sample_loop
    nop

    # Store back op1_out and mem
    sw      t4, 44(s3)
    sw      t5, 48(s3)

.next_chan:
    addiu   s6, s6, 1
    li      t0, 6
    bne     s6, t0, .chan_loop
    nop

    # DMA out work_out -> RDRAM output
    move    s0, s5
    la      s4, %lo(work_out)
    sll     t0, s7, 2
    addiu   t0, t0, -1
    li      t1, 0
    jal     DMAOut
    nop

    # DMA back chan_state -> RDRAM (for op1_out/mem writeback)
    la      t0, %lo(rdram_state_save)
    lw      s0, 0(t0)
    la      s4, %lo(chan_state)
    li      t0, 343
    li      t1, 0
    jal     DMAOut
    nop

    jr      ra
    nop
