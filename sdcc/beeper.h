// Beeper engine
// Copyright (C) 2021 by Juan J. Martinez <jjm@usebox.net>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
#ifndef _BEEPER_H
#define _BEEPER_H

#include <stdint.h>

struct beeper_sfx {
    uint8_t type;
    uint8_t frames;
    uint8_t freq;
    uint8_t slide;
    uint8_t next;
};

// to init the beeper engine, provide a pointer to the effect table
void beeper_init(const struct beeper_sfx *efx_table) __z88dk_fastcall;

// to queue a new effect
// efx_no is...
//
//    0: no effect (stops sound)
//    1: index 0 of the effect table
//    2: ...
//
void beeper_queue(uint8_t efx_no) __z88dk_fastcall;

// to be called in the INT handler; call beeper_init first!
void beeper_play();
#endif
