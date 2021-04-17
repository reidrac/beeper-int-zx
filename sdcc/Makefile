all: beeper.lib

AS=sdasz80
AR=sdar

beeper.lib: beeper.z80
	$(AS) -o $<
	$(AR) -rcD $@ beeper.rel

.PHONY: clean
clean:
	rm -f *.rel *.bin *.lib

