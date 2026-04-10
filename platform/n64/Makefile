# PicoDrive N64 - ROM build rules
#
# Usage (from project root, after ./configure --platform=n64 && make):
#   make -C platform/n64 rom
#
# For embedded ROM builds:
#   make N64_EMBEDDED_ROM=1
#   make -C platform/n64 rom

N64_INST ?= /opt/libdragon

ROM_NAME = PicoDrive64
ROM_TITLE = "PicoDrive64"

PICODRIVE_ELF ?= ../../picodrive

N64TOOL = $(N64_INST)/bin/n64tool
N64SYM = $(N64_INST)/bin/n64sym
N64ELFCOMPRESS = $(N64_INST)/bin/n64elfcompress
N64STRIP = $(N64_INST)/bin/mips64-elf-strip

.PHONY: rom clean

rom: $(ROM_NAME).z64

$(ROM_NAME).z64: $(PICODRIVE_ELF)
	@echo "=== Building N64 ROM: $@ ==="
	$(N64SYM) $(PICODRIVE_ELF) $(PICODRIVE_ELF).sym
	cp $(PICODRIVE_ELF) $(PICODRIVE_ELF).stripped
	$(N64STRIP) -s $(PICODRIVE_ELF).stripped
	$(N64ELFCOMPRESS) -o $(dir $(PICODRIVE_ELF)) -c 1 $(PICODRIVE_ELF).stripped
	$(N64TOOL) --title $(ROM_TITLE) --toc --output $@ \
		--align 256 $(PICODRIVE_ELF).stripped \
		--align 8 $(PICODRIVE_ELF).sym
	@rm -f $(PICODRIVE_ELF).stripped $(PICODRIVE_ELF).sym
	@echo ""
	@echo "ROM built: $@ ($$(stat -c%s $@ 2>/dev/null || echo '?') bytes)"
	@echo ""
	@echo "To use with SummerCart64:"
	@echo "  1. Copy $@ to your SD card"
	@echo "  2. Create sd:/picodrive/roms/ and put Genesis ROMs there"
	@echo ""
	@echo "To test in ares:  ares $@"

clean:
	rm -f $(ROM_NAME).z64 $(PICODRIVE_ELF).stripped $(PICODRIVE_ELF).sym
