all: player.bin

CC = sdcc
AS = sdasz80
AR = sdar
CFLAGS = -mz80 --Werror --fsigned-char --std-sdcc99 --opt-code-speed
LDFLAGS = --no-std-crt0 --fomit-frame-pointer

%.rel: %.z80
	$(AS) -g -o $@ $<

player.bin: player.rel
	$(CC) $(CFLAGS) $(LDFLAGS) --code-loc 20480 --data-loc 0 -o player.ihx $<
	hex2bin -p 00 player.ihx
	./bin2h.py player.bin player > ../player.h

.PHONY: clean
clean:
	rm -f *.rel *.ihx *.bin *.map *.noi *.lk

