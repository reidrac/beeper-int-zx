# Beeper engine

This is simple beeper engine for the ZX Spectrum 48K (or later), designed to be
run from an interrupt handler.

This project was inspired by the works of Steve Turner (of Graftgold fame) and
Shiru. I made this for myself (to be used in my games), and I'm releasing it
in case it is useful to someone else.

The player supports three types of effects:

* Silence (stops the sound)
* Tone (square wave)
* Noise (random)

Tone and noise require:

* the number of frames (ints) to play the effect
* the frequency; the higher the value, the more time the effect will use in the int
* an optional slide to apply to the frequency
* the next sound to play in a chain, or zero to stop (silence)

It is possible to chain multiple sounds and have a loop. To break the loop,
just play "silence" (queue effect 0) or another effect out of the loop.

The beeper library is documented in `sdcc/beeper.h`, and it provides three
functions:

* `beeper_init` to set the effect data and initialize the engine
* `beeper_queue` to schedule an effect to be played starting on next interrupt
  (0 for silence, the effects start on 1)
* `beeper_play` to be called from the interrupt handler on each interrupt

The engine comes with a simple GUI editor (`sfxed`) to design the effects on a PC.

![sfxed 1.0.0](https://github.com/reidrac/beeper-int-zx/raw/main/sfxed-1.0.0.png)

There are some binaries on this website: [https://github.com/reidrac/beeper-int-zx/releases](https://github.com/reidrac/beeper-int-zx/releases)

`sfxed` uses the Zymosis Z80 CPU emulation engine to execute the player and
collect audio samples to be played using SDL. The sound emulation should be
accurate enough!

The GUI is built with ImGui.

The editor can export the effect data in binary form, so it can be included in
any assembler project, and as a C include file (for example to be used with
SDCC).

Check the `example` directory for an example.

## Building the player

Check the README file in `sdcc` directory.

The code is provided in assembler for SDCC, to be built as a library. It should
be easay to convert to other assemblers.

## Building the editor

Edit the `Makefile` and change the following variables to point to the corerct directory:

- `IMGUI_DIR`: a checkout of ImGui; see: https://github.com/ocornut/imgui
- `IMGUI_FILE_DIALOG_DIR`: a checkout of ImGuiFileDialog "Lib_Only"; see: https://github.com/aiekick/ImGuiFileDialog

To build on Linux, you'll need:
- GNU Make, GCC
- SDL2 for development (e.g. libsdl2-dev in Debian)

When all the requirements are satisfied, just run `make`.

## License

This software is distributed under MIT license. See COPYING file.

**TL;DR**: the only condition is that you are required to preserve the copyright
and license notices. Licensed works, modifications, and larger works may be
distributed under different terms and without source code; this includes any game
made with the help of this software.
