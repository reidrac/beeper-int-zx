// sfxed for beeper engine
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
#ifndef _SFX_H
#define _SFX_H

#include <stdint.h>

struct beeper_sfx
{
    //uint8_t type;
    int type; // for the combo
    uint8_t frames;
    uint8_t freq;
    uint8_t slide;
    uint8_t next;

    char name[9];
};

typedef struct beeper_sfx BeeperSfx;

int load_sfx(char *filename, BeeperSfx *table, uint8_t n);
int save_sfx(char *filename, BeeperSfx *table, uint8_t n);
int export_sfx(char *filename, BeeperSfx *table, uint8_t n);
int play_sfx(uint8_t index, BeeperSfx *table, uint8_t n);

#endif //_SFX_H
