CC_LINUX=gcc
CC_WIN=i686-w64-mingw32-gcc
CC_DROID=arm-linux-gnueabihf-gcc

all: png2rle_linux png2rle_win png2rle_android

png2rle_linux: main.c png.c png.h
	@echo " CC\t" $@
	@$(CC_LINUX) -O3 -static -s *.c -o png2rle_linux

png2rle_android: main.c png.c png.h
	@echo " CC\t" $@
	@$(CC_DROID) -O3 -static -s *.c -o png2rle_android

png2rle_win: main.c png.c png.h
	@echo " CC\t" $@
	@$(CC_WIN) -O3 -static -s *.c -o png2rle_win

clean:
	rm -rf *.o png2rle_*




