# How to port the engine to other assemblers

In order to use the engine with other assemblers you need to:

1. Convert `beeper.z80` to your assembler's syntax.

You will need to remove the `.globl` directives and review the syntax making the necessary changes. The engine is small (less than 200 lines), so it shouldn't be too hard.

2. Include the effect data in your program.

This can be done by exporting the data as a binary file and including that in your source.

Most assemblers include a directive to do that.

For example, using rasm assembler:

```asm
; rasm syntax, include the binary data
_effects:
incbin "effects.bin"

```

If your assembler of choice doesn't support including binary files, write a small tool that converts from binary to asm source code.

3. Call the functions in your code.

Once the code compiles, you can call the functions like follows:

```asm
    ; using SDCC asm syntax

    ; init the engine (needed once)
    ; HL: address to the effects' data
    ld hl, #_effects
    call _beeper_init

    ; queue an effect to be played
    ; L: effect number to queue, 0 for silence
    ld l, #1
    call _beeper_queue
```

You also need to include a call to `_beeper_play` in your interrupt handler.

You can read a full example in `player/player.z80` (using SDCC syntax).

