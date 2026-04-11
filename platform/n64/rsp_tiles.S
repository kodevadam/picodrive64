##################################################
# RSP Palette Converter for PicoDrive Genesis Emulator
#
# Converts 8-bit indexed pixels (HighCol/screen_buffer) to
# 16-bit RGBA5551 (N64 display format) using palette lookup.
# Runs in parallel with VR4300 CPU doing next frame's emulation.
#
# DMEM Layout:
#   0x000: Control block (16 bytes)
#     0x000: RDRAM source address (8-bit indexed pixels)
#     0x004: RDRAM dest address (16-bit RGBA5551 output)
#     0x008: Number of pixels to convert
#     0x00C: Status (set to 1 on completion)
#   0x010-0x20F: Palette (256 entries × 2 bytes = 512 bytes)
#   0x210-0x3FF: Work buffer (up to 480 bytes for pixel batches)
#
# The RSP DMAs in batches of pixels, looks up each in the palette,
# and DMAs out the 16-bit results.
##################################################

#include <rsp.inc>

    .data
    .align 3

# Control block
rdram_src:      .word 0          # 0x000: source address
rdram_dst:      .word 0          # 0x004: dest address
num_pixels:     .word 0          # 0x008: pixel count
rsp_status:     .word 0          # 0x00C: completion flag

# Palette: 256 × 16-bit RGBA5551 entries
palette:        .space 512       # 0x010-0x20F

# Work buffers
pixel_in:       .space 256       # 0x210-0x30F: input batch (256 bytes)
pixel_out:      .space 512       # 0x310-0x50F: output batch (256 × 2 bytes)

    .text
    .align 2
    .globl _start
_start:
    # Load control
    lw      s0, %lo(rdram_src)(zero)     # RDRAM source
    lw      s1, %lo(rdram_dst)(zero)     # RDRAM dest
    lw      s2, %lo(num_pixels)(zero)    # total pixels
    beq     s2, zero, done
    nop

    # Process in batches of 256 pixels
    li      s3, 0                        # pixels processed

batch_loop:
    # Calculate batch size: min(256, remaining)
    sub     t4, s2, s3                   # remaining = total - processed
    slti    t0, t4, 257                  # t0 = (remaining < 257)
    beq     t0, zero, use_256
    li      t3, 256                      # batch = 256 (delay slot)
    move    t3, t4                       # batch = remaining
use_256:

    # DMA in pixel batch: RDRAM -> DMEM
    add     t0, s0, s3                   # src = rdram_src + processed
    move    s0, t0                       # save for later (clobbers s0)
    # Restore s0 after DMA
    lw      s0, %lo(rdram_src)(zero)

    add     t0, s0, s3                   # recalculate src
    sw      t0, %lo(rdram_src)(zero)     # temp store
    lw      s0, %lo(rdram_src)(zero)     # reload

    # Actually do the DMA: s0=RDRAM addr, s4=DMEM addr, t0=size-1
    add     s0, s0, s3                   # s0 = rdram_src + processed
    li      s4, %lo(pixel_in)
    addiu   t0, t3, -1                   # size - 1
    li      t1, 0
    jal     DMAIn
    nop

    # Reload s0 (clobbered by DMA)
    lw      s0, %lo(rdram_src)(zero)

    # Convert pixels: 8-bit index -> 16-bit RGBA5551 via palette
    li      t5, 0                        # batch offset
    li      t6, %lo(pixel_in)            # input ptr
    li      t7, %lo(pixel_out)           # output ptr

convert_loop:
    # Load one pixel byte
    add     t0, t6, t5
    lbu     t0, 0(t0)                    # t0 = palette index

    # Lookup in palette: each entry is 2 bytes
    sll     t1, t0, 1                    # t1 = index * 2
    addi    t1, t1, %lo(palette)
    lhu     t0, 0(t1)                    # t0 = RGBA5551 color

    # Store 16-bit result
    sll     t1, t5, 1                    # output offset = batch_offset * 2
    add     t1, t7, t1
    sh      t0, 0(t1)                    # store halfword

    # Next pixel
    addiu   t5, t5, 1
    bne     t5, t3, convert_loop
    nop

    # DMA out converted batch: DMEM -> RDRAM
    sll     t0, s3, 1                    # dest offset = processed * 2
    add     s0, s1, t0                   # s0 = rdram_dst + processed*2
    li      s4, %lo(pixel_out)
    sll     t0, t3, 1                    # size = batch * 2
    addiu   t0, t0, -1
    li      t1, 0
    jal     DMAOut
    nop

    # Reload s0, s1
    lw      s0, %lo(rdram_src)(zero)
    lw      s1, %lo(rdram_dst)(zero)

    # Advance
    add     s3, s3, t3                   # processed += batch
    slt     t0, s3, s2                   # more to do?
    bne     t0, zero, batch_loop
    nop

done:
    li      t0, 1
    sw      t0, %lo(rsp_status)(zero)
    break

#include <rsp_dma.inc>
