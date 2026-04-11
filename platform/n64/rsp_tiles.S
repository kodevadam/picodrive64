##################################################
# RSP Tile Decoder for PicoDrive Genesis Emulator
#
# Decodes Genesis VDP 4bpp tiles into 8-bit indexed pixels.
#
# DMEM Layout:
#   0x000: num_tiles (u32)
#   0x004: rdram_src (u32) - source address for tile word DMA
#   0x008: rdram_dst (u32) - dest address for output pixel DMA
#   0x00C: status (u32) - set to 1 on completion
#   0x010-0x20F: Input tile words (up to 128 × 4 bytes = 512 bytes)
#   0x210-0x28F: Palette base per tile (up to 128 bytes)
#   0x290-0x690: Output pixels (up to 128 × 8 bytes = 1024 bytes)
##################################################

#include <rsp.inc>

    .data
    .align 3

    # Control block
num_tiles:   .word 0          # 0x000
rdram_src:   .word 0          # 0x004
rdram_dst:   .word 0          # 0x008
rsp_status:  .word 0          # 0x00C

tile_input:  .space 512       # 0x010: packed tile words
pal_info:    .space 128       # 0x210: palette base per tile
tile_output: .space 1024      # 0x290: decoded pixels

    .text
    .align 2
    .globl _start
_start:
    # Load control parameters from DMEM
    lw      t4, %lo(num_tiles)(zero)
    beq     t4, zero, done
    lw      t5, %lo(rdram_dst)(zero)     # delay slot

    # DMA in tile data from RDRAM -> DMEM
    lw      s0, %lo(rdram_src)(zero)     # RDRAM source
    li      s4, %lo(tile_input)          # DMEM dest
    sll     t0, t4, 2                    # size = count * 4
    addiu   t0, t0, -1                   # DMA length = size - 1
    li      t1, 0                        # pitch (1D transfer)
    jal     DMAIn
    nop

    # Process each tile row
    li      t6, 0                        # tile index
    li      s3, %lo(tile_input)          # input ptr (DMEM)
    li      s5, %lo(tile_output)         # output ptr (DMEM)

tile_loop:
    # Load 32-bit packed tile word from DMEM
    lw      t0, 0(s3)

    # Load palette base for this tile
    add     t3, t6, zero
    addi    t3, t3, %lo(pal_info)
    lbu     t1, 0(t3)

    # Decode 8 pixels (4bpp -> 8bpp)
    # Pixel 0: bits 15-12
    srl     t2, t0, 12
    andi    t2, t2, 0xf
    or      t2, t2, t1
    sb      t2, 0(s5)

    # Pixel 1: bits 11-8
    srl     t2, t0, 8
    andi    t2, t2, 0xf
    or      t2, t2, t1
    sb      t2, 1(s5)

    # Pixel 2: bits 7-4
    srl     t2, t0, 4
    andi    t2, t2, 0xf
    or      t2, t2, t1
    sb      t2, 2(s5)

    # Pixel 3: bits 3-0
    andi    t2, t0, 0xf
    or      t2, t2, t1
    sb      t2, 3(s5)

    # Pixel 4: bits 31-28
    srl     t2, t0, 28
    or      t2, t2, t1
    sb      t2, 4(s5)

    # Pixel 5: bits 27-24
    srl     t2, t0, 24
    andi    t2, t2, 0xf
    or      t2, t2, t1
    sb      t2, 5(s5)

    # Pixel 6: bits 23-20
    srl     t2, t0, 20
    andi    t2, t2, 0xf
    or      t2, t2, t1
    sb      t2, 6(s5)

    # Pixel 7: bits 19-16
    srl     t2, t0, 16
    andi    t2, t2, 0xf
    or      t2, t2, t1
    sb      t2, 7(s5)

    # Next tile
    addiu   s3, s3, 4
    addiu   s5, s5, 8
    addiu   t6, t6, 1
    bne     t6, t4, tile_loop
    nop

    # DMA out results: DMEM -> RDRAM
    move    s0, t5                       # RDRAM dest
    li      s4, %lo(tile_output)         # DMEM source
    sll     t0, t4, 3                    # size = count * 8
    addiu   t0, t0, -1
    li      t1, 0
    jal     DMAOut
    nop

done:
    # Signal completion
    li      t0, 1
    sw      t0, %lo(rsp_status)(zero)

    # Halt RSP
    break

#include <rsp_dma.inc>
