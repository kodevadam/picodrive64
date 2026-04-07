# PicoDrive N64 platform Makefile
#
# This is intended to be included from the main Makefile when PLATFORM=n64.
# It can also be used standalone with libdragon's build system.
#
# Prerequisites:
#   - libdragon SDK installed (https://github.com/DragonMinded/libdragon)
#   - mips64-elf-gcc toolchain in PATH
#
# Usage:
#   From project root:
#     ./configure --platform=n64
#     make
#
#   Or standalone with libdragon:
#     cd platform/n64 && make -f Makefile.n64

# N64-specific build variables
N64_ROM_TITLE = "PicoDrive64"
N64_ROM_SAVETYPE = none

# Object files for N64 platform
N64_OBJS = platform/n64/plat.o \
           platform/n64/emu.o \
           platform/n64/in_n64.o
