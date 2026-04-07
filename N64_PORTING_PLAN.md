# PicoDrive N64 Port

## Quick Start (SummerCart64)

### Build

```bash
# Prerequisites: libdragon SDK installed, N64_INST set
export N64_INST=/opt/libdragon
export PATH=$N64_INST/bin:$PATH

# Configure and build
./configure --platform=n64
make

# Build .z64 ROM image
make -C platform/n64 rom
```

### Setup on SD Card

```
sd:/
  PicoDrive64.z64          <- ROM file (or load via SC64 menu)
  picodrive/
    roms/                   <- Put Genesis/SMS/GG ROMs here
      sonic.bin
      streets_of_rage.md
    saves/                  <- Save states auto-created here
    config2.cfg             <- Config auto-created here
```

### Controls

| N64 Button | Genesis | Menu |
|------------|---------|------|
| D-Pad/Stick | D-Pad | Navigate |
| A | B | OK |
| B | C | Back |
| Z | A | Alt action |
| L | X | Page left |
| R | Z | Page right |
| C-Right | Y | - |
| Start | Start | - |
| C-Up | - | Open menu |
| C-Down | - | Save state |
| C-Left | - | Load state |

### Requirements

- **Expansion Pak strongly recommended** (8 MB): supports ROMs up to 4 MB
- Without Expansion Pak (4 MB): only ROMs up to ~1 MB

---

## Porting Plan

### Overview

Port PicoDrive (Sega Genesis/Mega Drive, Master System, Game Gear emulator) to run on Nintendo 64 hardware using the libdragon SDK. Targets SummerCart64 flashcart for ROM loading.

## N64 Hardware Summary

| Component | Specification |
|-----------|--------------|
| CPU | NEC VR4300 (MIPS III), 93.75 MHz |
| RAM | 4 MB RDRAM (8 MB with Expansion Pak) |
| GPU | Reality Co-Processor (RSP + RDP) |
| Audio | RSP-based, 16-bit stereo |
| Display | Up to 640x480, typically 320x240 |
| Storage | ROM cartridge (read-only), Controller Pak (32KB save) |
| Input | 4 controller ports, analog stick + d-pad + 10 buttons |

## Key Advantages (Why This Is Feasible)

1. **Existing MIPS support**: PicoDrive already has a MIPS dynamic recompiler backend (`cpu/drc/emit_mips.c`, 1,969 lines) and MIPS assembly routines (`pico/memory_amips.S`, `pico/misc_amips.s`)
2. **Existing embedded ports**: PSP (MIPS Allegrex), PS2 (MIPS R5900), OpenDingux (MIPS32) ports prove the codebase works on MIPS and resource-constrained devices
3. **Native resolution match**: Genesis outputs 320x224/240 — N64 natively supports 320x240
4. **Modular architecture**: Platform abstraction layer (`plat_mmap`, `emu_video_mode_change`, etc.) makes adding new platforms straightforward
5. **C fallback paths**: Every ARM assembly optimization has a C fallback, so nothing is blocked by missing ASM

## Key Challenges

1. **RAM constraint**: 4-8 MB total vs. Genesis ROM sizes up to 4 MB + emulator overhead
2. **No OS/libc**: Must use libdragon's newlib or bare-metal equivalents
3. **MIPS III vs MIPS32**: VR4300 is MIPS III (1994); existing DRC targets MIPS32 R1/R2 — some instructions differ
4. **No writable executable memory (easily)**: DRC needs careful handling for cache coherency on VR4300
5. **ROM loading**: Game ROMs must come from flashcart SD card or be bundled in the N64 ROM
6. **No floating point in hot paths**: VR4300 FPU exists but is slow; emulator core already avoids FP

---

## Phase 1: Build System & Toolchain Integration

### Goal: Get PicoDrive compiling for N64 with libdragon toolchain

### Tasks

1. **Add `n64` platform target to `configure` script**
   - File: `configure` (line 42, add to `platform_list`)
   - Add `set_platform` case for `n64` (~line 74-169)
   - Set `ARCH=mips64` (VR4300 is 64-bit MIPS III)
   - Set `MFLAGS="-march=vr4300 -mtune=vr4300 -mabi=32"`
   - Set `CFLAGS="-D__N64__"` 
   - Disable SDL sound drivers, set `sound_drivers="n64"`

2. **Create `platform/n64/` directory with skeleton files**
   ```
   platform/n64/
   ├── Makefile        # N64-specific build rules (libdragon integration)
   ├── plat.c          # Platform init, mmap, memory management
   ├── emu.c           # Emulation loop, video/audio output
   ├── in_n64.c        # N64 controller input mapping
   ├── menu.c          # ROM selection menu (optional, later phase)
   └── n64.h           # N64-specific constants and helpers
   ```

3. **Integrate with libdragon build system**
   - Use libdragon's `mips64-elf-gcc` cross-compiler
   - Link against libdragon (`-ldragon -lc -lm -ldragonsys`)
   - Output `.z64` ROM image via `n64tool`
   - Add `Makefile` rules to produce final `.z64`

4. **Disable unsupported subsystems at compile time**
   - Disable: Sega CD (too much RAM), 32X (too much CPU), SVP
   - Disable: CHD/libchdr, libavcodec, MP3 decoding
   - Disable: ARM-specific assembly (`use_cyclone=0`, `use_drz80=0`, `asm_*=0`)
   - Enable: CZ80 (C Z80 core), FAME or Musashi (C 68k core)
   - Enable: C rendering path (`draw.c`, `draw2.c` not `draw_arm.S`)

### Key Files to Modify
- `configure` — add n64 platform case
- `Makefile` — add n64 build target and object list
- `platform/common/common.mak` — ensure C fallbacks selected for n64

---

## Phase 2: Platform Abstraction Layer Implementation

### Goal: Implement all platform callbacks required by pico core

### 2a: Memory Management (`platform/n64/plat.c`)

```c
// Required by pico/pico.h:
void *plat_mmap(unsigned long addr, size_t size, int need_exec, int is_fixed);
void *plat_mremap(void *ptr, size_t oldsize, size_t newsize);
void  plat_munmap(void *ptr, size_t size);
void *plat_mem_get_for_drc(size_t size);
int   plat_mem_set_exec(void *ptr, size_t size);
void  cache_flush_d_inval_i(void *start, void *end);
```

**Implementation strategy:**
- Use a simple arena/pool allocator from RDRAM (no OS malloc initially)
- `plat_mmap`: Allocate from a pre-reserved memory pool
- `plat_mem_get_for_drc`: Reserve a fixed region (~256KB) for DRC code cache
- `cache_flush_d_inval_i`: Use VR4300 cache instructions (`CACHE 0x19` for d-cache writeback, `CACHE 0x10` for i-cache invalidate) — **critical for DRC**
- `plat_mem_set_exec`: No-op on N64 (no MMU page permissions in bare metal)

**Memory budget (with Expansion Pak = 8 MB):**
| Region | Size | Purpose |
|--------|------|---------|
| Emulator code + data | ~1 MB | PicoDrive binary |
| Genesis 68k RAM | 64 KB | Main work RAM |
| Genesis VRAM | 64 KB | Video RAM |
| Genesis Z80 RAM | 8 KB | Sound CPU RAM |
| ROM buffer | 2-4 MB | Game ROM (loaded from SD) |
| DRC code cache | 256 KB | Dynamic recompiler output |
| Frame buffers (2x) | 150 KB | 320x240x16bpp double buffer |
| Audio buffers | 16 KB | Sound output ring buffer |
| Stack + heap | ~512 KB | General purpose |
| **Total** | **~4.5-6.5 MB** | Fits in 8 MB with Expansion Pak |

**Without Expansion Pak (4 MB):** Only small ROMs (≤1 MB) feasible. Consider requiring Expansion Pak.

### 2b: Video Output (`platform/n64/emu.c`)

**Genesis video → N64 display pipeline:**

1. PicoDrive renders into a `g_screen_ptr` buffer (320x224/240, 16bpp RGB565)
2. N64 display uses libdragon's `display_get()` / `display_show()` API
3. Copy/blit the PicoDrive framebuffer into the N64 display buffer each frame

```c
// Key globals to set in plat_init():
g_screen_width  = 320;
g_screen_height = 240;
g_screen_ppitch = 320;
g_screen_ptr    = <allocated 16bpp buffer>;

// Per-frame in emu loop:
void emu_video_mode_change(int start_line, int line_count, int start_col, int col_count) {
    // Adjust visible area, handle 224 vs 240 line modes
    // Handle 256 vs 320 pixel width (H32/H40 modes)
}
```

**Pixel format consideration:**
- Genesis VDP outputs in various internal formats
- PicoDrive converts to BGR555/BGR565/RGB565 depending on platform
- N64 RDP expects RGBA5551 or RGBA8888
- Need a pixel format conversion step (RGB565 → RGBA5551) or configure PicoDrive to output RGBA5551 directly

**Optimization:** Use RDP hardware to blit the texture (DMA the framebuffer as a texture, draw a screen-filling rectangle). This offloads the copy from CPU.

### 2c: Audio Output (`platform/n64/emu.c`)

**Genesis audio → N64 audio pipeline:**

1. PicoDrive mixes audio into `PsndOut` buffer (16-bit stereo PCM)
2. Use libdragon's audio subsystem (`audio_init()`, `audio_write()`)
3. Target sample rate: 22050 Hz (good balance of quality vs. CPU cost)

```c
// Audio configuration:
defaultConfig.s_PsndRate = 22050;  // or 44100 if CPU allows
PicoIn.opt |= POPT_EN_FM | POPT_EN_PSG | POPT_EN_STEREO;

// Audio callback fills N64 audio buffer from PsndOut
```

**Buffer strategy:**
- Use double-buffering with ~4 audio frames of latency
- Buffer size: `22050 / 60 * 2 * 2 = ~1.5 KB per frame` (stereo 16-bit)
- Ring buffer of 4-7 chunks as done in PSP port

### 2d: Input (`platform/n64/in_n64.c`)

**Mapping N64 controller → Genesis 6-button pad:**

| N64 Button | Genesis Button |
|------------|---------------|
| D-Pad | D-Pad (Up/Down/Left/Right) |
| A | B |
| B | C |
| Z | A |
| L | X |
| R | Z |
| C-Right | Y |
| Start | Start |
| C-Up | Mode (optional) |
| Analog stick | D-Pad (with threshold) |

Implementation uses libdragon's `joypad_init()` / `joypad_get_buttons()`.

---

## Phase 3: MIPS DRC Adaptation for VR4300

### Goal: Get the SH2 dynamic recompiler working on VR4300 (optional for Genesis-only, required for 32X)

**Note:** For Genesis-only emulation, the DRC is not strictly needed (it's for SH2/32X). However, a 68k DRC would massively help performance. The existing `emit_mips.c` can be adapted.

### 3a: MIPS III vs MIPS32 Differences

The existing `cpu/drc/emit_mips.c` targets MIPS32 R1/R2. VR4300 is MIPS III. Key differences:

| Feature | MIPS III (VR4300) | MIPS32 R1 |
|---------|-------------------|-----------|
| `MUL` instruction | Not available | Available |
| `CLZ/CLO` | Not available | Available |
| `EXT/INS` | Not available | MIPS32 R2 only |
| `SEB/SEH` | Not available | MIPS32 R2 only |
| `MOVN/MOVZ` | Available (MIPS IV) | Available |
| 64-bit ops | Full 64-bit ALU | 32-bit only |
| Branch likely | Available | Deprecated |

### 3b: Required Changes to `emit_mips.c`

1. **Replace `MUL` with `MULT` + `MFLO`** sequence (VR4300 lacks fused multiply)
2. **Replace `CLZ`** with a software bit-scan loop or lookup table
3. **Replace `EXT/INS`** (bit field extract/insert) with shift+mask sequences
4. **Replace `SEB/SEH`** (sign extend byte/half) with `SLL`+`SRA` sequences
5. **Set `__mips_isa_rev 0`** to indicate pre-MIPS32 ISA
6. **Verify branch delay slot handling** — same on both, should be fine
7. **Cache flush after code generation** — implement `cache_flush_d_inval_i()` using VR4300 CACHE instructions

### 3c: File Changes

- `cpu/drc/emit_mips.c` — Add `#ifdef __N64__` guards for VR4300-compatible instruction sequences
- `pico/memory_amips.S` — Verify all instructions are MIPS III compatible
- `pico/misc_amips.s` — Same verification

---

## Phase 4: ROM Loading & Storage

### Goal: Load Genesis ROMs from flashcart SD card

### Implementation

Modern N64 flashcarts (EverDrive 64, 64drive) support SD card access. libdragon provides a filesystem API:

```c
// Initialize filesystem (SD card via flashcart)
dfs_init(DFS_DEFAULT_LOCATION);

// Or use libdragon's DragonFS / FAT SD support
// Load ROM into RAM buffer
FILE *f = fopen("sd:/roms/sonic.bin", "rb");
fread(rom_buffer, 1, rom_size, f);
fclose(f);
```

**ROM size management:**
- ROMs up to ~4 MB fit with Expansion Pak
- ROMs up to ~1.5 MB fit without Expansion Pak
- Could implement bank-switching for larger ROMs (read from SD on demand) but adds complexity and latency

**Save states:**
- Write to SD card via flashcart filesystem
- Genesis SRAM (8-32 KB) easily fits on Controller Pak or SD

---

## Phase 5: Performance Optimization

### Goal: Achieve 60 fps (NTSC) / 50 fps (PAL) on VR4300

### 5a: CPU Budget Analysis

VR4300 at 93.75 MHz must emulate:
- 68000 at 7.67 MHz (~1 host cycle per guest cycle with interpreter, much better with DRC)
- Z80 at 3.58 MHz
- VDP rendering (~15% of frame time)
- YM2612 FM synthesis (~20% of frame time)
- SN76496 PSG (~5% of frame time)

**Estimated feasibility:** The PSP (MIPS Allegrex, 333 MHz) runs PicoDrive at full speed with DRC. The N64 at 93.75 MHz is ~3.5x slower. Without DRC, this will be tight. With a 68k DRC (new work), it becomes much more feasible.

### 5b: Optimization Priorities (in order)

1. **Use interpreter-only first** — Get it working, profile, then optimize
2. **Adapt MIPS DRC for 68k** — The SH2 DRC framework in `cpu/sh2/compiler.c` with `emit_mips.c` could be adapted for 68k, or investigate using FAME with careful optimization
3. **Use alternative renderer** (`POPT_ALT_RENDERER`) — `draw2.c` is faster than `draw.c`
4. **Reduce audio sample rate** — 22050 Hz or even 11025 Hz saves significant CPU
5. **Disable non-essential features** — No FM filter, no SSG-EG, simplified YM2612
6. **Frame skip** — Allow 1-2 frame skip for demanding games
7. **RDP-assisted rendering** — Offload framebuffer blit to RDP hardware
8. **Write MIPS III assembly** for hot paths — Port key routines from `draw_arm.S`, `mix_arm.S`, `ym2612_arm.S` to MIPS III assembly
9. **Profile-guided optimization** — Use N64 cycle counter (`COUNT` register) to find bottlenecks

### 5c: Memory Optimization

- Use `draw2.c` alternative renderer (uses less RAM for sprite processing)
- Reduce audio buffer sizes to minimum stable values
- Consider 8-bit indexed color mode internally (saves framebuffer RAM)
- Strip unused code with `-fdata-sections -ffunction-sections -Wl,--gc-sections`

---

## Phase 6: Testing & Polish

### 6a: Testing Strategy

1. **Bring-up test:** Black screen → display solid color via N64 display API
2. **Core test:** Load a tiny homebrew Genesis ROM, verify CPU execution
3. **Video test:** Render first frame of Sonic 1 title screen
4. **Audio test:** Get PSG beeps working, then FM
5. **Input test:** Verify button mapping with a simple game
6. **Compatibility sweep:** Test top 20 Genesis games

### 6b: ROM Browser / Menu (Optional)

- Simple file browser using libdragon's console or custom UI
- List `.bin`/`.md`/`.gen`/`.sms` files from SD card
- Display ROM name, region, size
- Reference: `platform/psp/menu.c` for similar implementation

### 6c: N64-Specific Polish

- Support Expansion Pak detection and memory sizing
- Rumble Pak support for Genesis games with force feedback (optional)
- Controller Pak save support as alternative to SD saves
- Support for multiple controller ports (multiplayer Genesis games)

---

## Implementation Order (Recommended)

| Step | Phase | Description | Complexity |
|------|-------|-------------|------------|
| 1 | 1 | Build system + configure for N64 | Medium |
| 2 | 2a | Memory management (plat.c) | Medium |
| 3 | 2d | Input handling | Low |
| 4 | 2b | Video output (framebuffer → display) | Medium |
| 5 | 4 | ROM loading from SD | Low-Medium |
| 6 | — | **First boot: static ROM, no audio** | Milestone |
| 7 | 2c | Audio output | Medium |
| 8 | — | **Second milestone: playable with audio** | Milestone |
| 9 | 3 | MIPS DRC adaptation for VR4300 | High |
| 10 | 5 | Performance optimization | High |
| 11 | 6 | Testing, ROM browser, polish | Medium |

---

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| Insufficient CPU speed without DRC | High | Frame skip, reduced audio, alt renderer, eventual 68k DRC |
| RAM too tight for large ROMs | Medium | Require Expansion Pak, limit to small ROMs initially |
| VR4300 MIPS III incompatibilities with emit_mips.c | Medium | Well-understood differences, systematic replacement of instructions |
| libdragon audio latency | Low | Tunable buffer sizes, proven in other emulators |
| Flashcart compatibility | Low | libdragon abstracts this well, test on EverDrive 64 |

---

## Dependencies

- **libdragon SDK** (https://github.com/DragonMinded/libdragon) — open source N64 SDK
- **mips64-elf-gcc toolchain** — provided by libdragon's build system
- **N64 flashcart** (EverDrive 64 X7 or similar) — for testing on real hardware
- **Ares or cen64 emulator** — for development/testing without hardware
- **Expansion Pak** — strongly recommended for ROM sizes > 1.5 MB
