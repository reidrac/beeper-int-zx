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
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "SDL.h"

#include "zymosis.h"

#define LOCAL
#include "player.h"
#undef LOCAL

#include "sfx.h"

#define PLAYER_ADDR 20480
#define EFX_TABLE_ADDR 32000
#define EFX_IN_ADDR (EFX_TABLE_ADDR - 1)

// 48K timings
#define TSTATES_PER_FRAME 69888
#define TSTATE_STEP 16

uint8_t memory[65536];
uint8_t sound_state;
uint8_t exit_state;

#define MAX_SAMPLES (44100*5)
uint16_t samples[MAX_SAMPLES];
uint32_t nsamples;

int load_sfx(char *filename, BeeperSfx *table, uint8_t n)
{
    FILE *fd;
    char header[8];
    uint8_t entries = 0;
    uint8_t type;

    fd = fopen(filename, "rt");
    if (!fd)
    {
        fprintf(stderr, "ERROR: failed to load %s\n", filename);
        return -1;
    }

    if (fgets(header, 7, fd) == NULL || strcmp(header, ";SFXv1"))
    {
        fprintf(stderr, "ERROR: %s doesn't look like a valid SFX file\n", filename);
        return -1;
    }

    memset(table, 0, sizeof(struct beeper_sfx) * n);

    while (!feof(fd))
    {
        if (fscanf(fd, "%8s %hhd %hhd %hhd %hhd %hhd\n",
                   table[entries].name,
                   &type,
                   &table[entries].frames,
                   &table[entries].freq,
                   &table[entries].slide,
                   &table[entries].next
                  ) != 6)
        {
            fprintf(stderr, "ERROR: failed to load %s\n", filename);
            fclose(fd);
            return -1;
        }
        // prevent invalid frequency
        table[entries].freq = table[entries].freq ? table[entries].freq : 1;
        // move to int
        table[entries].type = type < 3 ? type : 0;

        entries++;
        if (entries == n)
        {
            fprintf(stderr, "WARN: read max %d entries\n", entries);
            break;
        }
    }

    fclose(fd);

    return entries;
}

int save_sfx(char *filename, BeeperSfx *table, uint8_t n)
{
    FILE *fd;

    fd = fopen(filename, "wt");
    if (!fd)
    {
        fprintf(stderr, "ERROR: failed to save %s\n", filename);
        return -1;
    }

    if (fprintf(fd, ";SFXv1\n") != 7)
    {
        fprintf(stderr, "ERROR: failed to write to %s\n", filename);
        return -1;
    }

    for (int i = 0; i < n; i++)
    {
        if (fprintf(fd, "%s %hhd %hhd %hhd %hhd %hhd\n",
                    table[i].name,
                    table[i].type,
                    table[i].frames,
                    table[i].freq,
                    table[i].slide,
                    table[i].next
                   ) == -1)
        {
            fprintf(stderr, "ERROR: failed to write %s\n", filename);
            fclose(fd);
            return -1;
        }
    }

    fclose(fd);

    return n;
}

int export_c(char *filename, BeeperSfx *table, uint8_t n)
{
    FILE *fd;

    fd = fopen(filename, "wt");
    if (!fd)
    {
        fprintf(stderr, "ERROR: failed to save %s\n", filename);
        return -1;
    }

    fprintf(fd, "#ifndef _SFX_H\n#define _SFX_H\n\n");

    fprintf(fd, "enum sfx_enum {\n");
    for (int  i = 0; i < n; i++)
    {
        fprintf(fd, "\t// %s\n", table[i].name);
        if (i == 0)
            fprintf(fd, "\tSFX%d = 1,\n", i + 1);
        else
            fprintf(fd, "\tSFX%d,\n", i + 1);
    }
    fprintf(fd, "};\n\n");

    fprintf(fd, "const struct beeper_sfx sfx_table[] = {\n");
    for (int i = 0; i < n; i++)
    {
        if (fprintf(fd, "\t{ %hhu, %hhu, %hhu, %hhu, %hhu },\n",
                    table[i].type,
                    table[i].frames,
                    table[i].freq,
                    table[i].slide,
                    table[i].next
                   ) == -1)
        {
            fprintf(stderr, "ERROR: failed to write %s\n", filename);
            fclose(fd);
            return -1;
        }
    }
    fprintf(fd, "};\n\n");

    fprintf(fd, "#endif /* _SFX_H */\n");

    fclose(fd);

    return n;
}

int export_bin(char *filename, BeeperSfx *table, uint8_t n)
{
    FILE *fd;

    fd = fopen(filename, "wb");
    if (!fd)
    {
        fprintf(stderr, "ERROR: failed to save %s\n", filename);
        return -1;
    }

    for (int i = 0; i < n; i++)
    {
        if (fwrite(&table[i].type, 1, 1, fd) != 1
                || fwrite(&table[i].frames, 1, 1, fd) != 1
                || fwrite(&table[i].freq, 1, 1, fd) != 1
                || fwrite(&table[i].slide, 1, 1, fd) != 1
                || fwrite(&table[i].next, 1, 1, fd) != 1)
        {
            fclose(fd);
            fprintf(stderr, "ERROR: failed to write %s\n", filename);
            return -1;
        }
    }

    fclose(fd);

    return n;
}

int export_sfx(char *filename, BeeperSfx *table, uint8_t n)
{
    if (filename[strlen(filename) - 1] == 'h')
        return export_c(filename, table, n);
    else
        return export_bin(filename, table, n);
}

void z80_mem_write(Z80Info *z80, uint16_t addr, uint8_t value, Z80MemIOType mio)
{
    memory[addr] = value;
}

uint8_t z80_mem_read(Z80Info *z80, uint16_t addr, Z80MemIOType mio)
{
    return memory[addr];
}


uint8_t z80_port_in(Z80Info *z80, uint16_t port, Z80PIOType pio)
{
    return 0;
}

void z80_port_out(Z80Info *z80, uint16_t port, uint8_t value, Z80PIOType pio)
{
    switch (port & 0xff)
    {
        case 0xfe:
            sound_state = (value & 16);
            break;

        default:
            exit_state = 1;
            break;
    }
}

SDL_AudioDeviceID dev = 0;

uint8_t play_samples()
{
    SDL_AudioSpec want, have;

    SDL_memset(&want, 0, sizeof(want));
    want.freq = 44100;
    want.format = AUDIO_S16;
    want.channels = 1;
    want.samples = 4096;

    if (!dev) {
        dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (dev == 0) {
            fprintf(stderr, "ERROR: failed to open audio: %s", SDL_GetError());
            return 1;
        }
        SDL_PauseAudioDevice(dev, 0);
    }
    else
        SDL_ClearQueuedAudio(dev);

    if (SDL_QueueAudio(dev, samples, nsamples * 2) != 0)
        fprintf(stderr, "ERROR: playback error: %s\n", SDL_GetError());

    return 0;
}

int play_sfx(uint8_t index, BeeperSfx *table, uint8_t n)
{
    int i;
    uint32_t states, real, out;
    int32_t next_int;
    Z80Info z80;
    uint8_t *p;

    if (index > n)
        return 1;

    memset(&z80, 0, sizeof(Z80Info));
    memset(memory, 0, 65536);
    memcpy(memory + PLAYER_ADDR, player, PLAYER_LEN);

    p = &memory[EFX_TABLE_ADDR];
    for (int i = 0; i < n; i++)
    {
        *p++ = table[i].type & 0xff;
        *p++ = table[i].frames;
        *p++ = table[i].freq;
        *p++ = table[i].slide;
        *p++ = table[i].next;
    }
    memory[EFX_IN_ADDR] = index;

    Z80_ResetCallbacks(&z80);

    z80.memReadFn = z80_mem_read;
    z80.memWriteFn = z80_mem_write;
    z80.portInFn = z80_port_in;
    z80.portOutFn = z80_port_out;

    Z80_Reset(&z80);

    sound_state = 0;
    exit_state = 0;
    nsamples = 0;
    next_int = TSTATES_PER_FRAME;
    states = TSTATE_STEP;
    while (!exit_state)
    {
        for (i = 0, out = 0; i < 4; i++)
        {
            real = Z80_ExecuteTS(&z80, states);
            next_int -= real;

            states = TSTATE_STEP + (TSTATE_STEP - real);
            out += sound_state * 16384 / 16;

            if (next_int < TSTATE_STEP)
            {
                next_int += TSTATES_PER_FRAME;
                Z80_Interrupt(&z80);
            }
        }

        samples[nsamples++] = out >> 2;

        if (nsamples > MAX_SAMPLES)
            break;
    }

    return play_samples();
}
