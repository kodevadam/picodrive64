/*
 * PicoDrive 68k Dynamic Recompiler for MIPS III (VR4300/N64)
 *
 * Phase 1: Minimal hybrid dynarec
 * - Compiles basic blocks for common opcodes
 * - Falls back to FAME interpreter for unsupported ops
 * - Uses emit_mips.c for native code generation
 *
 * (C) 2026
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <pico/pico_int.h>
#include "cmn.h"
#include "drc68k.h"

/* Stubs/defines needed by emit_mips.c */
#define COUNT_OP   /* no-op: profiling counter not used */
#define EMIT_CACHE 0
#define host_instructions_updated(a, b, c) \
	cache_flush_d_inval_i(a, b)

static u32 *tcache_ptr;
#define EMIT(x) *tcache_ptr++ = (x)

/* MIPS instruction encoding helpers */
#define MIPS_INSN(op, rs, rt, rd, sa, fn) \
	(((op)<<26)|((rs)<<21)|((rt)<<16)|((rd)<<11)|((sa)<<6)|((fn)<<0))

/* R-type: SPECIAL (op=0) */
#define MIPS_ADDU(rd,rs,rt) MIPS_INSN(0,rs,rt,rd,0,0x21)
#define MIPS_SUBU(rd,rs,rt) MIPS_INSN(0,rs,rt,rd,0,0x23)
#define MIPS_AND(rd,rs,rt)  MIPS_INSN(0,rs,rt,rd,0,0x24)
#define MIPS_OR(rd,rs,rt)   MIPS_INSN(0,rs,rt,rd,0,0x25)
#define MIPS_XOR(rd,rs,rt)  MIPS_INSN(0,rs,rt,rd,0,0x26)
#define MIPS_SLTU(rd,rs,rt) MIPS_INSN(0,rs,rt,rd,0,0x2b)
#define MIPS_JR(rs)         MIPS_INSN(0,rs,0,0,0,0x08)
#define MIPS_SLL(rd,rt,sa)  MIPS_INSN(0,0,rt,rd,sa,0x00)
#define MIPS_SRL(rd,rt,sa)  MIPS_INSN(0,0,rt,rd,sa,0x02)
#define MIPS_SRA(rd,rt,sa)  MIPS_INSN(0,0,rt,rd,sa,0x03)
#define MIPS_NOP            0

/* I-type */
#define MIPS_ADDIU(rt,rs,imm) MIPS_INSN(9,rs,rt,0,0,(u16)(imm))
#define MIPS_ANDI(rt,rs,imm)  MIPS_INSN(12,rs,rt,0,0,(u16)(imm))
#define MIPS_ORI(rt,rs,imm)   MIPS_INSN(13,rs,rt,0,0,(u16)(imm))
#define MIPS_LUI(rt,imm)      MIPS_INSN(15,0,rt,0,0,(u16)(imm))
#define MIPS_LW(rt,off,base)  MIPS_INSN(35,base,rt,0,0,(u16)(off))
#define MIPS_SW(rt,off,base)  MIPS_INSN(43,base,rt,0,0,(u16)(off))
#define MIPS_BEQ(rs,rt,off)   MIPS_INSN(4,rs,rt,0,0,(u16)(off))
#define MIPS_BNE(rs,rt,off)   MIPS_INSN(5,rs,rt,0,0,(u16)(off))
#define MIPS_BLEZ(rs,off)     MIPS_INSN(6,rs,0,0,0,(u16)(off))

/* Registers */
#define Z0 0
#define SP 29
#define LR 31

/* ======== DRC State ======== */

drc68k_state_t drc68k;

/* ======== 68k Register Mapping ======== */

/*
 * 68k has 16 registers (D0-D7, A0-A7) + PC + SR/CCR.
 * MIPS has limited registers. Strategy:
 * - Keep 68k regs in a memory context block (M68K_CONTEXT)
 * - Load into MIPS temporaries for each compiled instruction
 * - Write back after modification
 * - Lazy flag computation: store result, derive NZVC when needed
 *
 * MIPS register usage during compiled blocks:
 *   s0 = pointer to M68K_CONTEXT
 *   s1 = pointer to 68k Fetch table (for PC->real address mapping)
 *   s2 = cycle counter (decremented per instruction)
 *   t0-t6 = temporaries for 68k operations
 *   AT = reserved by emitter
 */
#define REG_CTX     16  /* s0 - M68K_CONTEXT pointer */
#define REG_FETCH   17  /* s1 - Fetch table pointer */
#define REG_CYCLES  18  /* s2 - cycle counter */
#define REG_TMP0     8  /* t0 */
#define REG_TMP1     9  /* t1 */
#define REG_TMP2    10  /* t2 */
#define REG_TMP3    11  /* t3 */
#define REG_TMP4    12  /* t4 */

/* M68K_CONTEXT field offsets */
#define CTX_OFF_DREG(n)  (offsetof(M68K_CONTEXT, dreg) + (n) * 4)
#define CTX_OFF_AREG(n)  (offsetof(M68K_CONTEXT, areg) + (n) * 4)
#define CTX_OFF_PC       (offsetof(M68K_CONTEXT, pc))
#define CTX_OFF_SR       (offsetof(M68K_CONTEXT, sr))
#define CTX_OFF_CYCLES   (offsetof(M68K_CONTEXT, io_cycle_counter))
#define CTX_OFF_FLAG_C   (offsetof(M68K_CONTEXT, flag_C))
#define CTX_OFF_FLAG_V   (offsetof(M68K_CONTEXT, flag_V))
#define CTX_OFF_FLAG_NZ  (offsetof(M68K_CONTEXT, flag_NotZ))
#define CTX_OFF_FLAG_N   (offsetof(M68K_CONTEXT, flag_N))
#define CTX_OFF_FLAG_X   (offsetof(M68K_CONTEXT, flag_X))
#define CTX_OFF_FETCH    (offsetof(M68K_CONTEXT, Fetch))

/* ======== 68k Instruction Decoder ======== */

/* Read a 16-bit word from 68k address space (for block compilation) */
#define M68K_FETCH_SHIFT 16  /* 24 - FAMEC_FETCHBITS(8) */
#define M68K_FETCH_MASK  0xff

static u16 fetch_68k_word(u32 addr)
{
	uptr base = PicoCpuFM68k.Fetch[(addr >> M68K_FETCH_SHIFT) & M68K_FETCH_MASK];
	if (base == (uptr)-1)
		return 0x4e71; /* NOP if unmapped */
	return *(u16 *)(base + addr);
}

/* 68k addressing mode types */
#define EA_DREG     0   /* Dn */
#define EA_AREG     1   /* An */
#define EA_AREG_IND 2   /* (An) */
#define EA_AREG_INC 3   /* (An)+ */
#define EA_AREG_DEC 4   /* -(An) */
#define EA_AREG_DSP 5   /* d16(An) */
#define EA_ABS_W    7   /* xxxx.w (mode=7, reg=0) */
#define EA_ABS_L    8   /* xxxx.l (mode=7, reg=1) */
#define EA_IMM      9   /* #imm (mode=7, reg=4) */
#define EA_UNSUP   -1   /* unsupported */

/* Extract EA mode and register from 68k opcode */
#define OP_EA_MODE(op) (((op) >> 3) & 7)
#define OP_EA_REG(op)  ((op) & 7)
#define OP_SIZE(op)    (((op) >> 6) & 3) /* 0=byte, 1=word, 2=long */

/* ======== Code Emitter Helpers ======== */

/* Emit: load 68k Dn into MIPS reg */
static void emit_load_dreg(int mips_reg, int dreg_num)
{
	/* LW mips_reg, CTX_OFF_DREG(dreg_num)(REG_CTX) */
	EMIT(MIPS_LW(mips_reg, CTX_OFF_DREG(dreg_num), REG_CTX));
}

/* Emit: store MIPS reg to 68k Dn */
static void emit_store_dreg(int dreg_num, int mips_reg)
{
	EMIT(MIPS_SW(mips_reg, CTX_OFF_DREG(dreg_num), REG_CTX));
}

/* Emit: load 68k An into MIPS reg */
static void emit_load_areg(int mips_reg, int areg_num)
{
	EMIT(MIPS_LW(mips_reg, CTX_OFF_AREG(areg_num), REG_CTX));
}

/* Emit: store MIPS reg to 68k An */
static void emit_store_areg(int areg_num, int mips_reg)
{
	EMIT(MIPS_SW(mips_reg, CTX_OFF_AREG(areg_num), REG_CTX));
}

/* Emit: update N and Z flags from result in mips_reg (long) */
static void emit_update_nz_long(int mips_reg)
{
	/* flag_NotZ = result (non-zero if result != 0) */
	EMIT(MIPS_SW(mips_reg, CTX_OFF_FLAG_NZ, REG_CTX));
	/* flag_N = result (bit 31 is sign) */
	EMIT(MIPS_SW(mips_reg, CTX_OFF_FLAG_N, REG_CTX));
}

/* Emit: clear V and C flags */
static void emit_clear_vc(void)
{
	EMIT(MIPS_SW(Z0, CTX_OFF_FLAG_V, REG_CTX));
	EMIT(MIPS_SW(Z0, CTX_OFF_FLAG_C, REG_CTX));
}

/* Emit: subtract cycles and check for exit */
static void emit_cycle_check(int cycles, u32 *exit_label)
{
	EMIT(MIPS_ADDIU(REG_CYCLES, REG_CYCLES, -cycles));
	/* BLEZ REG_CYCLES, exit_label (filled in later) */
	*exit_label = (u32)(tcache_ptr);
	EMIT(MIPS_BLEZ(REG_CYCLES, 0)); /* placeholder */
	EMIT(MIPS_NOP); /* branch delay slot */
}

/* ======== Block Compiler ======== */

/* Compile one 68k instruction. Returns:
 *  > 0: number of 68k bytes consumed
 *  -1:  unsupported opcode, end block
 */
static int compile_one_insn(u32 pc, int *cycles_out)
{
	u16 opcode = fetch_68k_word(pc);
	int size, src_reg, dst_reg;

	/* Decode the major opcode groups */
	switch ((opcode >> 12) & 0xf) {

	case 0x2: /* MOVE.L */
	case 0x3: /* MOVE.W */
	{
		int op_size = ((opcode >> 12) & 0xf) == 0x2 ? 2 : 1; /* 2=long, 1=word */
		int src_mode = OP_EA_MODE(opcode);
		int src_r = OP_EA_REG(opcode);
		int dst_mode = (opcode >> 6) & 7;
		int dst_r = (opcode >> 9) & 7;

		/* Phase 1: only Dn->Dn moves */
		if (src_mode == 0 && dst_mode == 0) {
			emit_load_dreg(REG_TMP0, src_r);
			if (op_size == 2) {
				/* MOVE.L */
				emit_store_dreg(dst_r, REG_TMP0);
			} else {
				/* MOVE.W - preserve upper word of destination */
				emit_load_dreg(REG_TMP1, dst_r);
				EMIT(MIPS_ANDI( REG_TMP0, REG_TMP0, 0xffff));
				EMIT(MIPS_LUI(REG_TMP2, 0xffff));
				EMIT(MIPS_AND(REG_TMP1, REG_TMP1, REG_TMP2));
				EMIT(MIPS_OR(REG_TMP0, REG_TMP0, REG_TMP1));
				emit_store_dreg(dst_r, REG_TMP0);
			}
			emit_update_nz_long(REG_TMP0);
			emit_clear_vc();
			*cycles_out = 4;
			return 2;
		}

		/* Dn->An or An->Dn not yet supported, etc. */
		return -1;
	}

	case 0x1: /* MOVE.B */
	{
		int src_mode = OP_EA_MODE(opcode);
		int src_r = OP_EA_REG(opcode);
		int dst_mode = (opcode >> 6) & 7;
		int dst_r = (opcode >> 9) & 7;

		if (src_mode == 0 && dst_mode == 0) {
			/* MOVE.B Dn, Dn */
			emit_load_dreg(REG_TMP0, src_r);
			emit_load_dreg(REG_TMP1, dst_r);
			EMIT(MIPS_ANDI( REG_TMP0, REG_TMP0, 0xff));
			/* Mask out low byte of dest, OR in source */
			EMIT(MIPS_ANDI( REG_TMP2, REG_TMP1, 0xff00));
			/* Actually need upper 24 bits: use LUI + ORI trick */
			EMIT(MIPS_ADDIU(REG_TMP2, Z0, -256)); /* 0xFFFFFF00 */
			EMIT(MIPS_AND(REG_TMP1, REG_TMP1, REG_TMP2));
			EMIT(MIPS_OR(REG_TMP0, REG_TMP0, REG_TMP1));
			emit_store_dreg(dst_r, REG_TMP0);
			emit_update_nz_long(REG_TMP0);
			emit_clear_vc();
			*cycles_out = 4;
			return 2;
		}
		return -1;
	}

	case 0xd: /* ADD */
	{
		int reg = (opcode >> 9) & 7;
		int op_mode = (opcode >> 6) & 7;
		int ea_mode = OP_EA_MODE(opcode);
		int ea_reg = OP_EA_REG(opcode);

		/* ADD.L Dn, Dn (op_mode=2, ea_mode=0) */
		if (op_mode == 2 && ea_mode == 0) {
			emit_load_dreg(REG_TMP0, ea_reg);  /* source */
			emit_load_dreg(REG_TMP1, reg);     /* dest */
			EMIT(MIPS_ADDU(REG_TMP2, REG_TMP0, REG_TMP1));
			emit_store_dreg(reg, REG_TMP2);
			emit_update_nz_long(REG_TMP2);
			/* C = carry, X = carry (simplified: use SLTU) */
			EMIT(MIPS_SLTU(REG_TMP3, REG_TMP2, REG_TMP0));
			EMIT(MIPS_SW(REG_TMP3, CTX_OFF_FLAG_C, REG_CTX));
			EMIT(MIPS_SW(REG_TMP3, CTX_OFF_FLAG_X, REG_CTX));
			/* V = overflow (simplified) */
			EMIT(MIPS_SW(Z0, CTX_OFF_FLAG_V, REG_CTX));
			*cycles_out = 8;
			return 2;
		}
		return -1;
	}

	case 0x9: /* SUB */
	{
		int reg = (opcode >> 9) & 7;
		int op_mode = (opcode >> 6) & 7;
		int ea_mode = OP_EA_MODE(opcode);
		int ea_reg = OP_EA_REG(opcode);

		/* SUB.L Dn, Dn */
		if (op_mode == 2 && ea_mode == 0) {
			emit_load_dreg(REG_TMP0, ea_reg);
			emit_load_dreg(REG_TMP1, reg);
			EMIT(MIPS_SUBU(REG_TMP2, REG_TMP1, REG_TMP0));
			emit_store_dreg(reg, REG_TMP2);
			emit_update_nz_long(REG_TMP2);
			/* C = borrow */
			EMIT(MIPS_SLTU(REG_TMP3, REG_TMP1, REG_TMP0));
			EMIT(MIPS_SW(REG_TMP3, CTX_OFF_FLAG_C, REG_CTX));
			EMIT(MIPS_SW(REG_TMP3, CTX_OFF_FLAG_X, REG_CTX));
			EMIT(MIPS_SW(Z0, CTX_OFF_FLAG_V, REG_CTX));
			*cycles_out = 8;
			return 2;
		}
		return -1;
	}

	case 0xb: /* CMP / EOR */
	{
		int reg = (opcode >> 9) & 7;
		int op_mode = (opcode >> 6) & 7;
		int ea_mode = OP_EA_MODE(opcode);
		int ea_reg = OP_EA_REG(opcode);

		/* CMP.L Dn, Dn (op_mode=2, ea_mode=0) */
		if (op_mode == 2 && ea_mode == 0) {
			emit_load_dreg(REG_TMP0, ea_reg);
			emit_load_dreg(REG_TMP1, reg);
			EMIT(MIPS_SUBU(REG_TMP2, REG_TMP1, REG_TMP0));
			/* Don't store result - just set flags */
			emit_update_nz_long(REG_TMP2);
			EMIT(MIPS_SLTU(REG_TMP3, REG_TMP1, REG_TMP0));
			EMIT(MIPS_SW(REG_TMP3, CTX_OFF_FLAG_C, REG_CTX));
			EMIT(MIPS_SW(Z0, CTX_OFF_FLAG_V, REG_CTX));
			*cycles_out = 6;
			return 2;
		}
		return -1;
	}

	case 0x7: /* MOVEQ */
	{
		int reg = (opcode >> 9) & 7;
		s8 imm = (s8)(opcode & 0xff);
		/* MOVEQ #imm8, Dn - sign extended to 32 bits */
		EMIT(MIPS_ADDIU(REG_TMP0, Z0, (s16)imm));
		emit_store_dreg(reg, REG_TMP0);
		emit_update_nz_long(REG_TMP0);
		emit_clear_vc();
		*cycles_out = 4;
		return 2;
	}

	case 0x4: /* Miscellaneous */
	{
		if ((opcode & 0xff00) == 0x4200) {
			/* CLR */
			int size = OP_SIZE(opcode);
			int ea_mode = OP_EA_MODE(opcode);
			int ea_reg = OP_EA_REG(opcode);

			if (ea_mode == 0 && size == 2) {
				/* CLR.L Dn */
				emit_store_dreg(ea_reg, Z0);
				emit_update_nz_long(Z0);
				emit_clear_vc();
				*cycles_out = 6;
				return 2;
			}
		}

		if ((opcode & 0xfff0) == 0x4e70) {
			if (opcode == 0x4e71) {
				/* NOP */
				*cycles_out = 4;
				return 2;
			}
			if (opcode == 0x4e75) {
				/* RTS - end block */
				return -1;
			}
		}

		if ((opcode & 0xffc0) == 0x4a00) {
			/* TST */
			int size = OP_SIZE(opcode);
			int ea_mode = OP_EA_MODE(opcode);
			int ea_reg = OP_EA_REG(opcode);

			if (ea_mode == 0 && size == 2) {
				/* TST.L Dn */
				emit_load_dreg(REG_TMP0, ea_reg);
				emit_update_nz_long(REG_TMP0);
				emit_clear_vc();
				*cycles_out = 4;
				return 2;
			}
		}

		return -1;
	}

	case 0xc: /* AND / MUL */
	{
		int reg = (opcode >> 9) & 7;
		int op_mode = (opcode >> 6) & 7;
		int ea_mode = OP_EA_MODE(opcode);
		int ea_reg = OP_EA_REG(opcode);

		/* AND.L Dn, Dn */
		if (op_mode == 2 && ea_mode == 0) {
			emit_load_dreg(REG_TMP0, ea_reg);
			emit_load_dreg(REG_TMP1, reg);
			EMIT(MIPS_AND(REG_TMP2, REG_TMP0, REG_TMP1));
			emit_store_dreg(reg, REG_TMP2);
			emit_update_nz_long(REG_TMP2);
			emit_clear_vc();
			*cycles_out = 8;
			return 2;
		}
		return -1;
	}

	case 0x8: /* OR / DIV */
	{
		int reg = (opcode >> 9) & 7;
		int op_mode = (opcode >> 6) & 7;
		int ea_mode = OP_EA_MODE(opcode);
		int ea_reg = OP_EA_REG(opcode);

		/* OR.L Dn, Dn */
		if (op_mode == 2 && ea_mode == 0) {
			emit_load_dreg(REG_TMP0, ea_reg);
			emit_load_dreg(REG_TMP1, reg);
			EMIT(MIPS_OR(REG_TMP2, REG_TMP0, REG_TMP1));
			emit_store_dreg(reg, REG_TMP2);
			emit_update_nz_long(REG_TMP2);
			emit_clear_vc();
			*cycles_out = 8;
			return 2;
		}
		return -1;
	}

	case 0x6: /* Bcc / BRA / BSR */
	{
		/* Branch instructions end the block */
		return -1;
	}

	default:
		return -1;
	}
}

/* Compile a basic block starting at addr_68k.
 * Returns the block, or NULL if compilation fails. */
static drc68k_block_t *compile_block(u32 addr_68k)
{
	drc68k_block_t *block;
	u8 *code_start;
	u32 pc;
	int total_cycles = 0;
	int insn_count = 0;

	if (drc68k.block_count >= DRC68K_BLOCK_MAX)
		return NULL;
	if ((u8 *)tcache_ptr + 1024 > drc68k.cache_end)
		return NULL;

	block = &drc68k.blocks[drc68k.block_count];
	block->addr_68k = addr_68k;
	block->code = tcache_ptr;
	code_start = (u8 *)tcache_ptr;

	/* Prologue: save s-regs, load context pointer */
	/* Push s0-s2 onto stack */
	EMIT(MIPS_ADDIU(SP, SP, -16));
	EMIT(MIPS_SW(16, 0, SP));   /* save s0 */
	EMIT(MIPS_SW(17, 4, SP));   /* save s1 */
	EMIT(MIPS_SW(18, 8, SP));   /* save s2 */
	EMIT(MIPS_SW(LR, 12, SP));  /* save ra */

	/* a0 = M68K_CONTEXT*, a1 = cycles */
	EMIT(MIPS_ADDU(REG_CTX, 4, Z0));    /* s0 = a0 (ctx) */
	EMIT(MIPS_ADDU(REG_CYCLES, 5, Z0));  /* s2 = a1 (cycles) */

	/* Compile instructions */
	pc = addr_68k;
	while (insn_count < DRC68K_MAX_BLOCK_INSNS) {
		int insn_cycles = 0;
		int consumed = compile_one_insn(pc, &insn_cycles);

		if (consumed < 0)
			break; /* unsupported opcode or block-ending instruction */

		pc += consumed;
		total_cycles += insn_cycles;
		insn_count++;
	}

	if (insn_count == 0) {
		/* Couldn't compile anything, revert */
		tcache_ptr = (u32 *)code_start;
		return NULL;
	}

	/* Epilogue: write back updated PC and remaining cycles, restore, return */
	/* Store new PC into context */
	EMIT(MIPS_LUI(REG_TMP0, (pc >> 16) & 0xffff));
	EMIT(MIPS_ORI( REG_TMP0, REG_TMP0, pc & 0xffff));
	EMIT(MIPS_SW(REG_TMP0, CTX_OFF_PC, REG_CTX));

	/* Return value = total cycles consumed */
	EMIT(MIPS_ADDIU(2, Z0, total_cycles));  /* v0 = total_cycles */

	/* Restore saved regs */
	EMIT(MIPS_LW(16, 0, SP));
	EMIT(MIPS_LW(17, 4, SP));
	EMIT(MIPS_LW(18, 8, SP));
	EMIT(MIPS_LW(LR, 12, SP));
	EMIT(MIPS_ADDIU(SP, SP, 16));

	/* Return */
	EMIT(MIPS_JR(LR));
	EMIT(MIPS_NOP); /* branch delay slot */

	/* Flush I-cache for the generated code */
	cache_flush_d_inval_i(code_start, (u8 *)tcache_ptr);

	block->insn_count = insn_count;
	block->code_size = (u8 *)tcache_ptr - code_start;
	block->cycles = total_cycles;

	/* Add to hash table */
	int h = drc68k_hash(addr_68k);
	drc68k.hash[h] = drc68k.block_count;

	drc68k.block_count++;
	drc68k.blocks_compiled++;

	return block;
}

/* ======== Public API ======== */

void drc68k_init(void)
{
	memset(&drc68k, 0, sizeof(drc68k));
	memset(drc68k.hash, -1, sizeof(drc68k.hash));

	drc68k.cache = (u8 *)plat_mem_get_for_drc(DRC68K_CACHE_SIZE);
	if (!drc68k.cache) {
		drc68k.cache = (u8 *)malloc(DRC68K_CACHE_SIZE);
	}
	if (!drc68k.cache) {
		elprintf(EL_STATUS, "drc68k: FATAL: cannot allocate code cache");
		return;
	}

	drc68k.cache_ptr = drc68k.cache;
	drc68k.cache_end = drc68k.cache + DRC68K_CACHE_SIZE;
	tcache_ptr = (u32 *)drc68k.cache;

	plat_mem_set_exec(drc68k.cache, DRC68K_CACHE_SIZE);

	elprintf(EL_STATUS, "drc68k: initialized, %d KB code cache at %p",
		DRC68K_CACHE_SIZE / 1024, drc68k.cache);
}

void drc68k_reset(void)
{
	drc68k.block_count = 0;
	drc68k.cache_ptr = drc68k.cache;
	tcache_ptr = (u32 *)drc68k.cache;
	memset(drc68k.hash, -1, sizeof(drc68k.hash));
}

void drc68k_cleanup(void)
{
	if (drc68k.cache) {
		free(drc68k.cache);
		drc68k.cache = NULL;
	}
}

int drc68k_execute(M68K_CONTEXT *ctx, u32 addr, int cycles_max)
{
	int h = drc68k_hash(addr);
	drc68k_block_t *block = NULL;
	int cycles_used;

	/* Look up in hash table */
	s16 idx = drc68k.hash[h];
	if (idx >= 0 && drc68k.blocks[idx].addr_68k == addr) {
		block = &drc68k.blocks[idx];
	}

	/* Try to compile if not found */
	if (!block) {
		tcache_ptr = (u32 *)drc68k.cache_ptr;
		block = compile_block(addr);
		if (block)
			drc68k.cache_ptr = (u8 *)tcache_ptr;
	}

	if (!block) {
		drc68k.fallbacks++;
		return -1; /* fall back to interpreter */
	}

	/* Execute the compiled block */
	typedef int (*block_func)(M68K_CONTEXT *ctx, int cycles);
	block_func fn = (block_func)block->code;
	cycles_used = fn(ctx, cycles_max);

	drc68k.blocks_executed++;
	return cycles_used;
}
