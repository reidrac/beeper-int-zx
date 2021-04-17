This is the main implementation of the beeper engine, to be used with SDCC
compiler.

The assembler syntax specific to this compiler suite, but it should be
easy to convert to your favourite assembler.

Feel free to contribute your port!

## Build instructions

Ensure that SDCC is in your path and run `make`.

Include this directory in your include and library paths, and link with the
beeper library.

For example:
```
CFLAGS += -I$(BEEPER_LIB_DIR)
LDFLAGS += -L$(BEEPER_LIB_DIR) -lbeeper
```

It is recommended that the beeper code runs from non-contended memory.

