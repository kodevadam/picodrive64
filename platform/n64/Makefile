# PicoDrive N64 platform - libdragon build integration
#
# This Makefile is used to produce the final .z64 ROM image.
# It expects the PicoDrive ELF binary to already be built by the
# main Makefile with --platform=n64.
#
# Prerequisites:
#   - libdragon SDK installed
#   - N64_INST environment variable pointing to libdragon install
#     (default: /opt/libdragon)
#
# Build steps:
#   1. ./configure --platform=n64
#   2. make
#   3. make -C platform/n64 rom
#
# The output is PicoDrive64.z64, ready to flash to SummerCart64 SD card.

N64_INST ?= /opt/libdragon

ROM_NAME = PicoDrive64
ROM_TITLE = "PicoDrive64"
ROM_SAVETYPE = none
ROM_REGIONFREE = true

# Path to PicoDrive ELF built by main Makefile
PICODRIVE_ELF ?= ../../picodrive

# libdragon tools
N64TOOL = $(N64_INST)/bin/n64tool
CHKSUM64 = $(N64_INST)/bin/chksum64
MKDFS = $(N64_INST)/bin/mkdfs

.PHONY: rom clean

rom: $(ROM_NAME).z64

# Create the .z64 ROM image from ELF
$(ROM_NAME).z64: $(PICODRIVE_ELF)
	@echo "Building N64 ROM: $@"
	$(N64_INST)/bin/mips64-elf-objcopy -O binary $(PICODRIVE_ELF) $(ROM_NAME).bin
	@# Pad to 1MB minimum for N64 boot
	@truncate -s '>1M' $(ROM_NAME).bin 2>/dev/null || true
	$(N64TOOL) \
		--title $(ROM_TITLE) \
		--output $@ \
		$(ROM_NAME).bin
	$(CHKSUM64) $@
	@echo "ROM built: $@ ($(shell stat -c%s $@ 2>/dev/null || echo '?') bytes)"
	@echo ""
	@echo "To use with SummerCart64:"
	@echo "  1. Copy $@ to your SD card"
	@echo "  2. Create sd:/picodrive/roms/ and put Genesis ROMs there"
	@echo "  3. Create sd:/picodrive/saves/ for save states"
	@echo ""

clean:
	rm -f $(ROM_NAME).z64 $(ROM_NAME).bin $(ROM_NAME).dfs
