# ----------------------------
# Makefile Options
# ----------------------------

NAME = painter
DESCRIPTION = "Painter game"
COMPRESSED = NO
LDHAS_ARG_PROCESSING = 1

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# Heap size to 384k allowing a 64k program
INIT_LOC = 040000
BSSHEAP_LOW = 050000
BSSHEAP_HIGH = 0AFFFF

# ----------------------------

include $(shell cedev-config --makefile)

install: bin/$(NAME).bin
	srec_cat bin/$(NAME).bin -binary -offset 0x40000 -o bin/$(NAME).hex -intel
	cp bin/$(NAME).bin $(NAME)
	rsync -rvu ./ ~/agon/sdcard_sync/$(NAME)


run: install
	cd ~/agon/fab-agon-emulator ;\
	./fab-agon-emulator --firmware console8 --vdp src/vdp/vdp_console8.so 

package: install
	zip -r painter_v1.zip painter levels img
