/*
 * Z80 CPU emulation engine v0.0.3b
 * coded by Ketmar // Vampire Avalon
 *
 * This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details.
 */
#include <stdlib.h>

#include "zymosis.h"


/******************************************************************************/
/* some funny tables */
static int tablesInitialized = 0;
static uint8_t parityTable[256];
static uint8_t sz53Table[256];  /* bits 3, 5 and 7 of result, Z flag */
static uint8_t sz53pTable[256]; /* bits 3, 5 and 7 of result, Z and P flags */


/******************************************************************************/
void Z80_InitTables (void)
{
    if (!tablesInitialized)
    {
        int f;
        /***/
        for (f = 0; f <= 255; ++f)
        {
            int n, p;
            /***/
            sz53Table[f] = (f & Z80_FLAG_S35);
            for (n = f, p = 0; n != 0; n >>= 1) p ^= n & 0x01;
            parityTable[f] = (p ? 0 : Z80_FLAG_PV);
            sz53pTable[f] = (sz53Table[f] | parityTable[f]);
        }
        sz53Table[0] |= Z80_FLAG_Z;
        sz53pTable[0] |= Z80_FLAG_Z;
        /***/
        tablesInitialized = 1;
    }
}


void Z80_ResetCallbacks (Z80Info *z80)
{
    if (!tablesInitialized) Z80_InitTables();
    z80->memReadFn = NULL;
    z80->memWriteFn = NULL;
    z80->contentionFn = NULL;
    z80->portInFn = NULL;
    z80->portOutFn = NULL;
    z80->portContentionFn = NULL;
    z80->retiFn = NULL;
    z80->retnFn = NULL;
    z80->trapEDFn = NULL;
    z80->pagerFn = NULL;
    z80->checkBPFn = NULL;
}


/* seems that all regs (and memptr) should be set to 'all 1' here, but i don't care */
void Z80_Reset (Z80Info *z80)
{
    if (!tablesInitialized) Z80_InitTables();
    z80->bc.w = z80->de.w = z80->hl.w = z80->af.w = z80->sp.w = z80->ix.w = z80->iy.w = 0;
    z80->bcx.w = z80->dex.w = z80->hlx.w = z80->afx.w = 0;
    z80->pc = z80->prev_pc = z80->org_pc = 0;
    z80->memptr.w = 0;
    z80->regI = z80->regR = 0;
    z80->iff1 = z80->iff2 = 0;
    z80->im = 0;
    z80->halted = 0;
    z80->prev_was_EIDDR = 0;
    z80->tstates = 0;
    z80->dd = &z80->hl;
}


/******************************************************************************/
#define Z80_EXX(_z80)  do { \
  uint16_t t = (_z80)->bc.w; (_z80)->bc.w = (_z80)->bcx.w; (_z80)->bcx.w = t; \
  t = (_z80)->de.w; (_z80)->de.w = (_z80)->dex.w; (_z80)->dex.w = t; \
  t = (_z80)->hl.w; (_z80)->hl.w = (_z80)->hlx.w; (_z80)->hlx.w = t; \
} while (0)

#define Z80_EXAFAF(_z80)  do { \
  uint16_t t = (_z80)->af.w; (_z80)->af.w = (_z80)->afx.w; (_z80)->afx.w = t; \
} while (0)


/******************************************************************************/
/* simulate contented memory access */
/* (tstates = tstates+contention+1)*cnt */
/* (Z80Info *z80, uint16_t addr, int tstates, Z80MemIOType mio) */
#define Z80_Contention(_z80,_addr,_tstates,_mio)  do { \
  if ((_z80)->contentionFn != NULL) (_z80)->contentionFn((_z80), (_addr), (_tstates), (_mio)); else (_z80)->tstates += (_tstates); \
} while (0)


#define Z80_ContentionBy1(_z80,_addr,_cnt)  do { \
  if ((z80)->contentionFn != NULL) { \
    int _f; \
    for (_f = (_cnt); _f-- > 0; (_z80)->contentionFn((_z80), (_addr), 1, Z80_MREQ_NONE|Z80_MEMIO_OTHER)) ; \
  } else { \
    (_z80)->tstates += (_cnt); \
  } \
} while (0)


#define Z80_ContentionIRBy1(_z80,_cnt)  Z80_ContentionBy1((_z80), (((uint16_t)(_z80)->regI)<<8)|((_z80)->regR), (_cnt))
#define Z80_ContentionPCBy1(_z80,_cnt)  Z80_ContentionBy1((_z80), (_z80)->pc, (_cnt))


/******************************************************************************/
static ZYMOSIS_INLINE uint8_t Z80_PortIn (Z80Info *z80, uint16_t port)
{
    uint8_t value;
    /***/
    if (z80->portContentionFn != NULL)
    {
        z80->portContentionFn(z80, port, 1, Z80_PIOFLAG_IN | Z80_PIOFLAG_EARLY);
        z80->portContentionFn(z80, port, 2, Z80_PIOFLAG_IN);
    }
    else
    {
        z80->tstates += 3;
    }
    value = z80->portInFn(z80, port, Z80_PIO_NORMAL);
    ++z80->tstates;
    return value;
}


static ZYMOSIS_INLINE void Z80_PortOut (Z80Info *z80, uint16_t port, uint8_t value)
{
    if (z80->portContentionFn != NULL)
    {
        z80->portContentionFn(z80, port, 1, Z80_PIOFLAG_EARLY);
    }
    else
    {
        ++z80->tstates;
    }
    z80->portOutFn(z80, port, value, Z80_PIO_NORMAL);
    if (z80->portContentionFn != NULL)
    {
        z80->portContentionFn(z80, port, 2, 0);
        ++z80->tstates;
    }
    else
    {
        z80->tstates += 3;
    }
}


/******************************************************************************/
#define Z80_PeekBI(_z80,_addr)  (_z80)->memReadFn((_z80), (_addr), Z80_MEMIO_OTHER)
#define Z80_PeekB(_z80,_addr)   (_z80)->memReadFn((_z80), (_addr), Z80_MEMIO_DATA)
/*#define Z80_PeekWI(_z80,_addr)  (((uint16_t)Z80_PeekBI((_z80), (_addr)))|(((uint16_t)Z80_PeekBI((_z80), ((_addr)+1)&0xffff))<<8)) */

#define Z80_PokeBI(_z80,_addr,_byte)  (_z80)->memWriteFn((_z80), (_addr), (_byte), Z80_MEMIO_OTHER)
#define Z80_PokeB(_z80,_addr,_byte)   (_z80)->memWriteFn((_z80), (_addr), (_byte), Z80_MEMIO_DATA)

/*  t1: setting /MREQ & /RD */
/*  t2: memory read */
/*#define Z80_PeekB3T(_z80,_addr)  (Z80_Contention(_z80, (_addr), 3, Z80_MREQ_READ|Z80_MEMIO_DATA), Z80_PeekB(_z80, (_addr))) */
static ZYMOSIS_INLINE uint8_t Z80_PeekB3T (Z80Info *z80, uint16_t addr)
{
    Z80_Contention(z80, addr, 3, Z80_MREQ_READ | Z80_MEMIO_DATA);
    return Z80_PeekB(z80, addr);
}

static ZYMOSIS_INLINE uint8_t Z80_PeekB3TA (Z80Info *z80, uint16_t addr)
{
    Z80_Contention(z80, addr, 3, Z80_MREQ_READ | Z80_MEMIO_OPCARG);
    return Z80_PeekB(z80, addr);
}

/* t1: setting /MREQ & /WR */
/* t2: memory write */
#define Z80_PokeB3T(_z80,_addr,_byte)  do { \
  Z80_Contention((_z80), (_addr), 3, Z80_MREQ_WRITE|Z80_MEMIO_DATA); \
  Z80_PokeB((_z80), (_addr), (_byte)); \
} while (0)


static ZYMOSIS_INLINE uint16_t Z80_PeekW6T (Z80Info *z80, uint16_t addr)
{
    uint16_t res = Z80_PeekB3T(z80, addr);
    return res | (((uint16_t)Z80_PeekB3T(z80, (addr + 1) & 0xffff)) << 8);
}

static ZYMOSIS_INLINE void Z80_PokeW6T (Z80Info *z80, uint16_t addr, uint16_t value)
{
    Z80_PokeB3T(z80, addr, value & 0xff);
    Z80_PokeB3T(z80, (addr + 1) & 0xffff, (value >> 8) & 0xff);
}

static ZYMOSIS_INLINE void Z80_PokeW6TInv (Z80Info *z80, uint16_t addr, uint16_t value)
{
    Z80_PokeB3T(z80, (addr + 1) & 0xffff, (value >> 8) & 0xff);
    Z80_PokeB3T(z80, addr, value & 0xff);
}

static ZYMOSIS_INLINE uint16_t Z80_GetWordPC (Z80Info *z80, int wait1)
{
    uint16_t res = Z80_PeekB3TA(z80, z80->pc);
    /***/
    z80->pc = (z80->pc + 1) & 0xffff;
    res |= ((uint16_t)Z80_PeekB3TA(z80, z80->pc)) << 8;
    if (wait1) Z80_ContentionPCBy1(z80, wait1);
    z80->pc = (z80->pc + 1) & 0xffff;
    return res;
}

static ZYMOSIS_INLINE uint16_t Z80_Pop6T (Z80Info *z80)
{
    uint16_t res = Z80_PeekB3T(z80, z80->sp.w);
    /***/
    z80->sp.w = (z80->sp.w + 1) & 0xffff;
    res |= ((uint16_t)Z80_PeekB3T(z80, z80->sp.w)) << 8;
    z80->sp.w = (z80->sp.w + 1) & 0xffff;
    return res;
}

/* 3 T states write high byte of PC to the stack and decrement SP */
/* 3 T states write the low byte of PC and jump to #0066 */
static ZYMOSIS_INLINE void Z80_Push6T (Z80Info *z80, uint16_t value)
{
    z80->sp.w = (((int32_t)z80->sp.w) - 1) & 0xffff;
    Z80_PokeB3T(z80, z80->sp.w, (value >> 8) & 0xff);
    z80->sp.w = (((int32_t)z80->sp.w) - 1) & 0xffff;
    Z80_PokeB3T(z80, z80->sp.w, value & 0xff);
}


/******************************************************************************/
static ZYMOSIS_INLINE void Z80_ADC_A (Z80Info *z80, uint8_t b)
{
    uint16_t new, o = z80->af.a;
    /***/
    z80->af.a = (new = o + b + (z80->af.f & Z80_FLAG_C)) & 0xff; /* Z80_FLAG_C is 0x01, so it's safe */
    z80->af.f =
        sz53Table[new & 0xff] |
        (new > 0xff ? Z80_FLAG_C : 0) |
        ((o ^ (~b)) & (o ^ new) & 0x80 ? Z80_FLAG_PV : 0) |
        ((o & 0x0f) + (b & 0x0f) + (z80->af.f & Z80_FLAG_C) >= 0x10 ? Z80_FLAG_H : 0);
}

static ZYMOSIS_INLINE void Z80_SBC_A (Z80Info *z80, uint8_t b)
{
    uint16_t new, o = z80->af.a;
    /***/
    z80->af.a = (new = ((int32_t)o - (int32_t)b - (int32_t)(z80->af.f & Z80_FLAG_C)) & 0xffff) & 0xff; /* Z80_FLAG_C is 0x01, so it's safe */
    z80->af.f =
        Z80_FLAG_N |
        sz53Table[new & 0xff] |
        (new > 0xff ? Z80_FLAG_C : 0) |
        ((o ^ b) & (o ^ new) & 0x80 ? Z80_FLAG_PV : 0) |
        ((int32_t)(o & 0x0f) - (int32_t)(b & 0x0f) - (int32_t)(z80->af.f & Z80_FLAG_C) < 0 ? Z80_FLAG_H : 0);
}


static ZYMOSIS_INLINE void Z80_ADD_A (Z80Info *z80, uint8_t b)
{
    z80->af.f &= ~Z80_FLAG_C;
    Z80_ADC_A(z80, b);
}

static ZYMOSIS_INLINE void Z80_SUB_A (Z80Info *z80, uint8_t b)
{
    z80->af.f &= ~Z80_FLAG_C;
    Z80_SBC_A(z80, b);
}

static ZYMOSIS_INLINE void Z80_CP_A (Z80Info *z80, uint8_t b)
{
    uint8_t o = z80->af.a, new = ((int32_t)o - (int32_t)b) & 0xff;
    /***/
    z80->af.f =
        Z80_FLAG_N |
        (new & Z80_FLAG_S) |
        (b & Z80_FLAG_35) |
        (new == 0 ? Z80_FLAG_Z : 0) |
        (o < b ? Z80_FLAG_C : 0) |
        ((o ^ b) & (o ^ new) & 0x80 ? Z80_FLAG_PV : 0) |
        ((int32_t)(o & 0x0f) - (int32_t)(b & 0x0f) < 0 ? Z80_FLAG_H : 0);
}


#define Z80_AND_A(_z80,_b) ((_z80)->af.f = sz53pTable[(_z80)->af.a&=(_b)]|Z80_FLAG_H)
#define Z80_OR_A(_z80,_b)  ((_z80)->af.f = sz53pTable[(_z80)->af.a|=(_b)])
#define Z80_XOR_A(_z80,_b) ((_z80)->af.f = sz53pTable[(_z80)->af.a^=(_b)])


/* carry unchanged */
static ZYMOSIS_INLINE uint8_t Z80_DEC8 (Z80Info *z80, uint8_t b)
{
    z80->af.f &= Z80_FLAG_C;
    z80->af.f |= Z80_FLAG_N |
                 (b == 0x80 ? Z80_FLAG_PV : 0) |
                 (b & 0x0f ? 0 : Z80_FLAG_H) |
                 sz53Table[(((int)b) - 1) & 0xff];
    return (((int)b) - 1) & 0xff;
}

/* carry unchanged */
static ZYMOSIS_INLINE uint8_t Z80_INC8 (Z80Info *z80, uint8_t b)
{
    z80->af.f &= Z80_FLAG_C;
    z80->af.f |=
        (b == 0x7f ? Z80_FLAG_PV : 0) |
        ((b + 1) & 0x0f ? 0 : Z80_FLAG_H ) |
        sz53Table[(b + 1) & 0xff];
    return ((b + 1) & 0xff);
}


/* cyclic, carry reflects shifted bit */
static ZYMOSIS_INLINE void Z80_RLCA (Z80Info *z80)
{
    uint8_t c = ((z80->af.a >> 7) & 0x01);
    /***/
    z80->af.a = (z80->af.a << 1) | c;
    z80->af.f = c | (z80->af.a & Z80_FLAG_35) | (z80->af.f & (Z80_FLAG_PV | Z80_FLAG_Z | Z80_FLAG_S));
}

/* cyclic, carry reflects shifted bit */
static ZYMOSIS_INLINE void Z80_RRCA (Z80Info *z80)
{
    uint8_t c = (z80->af.a & 0x01);
    /***/
    z80->af.a = (z80->af.a >> 1) | (c << 7);
    z80->af.f = c | (z80->af.a & Z80_FLAG_35) | (z80->af.f & (Z80_FLAG_PV | Z80_FLAG_Z | Z80_FLAG_S));
}


/* cyclic thru carry */
static ZYMOSIS_INLINE void Z80_RLA (Z80Info *z80)
{
    uint8_t c = ((z80->af.a >> 7) & 0x01);
    /***/
    z80->af.a = (z80->af.a << 1) | (z80->af.f & Z80_FLAG_C);
    z80->af.f = c | (z80->af.a & Z80_FLAG_35) | (z80->af.f & (Z80_FLAG_PV | Z80_FLAG_Z | Z80_FLAG_S));
}

/* cyclic thru carry */
static ZYMOSIS_INLINE void Z80_RRA (Z80Info *z80)
{
    uint8_t c = (z80->af.a & 0x01);
    /***/
    z80->af.a = (z80->af.a >> 1) | ((z80->af.f & Z80_FLAG_C) << 7);
    z80->af.f = c | (z80->af.a & Z80_FLAG_35) | (z80->af.f & (Z80_FLAG_PV | Z80_FLAG_Z | Z80_FLAG_S));
}

/* cyclic thru carry */
static ZYMOSIS_INLINE uint8_t Z80_RL (Z80Info *z80, uint8_t b)
{
    uint8_t c = (b >> 7)&Z80_FLAG_C;
    /***/
    z80->af.f = sz53pTable[(b = ((b << 1) & 0xff) | (z80->af.f & Z80_FLAG_C))] | c;
    return b;
}


static ZYMOSIS_INLINE uint8_t Z80_RR (Z80Info *z80, uint8_t b)
{
    uint8_t c = (b & 0x01);
    /***/
    z80->af.f = sz53pTable[(b = (b >> 1) | ((z80->af.f & Z80_FLAG_C) << 7))] | c;
    return b;
}

/* cyclic, carry reflects shifted bit */
static ZYMOSIS_INLINE uint8_t Z80_RLC (Z80Info *z80, uint8_t b)
{
    uint8_t c = ((b >> 7)&Z80_FLAG_C);
    /***/
    z80->af.f = sz53pTable[(b = ((b << 1) & 0xff) | c)] | c;
    return b;
}

/* cyclic, carry reflects shifted bit */
static ZYMOSIS_INLINE uint8_t Z80_RRC (Z80Info *z80, uint8_t b)
{
    uint8_t c = (b & 0x01);
    /***/
    z80->af.f = sz53pTable[(b = (b >> 1) | (c << 7))] | c;
    return b;
}

static ZYMOSIS_INLINE uint8_t Z80_SLA (Z80Info *z80, uint8_t b)
{
    uint8_t c = ((b >> 7) & 0x01);
    /***/
    z80->af.f = sz53pTable[(b <<= 1)] | c;
    return b;
}

static ZYMOSIS_INLINE uint8_t Z80_SRA (Z80Info *z80, uint8_t b)
{
    uint8_t c = (b & 0x01);
    /***/
    z80->af.f = sz53pTable[(b = (b >> 1) | (b & 0x80))] | c;
    return b;
}

static ZYMOSIS_INLINE uint8_t Z80_SLL (Z80Info *z80, uint8_t b)
{
    uint8_t c = ((b >> 7) & 0x01);
    /***/
    z80->af.f = sz53pTable[(b = (b << 1) | 0x01)] | c;
    return b;
}

static ZYMOSIS_INLINE uint8_t Z80_SLR (Z80Info *z80, uint8_t b)
{
    uint8_t c = (b & 0x01);
    /***/
    z80->af.f = sz53pTable[(b >>= 1)] | c;
    return b;
}


/* ddvalue+value */
static ZYMOSIS_INLINE uint16_t Z80_ADD_DD (Z80Info *z80, uint16_t value, uint16_t ddvalue)
{
    static const uint8_t hct[8] = { 0, Z80_FLAG_H, Z80_FLAG_H, Z80_FLAG_H, 0, 0, 0, Z80_FLAG_H };
    uint32_t res = (uint32_t)value + (uint32_t)ddvalue;
    uint8_t b = ((value & 0x0800) >> 11) | ((ddvalue & 0x0800) >> 10) | ((res & 0x0800) >> 9);
    /***/
    z80->memptr.w = (ddvalue + 1) & 0xffff;
    z80->af.f =
        (z80->af.f & (Z80_FLAG_PV | Z80_FLAG_Z | Z80_FLAG_S)) |
        (res > 0xffff ? Z80_FLAG_C : 0) |
        ((res >> 8)&Z80_FLAG_35) |
        hct[b];
    return res;
}

/* ddvalue+value */
static ZYMOSIS_INLINE uint16_t Z80_ADC_DD (Z80Info *z80, uint16_t value, uint16_t ddvalue)
{
    uint8_t c = (z80->af.f & Z80_FLAG_C);
    uint32_t new = (uint32_t)value + (uint32_t)ddvalue + (uint32_t)c;
    uint16_t res = (new & 0xffff);
    /***/
    z80->memptr.w = (ddvalue + 1) & 0xffff;
    z80->af.f =
        ((res >> 8)&Z80_FLAG_S35) |
        (res == 0 ? Z80_FLAG_Z : 0) |
        (new > 0xffff ? Z80_FLAG_C : 0) |
        ((value ^ ((~ddvalue) & 0xffff)) & (value ^ new) & 0x8000 ? Z80_FLAG_PV : 0) |
        ((value & 0x0fff) + (ddvalue & 0x0fff) + c >= 0x1000 ? Z80_FLAG_H : 0);
    return res;
}

/* ddvalue-value */
static ZYMOSIS_INLINE uint16_t Z80_SBC_DD (Z80Info *z80, uint16_t value, uint16_t ddvalue)
{
    uint16_t res;
    uint8_t tmpB = z80->af.a;
    /***/
    z80->memptr.w = (ddvalue + 1) & 0xffff;
    z80->af.a = ddvalue & 0xff;
    Z80_SBC_A(z80, value & 0xff);
    res = z80->af.a;
    z80->af.a = (ddvalue >> 8) & 0xff;
    Z80_SBC_A(z80, (value >> 8) & 0xff);
    res |= (z80->af.a << 8);
    z80->af.a = tmpB;
    z80->af.f = (res ? z80->af.f & (~Z80_FLAG_Z) : z80->af.f | Z80_FLAG_Z);
    return res;
}


static ZYMOSIS_INLINE void Z80_BIT (Z80Info *z80, uint8_t bit, uint8_t num, int mptr)
{
    z80->af.f =
        Z80_FLAG_H |
        (z80->af.f & Z80_FLAG_C) |
        (num & Z80_FLAG_35) |
        (num & (1 << bit) ? 0 : Z80_FLAG_PV | Z80_FLAG_Z) |
        (bit == 7 ? num&Z80_FLAG_S : 0);
    if (mptr) z80->af.f = (z80->af.f & ~Z80_FLAG_35) | (z80->memptr.h & Z80_FLAG_35);
}


static ZYMOSIS_INLINE void Z80_DAA (Z80Info *z80)
{
    uint8_t tmpI = 0, tmpC = (z80->af.f & Z80_FLAG_C), tmpA = z80->af.a;
    /***/
    if ((z80->af.f & Z80_FLAG_H) || (tmpA & 0x0f) > 9) tmpI = 6;
    if (tmpC != 0 || tmpA > 0x99) tmpI |= 0x60;
    if (tmpA > 0x99) tmpC = Z80_FLAG_C;
    if (z80->af.f & Z80_FLAG_N) Z80_SUB_A(z80, tmpI);
    else Z80_ADD_A(z80, tmpI);
    z80->af.f = (z80->af.f & ~(Z80_FLAG_C | Z80_FLAG_PV)) | tmpC | parityTable[z80->af.a];
}


static ZYMOSIS_INLINE void Z80_RRD_A (Z80Info *z80)
{
    uint8_t tmpB = Z80_PeekB3T(z80, z80->hl.w);
    /*IOP(4)*/
    z80->memptr.w = (z80->hl.w + 1) & 0xffff;
    Z80_ContentionBy1(z80, z80->hl.w, 4);
    Z80_PokeB3T(z80, z80->hl.w, (z80->af.a << 4) | (tmpB >> 4));
    z80->af.a = (z80->af.a & 0xf0) | (tmpB & 0x0f);
    z80->af.f = (z80->af.f & Z80_FLAG_C) | sz53pTable[z80->af.a];
}

static ZYMOSIS_INLINE void Z80_RLD_A (Z80Info *z80)
{
    uint8_t tmpB = Z80_PeekB3T(z80, z80->hl.w);
    /*IOP(4)*/
    z80->memptr.w = (z80->hl.w + 1) & 0xffff;
    Z80_ContentionBy1(z80, z80->hl.w, 4);
    Z80_PokeB3T(z80, z80->hl.w, (tmpB << 4) | (z80->af.a & 0x0f));
    z80->af.a = (z80->af.a & 0xf0) | (tmpB >> 4);
    z80->af.f = (z80->af.f & Z80_FLAG_C) | sz53pTable[z80->af.a];
}


static ZYMOSIS_INLINE void Z80_LD_A_IR (Z80Info *z80, uint8_t ir)
{
    z80->af.a = ir;
    z80->prev_was_EIDDR = -1;
    Z80_ContentionIRBy1(z80, 1);
    z80->af.f = sz53Table[z80->af.a] | (z80->af.f & Z80_FLAG_C) | (z80->iff2 ? Z80_FLAG_PV : 0);
}


/******************************************************************************/
#define INC_R  (z80->regR = ((z80->regR+1)&0x7f)|(z80->regR&0x80))

#define SET_TRUE_CC \
  switch ((opcode>>3)&0x07) { \
    case 0: trueCC = (z80->af.f&Z80_FLAG_Z) == 0; break; \
    case 1: trueCC = (z80->af.f&Z80_FLAG_Z) != 0; break; \
    case 2: trueCC = (z80->af.f&Z80_FLAG_C) == 0; break; \
    case 3: trueCC = (z80->af.f&Z80_FLAG_C) != 0; break; \
    case 4: trueCC = (z80->af.f&Z80_FLAG_PV) == 0; break; \
    case 5: trueCC = (z80->af.f&Z80_FLAG_PV) != 0; break; \
    case 6: trueCC = (z80->af.f&Z80_FLAG_S) == 0; break; \
    case 7: trueCC = (z80->af.f&Z80_FLAG_S) != 0; break; \
  }

#define INC_PC  (z80->pc = (z80->pc+1)&0xffff)
#define DEC_PC  (z80->pc = ((int32_t)(z80->pc)-1)&0xffff)

#define INC_W(n)  ((n) = ((n)+1)&0xffff)
#define DEC_W(n)  ((n) = ((int32_t)(n)-1)&0xffff)

#define XADD_W(n,v)  ((n) = ((n)+v)&0xffff)
#define XSUB_W(n,v)  ((n) = ((int32_t)(n)-v)&0xffff)

#define ZADD_W(n,v)  ((n) = ((int32_t)(n)+v)&0xffff)

#define ZADD_WX(n,v)  (((int32_t)(n)+v)&0xffff)

#define INC_B(n)  ((n) = ((n)+1)&0xff)
#define DEC_B(n)  ((n) = ((int32_t)(n)-1)&0xff)

/* t1: setting /MREQ & /RD */
/* t2: memory read */
/* t3, t4: decode command, increment R */
#define GET_OPCODE(_opc)  do { \
  Z80_Contention(z80, z80->pc, 4, Z80_MREQ_READ|Z80_MEMIO_OPCODE); \
  if (z80->evenM1 && (z80->tstates&0x01)) ++z80->tstates; \
  (_opc) = z80->memReadFn(z80, z80->pc, Z80_MEMIO_OPCODE); \
  z80->pc = (z80->pc+1)&0xffff; \
  z80->regR = ((z80->regR+1)&0x7f)|(z80->regR&0x80); \
} while (0)

#define GET_OPCODE_EXT(_opc)  do { \
  Z80_Contention(z80, z80->pc, 4, Z80_MREQ_READ|Z80_MEMIO_OPCEXT); \
  (_opc) = z80->memReadFn(z80, z80->pc, Z80_MEMIO_OPCEXT); \
  z80->pc = (z80->pc+1)&0xffff; \
  z80->regR = ((z80->regR+1)&0x7f)|(z80->regR&0x80); \
} while (0)


#define CBX_REPEATED (opcode&0x10)
#define CBX_BACKWARD (opcode&0x08)


void Z80_Execute (Z80Info *z80)
{
    uint8_t opcode;
    int gotDD, trueCC; /* booleans */
    int disp;
    uint8_t tmpB, tmpC, rsrc, rdst;
    uint16_t tmpW = 0; /* shut up the compiler; it's wrong but stubborn */
    /***/
    while (z80->tstates < z80->next_event_tstate)
    {
        if (z80->pagerFn != NULL) z80->pagerFn(z80);
        if (z80->checkBPFn != NULL && z80->checkBPFn(z80)) return;
        z80->prev_pc = z80->org_pc;
        z80->org_pc = z80->pc;
        /* read opcode -- OCR(4) */
        GET_OPCODE(opcode);
        z80->prev_was_EIDDR = 0;
        disp = gotDD = 0;
        z80->dd = &z80->hl;
        if (z80->halted)
        {
            DEC_W(z80->pc);
            continue;
        }
        /***/
        if (opcode == 0xdd || opcode == 0xfd)
        {
            static const uint32_t withIndexBmp[8] = {0x00, 0x700000, 0x40404040, 0x40bf4040, 0x40404040, 0x40404040, 0x0800, 0x00};
            /* IX/IY prefix */
            z80->dd = (opcode == 0xdd ? &z80->ix : &z80->iy);
            /* read opcode -- OCR(4) */
            GET_OPCODE_EXT(opcode);
            /* test if this instruction have (HL) */
            if (withIndexBmp[opcode >> 5] & (1 << (opcode & 0x1f)))
            {
                /* 3rd byte is always DISP here */
                disp = Z80_PeekB3TA(z80, z80->pc);
                if (disp > 127) disp -= 256;
                INC_PC;
                z80->memptr.w = ZADD_WX(z80->dd->w, disp);
            }
            else if (opcode == 0xdd && opcode == 0xfd)
            {
                /* double prefix; restart main loop */
                z80->prev_was_EIDDR = 1;
                continue;
            }
            gotDD = 1;
        }
        /* instructions */
        if (opcode == 0xed)
        {
            z80->dd = &z80->hl; /* а нас -- рать! */
            /* read opcode -- OCR(4) */
            GET_OPCODE_EXT(opcode);
            switch (opcode)
            {
            /* LDI, LDIR, LDD, LDDR */
            case 0xa0:
            case 0xb0:
            case 0xa8:
            case 0xb8:
                tmpB = Z80_PeekB3T(z80, z80->hl.w);
                Z80_PokeB3T(z80, z80->de.w, tmpB);
                /*MWR(5)*/
                Z80_ContentionBy1(z80, z80->de.w, 2);
                DEC_W(z80->bc.w);
                tmpB = (tmpB + z80->af.a) & 0xff;
                /***/
                z80->af.f =
                    (tmpB & Z80_FLAG_3) | (z80->af.f & (Z80_FLAG_C | Z80_FLAG_Z | Z80_FLAG_S)) |
                    (z80->bc.w != 0 ? Z80_FLAG_PV : 0) |
                    (tmpB & 0x02 ? Z80_FLAG_5 : 0);
                /***/
                if (CBX_REPEATED)
                {
                    if (z80->bc.w != 0)
                    {
                        /*IOP(5)*/
                        Z80_ContentionBy1(z80, z80->de.w, 5);
                        /* do it again */
                        XSUB_W(z80->pc, 2);
                        z80->memptr.w = (z80->pc + 1) & 0xffff;
                    }
                }
                if (!CBX_BACKWARD)
                {
                    INC_W(z80->hl.w);
                    INC_W(z80->de.w);
                }
                else
                {
                    DEC_W(z80->hl.w);
                    DEC_W(z80->de.w);
                }
                break;
            /* CPI, CPIR, CPD, CPDR */
            case 0xa1:
            case 0xb1:
            case 0xa9:
            case 0xb9:
                /* MEMPTR */
                if (CBX_REPEATED && (!(z80->bc.w == 1 || Z80_PeekBI(z80, z80->hl.w) == z80->af.a)))
                {
                    z80->memptr.w = ZADD_WX(z80->org_pc, 1);
                }
                else
                {
                    z80->memptr.w = ZADD_WX(z80->memptr.w, (CBX_BACKWARD ? -1 : 1));
                }
                /***/
                tmpB = Z80_PeekB3T(z80, z80->hl.w);
                /*IOP(5)*/
                Z80_ContentionBy1(z80, z80->hl.w, 5);
                DEC_W(z80->bc.w);
                /***/
                z80->af.f =
                    Z80_FLAG_N |
                    (z80->af.f & Z80_FLAG_C) |
                    (z80->bc.w != 0 ? Z80_FLAG_PV : 0) |
                    ((int32_t)(z80->af.a & 0x0f) - (int32_t)(tmpB & 0x0f) < 0 ? Z80_FLAG_H : 0);
                /***/
                tmpB = ((int32_t)z80->af.a - (int32_t)tmpB) & 0xff;
                /***/
                z80->af.f |=
                    (tmpB == 0 ? Z80_FLAG_Z : 0) |
                    (tmpB & Z80_FLAG_S);
                /***/
                if (z80->af.f & Z80_FLAG_H) tmpB = ((uint16_t)tmpB - 1) & 0xff;
                z80->af.f |= (tmpB & Z80_FLAG_3) | (tmpB & 0x02 ? Z80_FLAG_5 : 0);
                /***/
                if (CBX_REPEATED)
                {
                    /* repeated */
                    if ((z80->af.f & (Z80_FLAG_Z | Z80_FLAG_PV)) == Z80_FLAG_PV)
                    {
                        /*IOP(5)*/
                        Z80_ContentionBy1(z80, z80->hl.w, 5);
                        /* do it again */
                        XSUB_W(z80->pc, 2);
                    }
                }
                if (CBX_BACKWARD) DEC_W(z80->hl.w);
                else INC_W(z80->hl.w);
                break;
            /* OUTI, OTIR, OUTD, OTDR */
            case 0xa3:
            case 0xb3:
            case 0xab:
            case 0xbb:
                DEC_B(z80->bc.b);
            /* fallthru */
            /* INI, INIR, IND, INDR */
            case 0xa2:
            case 0xb2:
            case 0xaa:
            case 0xba:
                z80->memptr.w = ZADD_WX(z80->bc.w, (CBX_BACKWARD ? -1 : 1));
                /*OCR(5)*/
                Z80_ContentionIRBy1(z80, 1);
                if (opcode & 0x01)
                {
                    /* OUT* */
                    tmpB = Z80_PeekB3T(z80, z80->hl.w);/*MRD(3)*/
                    Z80_PortOut(z80, z80->bc.w, tmpB);
                    tmpW = ZADD_WX(z80->hl.w, (CBX_BACKWARD ? -1 : 1));
                    tmpC = (tmpB + tmpW) & 0xff;
                }
                else
                {
                    /* IN* */
                    tmpB = Z80_PortIn(z80, z80->bc.w);
                    Z80_PokeB3T(z80, z80->hl.w, tmpB);/*MWR(3)*/
                    DEC_B(z80->bc.b);
                    if (CBX_BACKWARD) tmpC = ((int32_t)tmpB + (int32_t)z80->bc.c - 1) & 0xff;
                    else tmpC = (tmpB + z80->bc.c + 1) & 0xff;
                }
                /***/
                z80->af.f =
                    (tmpB & 0x80 ? Z80_FLAG_N : 0) |
                    (tmpC < tmpB ? Z80_FLAG_H | Z80_FLAG_C : 0) |
                    parityTable[(tmpC & 0x07)^z80->bc.b] |
                    sz53Table[z80->bc.b];
                /***/
                if (CBX_REPEATED)
                {
                    /* repeating commands */
                    if (z80->bc.b != 0)
                    {
                        uint16_t a = (opcode & 0x01 ? z80->bc.w : z80->hl.w);
                        /***/
                        /*IOP(5)*/
                        Z80_ContentionBy1(z80, a, 5);
                        /* do it again */
                        XSUB_W(z80->pc, 2);
                    }
                }
                if (CBX_BACKWARD) DEC_W(z80->hl.w);
                else INC_W(z80->hl.w);
                break;
            /* not strings, but some good instructions anyway */
            default:
                if ((opcode & 0xc0) == 0x40)
                {
                    /* 0x40...0x7f */
                    switch (opcode & 0x07)
                    {
                    /* IN r8,(C) */
                    case 0:
                        z80->memptr.w = ZADD_WX(z80->bc.w, 1);
                        tmpB = Z80_PortIn(z80, z80->bc.w);
                        z80->af.f = sz53pTable[tmpB] | (z80->af.f & Z80_FLAG_C);
                        switch ((opcode >> 3) & 0x07)
                        {
                        case 0:
                            z80->bc.b = tmpB;
                            break;
                        case 1:
                            z80->bc.c = tmpB;
                            break;
                        case 2:
                            z80->de.d = tmpB;
                            break;
                        case 3:
                            z80->de.e = tmpB;
                            break;
                        case 4:
                            z80->hl.h = tmpB;
                            break;
                        case 5:
                            z80->hl.l = tmpB;
                            break;
                        case 7:
                            z80->af.a = tmpB;
                            break;
                            /* 6 affects only flags */
                        }
                        break;
                    /* OUT (C),r8 */
                    case 1:
                        z80->memptr.w = ZADD_WX(z80->bc.w, 1);
                        switch ((opcode >> 3) & 0x07)
                        {
                        case 0:
                            tmpB = z80->bc.b;
                            break;
                        case 1:
                            tmpB = z80->bc.c;
                            break;
                        case 2:
                            tmpB = z80->de.d;
                            break;
                        case 3:
                            tmpB = z80->de.e;
                            break;
                        case 4:
                            tmpB = z80->hl.h;
                            break;
                        case 5:
                            tmpB = z80->hl.l;
                            break;
                        case 7:
                            tmpB = z80->af.a;
                            break;
                        default:
                            tmpB = 0;
                            break; /*6*/
                        }
                        Z80_PortOut(z80, z80->bc.w, tmpB);
                        break;
                    /* SBC HL,rr/ADC HL,rr */
                    case 2:
                        /*IOP(4),IOP(3)*/
                        Z80_ContentionIRBy1(z80, 7);
                        switch ((opcode >> 4) & 0x03)
                        {
                        case 0:
                            tmpW = z80->bc.w;
                            break;
                        case 1:
                            tmpW = z80->de.w;
                            break;
                        case 2:
                            tmpW = z80->hl.w;
                            break;
                        default:
                            tmpW = z80->sp.w;
                            break;
                        }
                        z80->hl.w = (opcode & 0x08 ? Z80_ADC_DD(z80, tmpW, z80->hl.w) : Z80_SBC_DD(z80, tmpW, z80->hl.w));
                        break;
                    /* LD (nn),rr/LD rr,(nn) */
                    case 3:
                        tmpW = Z80_GetWordPC(z80, 0);
                        z80->memptr.w = (tmpW + 1) & 0xffff;
                        if (opcode & 0x08)
                        {
                            /* LD rr,(nn) */
                            switch ((opcode >> 4) & 0x03)
                            {
                            case 0:
                                z80->bc.w = Z80_PeekW6T(z80, tmpW);
                                break;
                            case 1:
                                z80->de.w = Z80_PeekW6T(z80, tmpW);
                                break;
                            case 2:
                                z80->hl.w = Z80_PeekW6T(z80, tmpW);
                                break;
                            case 3:
                                z80->sp.w = Z80_PeekW6T(z80, tmpW);
                                break;
                            }
                        }
                        else
                        {
                            /* LD (nn),rr */
                            switch ((opcode >> 4) & 0x03)
                            {
                            case 0:
                                Z80_PokeW6T(z80, tmpW, z80->bc.w);
                                break;
                            case 1:
                                Z80_PokeW6T(z80, tmpW, z80->de.w);
                                break;
                            case 2:
                                Z80_PokeW6T(z80, tmpW, z80->hl.w);
                                break;
                            case 3:
                                Z80_PokeW6T(z80, tmpW, z80->sp.w);
                                break;
                            }
                        }
                        break;
                    /* NEG */
                    case 4:
                        tmpB = z80->af.a;
                        z80->af.a = 0;
                        Z80_SUB_A(z80, tmpB);
                        break;
                    /* RETI/RETN */
                    case 5:
                        /*RETI: 0x4d, 0x5d, 0x6d, 0x7d*/
                        /*RETN: 0x45, 0x55, 0x65, 0x75*/
                        z80->iff1 = z80->iff2;
                        z80->memptr.w = z80->pc = Z80_Pop6T(z80);
                        if (opcode & 0x08)
                        {
                            /* RETI */
                            if (z80->retiFn != NULL && z80->retiFn(z80, opcode)) return;
                        }
                        else
                        {
                            /* RETN */
                            if (z80->retnFn != NULL && z80->retnFn(z80, opcode)) return;
                        }
                        break;
                    /* IM n */
                    case 6:
                        switch (opcode)
                        {
                        case 0x56:
                        case 0x76:
                            z80->im = 1;
                            break;
                        case 0x5e:
                        case 0x7e:
                            z80->im = 2;
                            break;
                        default:
                            z80->im = 0;
                            break;
                        }
                        break;
                    /* specials */
                    case 7:
                        switch (opcode)
                        {
                        /* LD I,A */
                        case 0x47:
                            /*OCR(5)*/
                            Z80_ContentionIRBy1(z80, 1);
                            z80->regI = z80->af.a;
                            break;
                        /* LD R,A */
                        case 0x4f:
                            /*OCR(5)*/
                            Z80_ContentionIRBy1(z80, 1);
                            z80->regR = z80->af.a;
                            break;
                        /* LD A,I */
                        case 0x57:
                            Z80_LD_A_IR(z80, z80->regI);
                            break;
                        /* LD A,R */
                        case 0x5f:
                            Z80_LD_A_IR(z80, z80->regR);
                            break;
                        /* RRD */
                        case 0x67:
                            Z80_RRD_A(z80);
                            break;
                        /* RLD */
                        case 0x6F:
                            Z80_RLD_A(z80);
                            break;
                        }
                    }
                }
                else
                {
                    /* slt and other traps */
                    if (z80->trapEDFn != NULL && z80->trapEDFn(z80, opcode)) return;
                }
                break;
            }
            continue;
        } /* 0xed done */
        /***/
        if (opcode == 0xcb)
        {
            /* shifts and bit operations */
            /* read opcode -- OCR(4) */
            if (!gotDD)
            {
                GET_OPCODE_EXT(opcode);
            }
            else
            {
                Z80_Contention(z80, z80->pc, 3, Z80_MREQ_READ | Z80_MEMIO_OPCEXT);
                opcode = z80->memReadFn(z80, z80->pc, Z80_MEMIO_OPCEXT);
                Z80_ContentionPCBy1(z80, 2);
                INC_PC;
            }
            if (gotDD)
            {
                tmpW = ZADD_WX(z80->dd->w, disp);
                tmpB = Z80_PeekB3T(z80, tmpW);
                Z80_ContentionBy1(z80, tmpW, 1);
            }
            else
            {
                switch (opcode & 0x07)
                {
                case 0:
                    tmpB = z80->bc.b;
                    break;
                case 1:
                    tmpB = z80->bc.c;
                    break;
                case 2:
                    tmpB = z80->de.d;
                    break;
                case 3:
                    tmpB = z80->de.e;
                    break;
                case 4:
                    tmpB = z80->hl.h;
                    break;
                case 5:
                    tmpB = z80->hl.l;
                    break;
                case 6:
                    tmpB = Z80_PeekB3T(z80, z80->hl.w);
                    Z80_Contention(z80, z80->hl.w, 1, Z80_MREQ_READ | Z80_MEMIO_DATA);
                    break;
                case 7:
                    tmpB = z80->af.a;
                    break;
                }
            }
            switch ((opcode >> 3) & 0x1f)
            {
            case 0:
                tmpB = Z80_RLC(z80, tmpB);
                break;
            case 1:
                tmpB = Z80_RRC(z80, tmpB);
                break;
            case 2:
                tmpB = Z80_RL(z80, tmpB);
                break;
            case 3:
                tmpB = Z80_RR(z80, tmpB);
                break;
            case 4:
                tmpB = Z80_SLA(z80, tmpB);
                break;
            case 5:
                tmpB = Z80_SRA(z80, tmpB);
                break;
            case 6:
                tmpB = Z80_SLL(z80, tmpB);
                break;
            case 7:
                tmpB = Z80_SLR(z80, tmpB);
                break;
            default:
                switch ((opcode >> 6) & 0x03)
                {
                case 1:
                    Z80_BIT(z80, (opcode >> 3) & 0x07, tmpB, (gotDD || (opcode & 0x07) == 6));
                    break;
                case 2:
                    tmpB &= ~(1 << ((opcode >> 3) & 0x07));
                    break; /* RES */
                case 3:
                    tmpB |= (1 << ((opcode >> 3) & 0x07));
                    break; /* SET */
                }
                break;
            }
            /***/
            if ((opcode & 0xc0) != 0x40)
            {
                /* BITs are not welcome here */
                if (gotDD)
                {
                    /* tmpW was set earlier */
                    if ((opcode & 0x07) != 6) Z80_PokeB3T(z80, tmpW, tmpB);
                }
                switch (opcode & 0x07)
                {
                case 0:
                    z80->bc.b = tmpB;
                    break;
                case 1:
                    z80->bc.c = tmpB;
                    break;
                case 2:
                    z80->de.d = tmpB;
                    break;
                case 3:
                    z80->de.e = tmpB;
                    break;
                case 4:
                    z80->hl.h = tmpB;
                    break;
                case 5:
                    z80->hl.l = tmpB;
                    break;
                case 6:
                    Z80_PokeB3T(z80, ZADD_WX(z80->dd->w, disp), tmpB);
                    break;
                case 7:
                    z80->af.a = tmpB;
                    break;
                }
            }
            continue;
        } /* 0xcb done */
        /* normal things */
        switch (opcode & 0xc0)
        {
        /* 0x00..0x3F */
        case 0x00:
            switch (opcode & 0x07)
            {
            /* misc,DJNZ,JR,JR cc */
            case 0:
                if (opcode & 0x30)
                {
                    /* branches */
                    if (opcode & 0x20)
                    {
                        /* JR cc */
                        switch ((opcode >> 3) & 0x03)
                        {
                        case 0:
                            trueCC = (z80->af.f & Z80_FLAG_Z) == 0;
                            break;
                        case 1:
                            trueCC = (z80->af.f & Z80_FLAG_Z) != 0;
                            break;
                        case 2:
                            trueCC = (z80->af.f & Z80_FLAG_C) == 0;
                            break;
                        case 3:
                            trueCC = (z80->af.f & Z80_FLAG_C) != 0;
                            break;
                        default:
                            trueCC = 0;
                            break;
                        }
                    }
                    else
                    {
                        /* DJNZ/JR */
                        if ((opcode & 0x08) == 0)
                        {
                            /* DJNZ */
                            /*OCR(5)*/
                            Z80_ContentionIRBy1(z80, 1);
                            DEC_B(z80->bc.b);
                            trueCC = (z80->bc.b != 0);
                        }
                        else
                        {
                            /* JR */
                            trueCC = 1;
                        }
                    }
                    /***/
                    disp = Z80_PeekB3TA(z80, z80->pc);
                    if (trueCC)
                    {
                        /* execute branch (relative) */
                        /*IOP(5)*/
                        if (disp > 127) disp -= 256;
                        Z80_ContentionPCBy1(z80, 5);
                        INC_PC;
                        ZADD_W(z80->pc, disp);
                        z80->memptr.w = z80->pc;
                    }
                    else
                    {
                        INC_PC;
                    }
                }
                else
                {
                    /* EX AF,AF' or NOP */
                    if (opcode != 0) Z80_EXAFAF(z80);
                }
                break;
            /* LD rr,nn/ADD HL,rr */
            case 1:
                if (opcode & 0x08)
                {
                    /* ADD HL,rr */
                    /*IOP(4),IOP(3)*/
                    Z80_ContentionIRBy1(z80, 7);
                    switch ((opcode >> 4) & 0x03)
                    {
                    case 0:
                        z80->dd->w = Z80_ADD_DD(z80, z80->bc.w, z80->dd->w);
                        break;
                    case 1:
                        z80->dd->w = Z80_ADD_DD(z80, z80->de.w, z80->dd->w);
                        break;
                    case 2:
                        z80->dd->w = Z80_ADD_DD(z80, z80->dd->w, z80->dd->w);
                        break;
                    case 3:
                        z80->dd->w = Z80_ADD_DD(z80, z80->sp.w, z80->dd->w);
                        break;
                    }
                }
                else
                {
                    /* LD rr,nn */
                    tmpW = Z80_GetWordPC(z80, 0);
                    switch ((opcode >> 4) & 0x03)
                    {
                    case 0:
                        z80->bc.w = tmpW;
                        break;
                    case 1:
                        z80->de.w = tmpW;
                        break;
                    case 2:
                        z80->dd->w = tmpW;
                        break;
                    case 3:
                        z80->sp.w = tmpW;
                        break;
                    }
                }
                break;
            /* LD xxx,xxx */
            case 2:
                switch ((opcode >> 3) & 0x07)
                {
                /* LD (BC),A */
                case 0:
                    Z80_PokeB3T(z80, z80->bc.w, z80->af.a);
                    z80->memptr.l = (z80->bc.c + 1) & 0xff;
                    z80->memptr.h = z80->af.a;
                    break;
                /* LD A,(BC) */
                case 1:
                    z80->af.a = Z80_PeekB3T(z80, z80->bc.w);
                    z80->memptr.w = (z80->bc.w + 1) & 0xffff;
                    break;
                /* LD (DE),A */
                case 2:
                    Z80_PokeB3T(z80, z80->de.w, z80->af.a);
                    z80->memptr.l = (z80->de.e+1) & 0xff;
                    z80->memptr.h = z80->af.a;
                    break;
                /* LD A,(DE) */
                case 3:
                    z80->af.a = Z80_PeekB3T(z80, z80->de.w);
                    z80->memptr.w = (z80->de.w + 1) & 0xffff;
                    break;
                /* LD (nn),HL */
                case 4:
                    tmpW = Z80_GetWordPC(z80, 0);
                    z80->memptr.w = (tmpW + 1) & 0xffff;
                    Z80_PokeW6T(z80, tmpW, z80->dd->w);
                    break;
                /* LD HL,(nn) */
                case 5:
                    tmpW = Z80_GetWordPC(z80, 0);
                    z80->memptr.w = (tmpW + 1) & 0xffff;
                    z80->dd->w = Z80_PeekW6T(z80, tmpW);
                    break;
                /* LD (nn),A */
                case 6:
                    tmpW = Z80_GetWordPC(z80, 0);
                    z80->memptr.l = (tmpW + 1) & 0xff;
                    z80->memptr.h = z80->af.a;
                    Z80_PokeB3T(z80, tmpW, z80->af.a);
                    break;
                /* LD A,(nn) */
                case 7:
                    tmpW = Z80_GetWordPC(z80, 0);
                    z80->memptr.w = (tmpW + 1) & 0xffff;
                    z80->af.a = Z80_PeekB3T(z80, tmpW);
                    break;
                }
                break;
            /* INC rr/DEC rr */
            case 3:
                /*OCR(6)*/
                Z80_ContentionIRBy1(z80, 2);
                if (opcode & 0x08)
                {
                    /*DEC*/
                    switch ((opcode >> 4) & 0x03)
                    {
                    case 0:
                        DEC_W(z80->bc.w);
                        break;
                    case 1:
                        DEC_W(z80->de.w);
                        break;
                    case 2:
                        DEC_W(z80->dd->w);
                        break;
                    case 3:
                        DEC_W(z80->sp.w);
                        break;
                    }
                }
                else
                {
                    /*INC*/
                    switch ((opcode >> 4) & 0x03)
                    {
                    case 0:
                        INC_W(z80->bc.w);
                        break;
                    case 1:
                        INC_W(z80->de.w);
                        break;
                    case 2:
                        INC_W(z80->dd->w);
                        break;
                    case 3:
                        INC_W(z80->sp.w);
                        break;
                    }
                }
                break;
            /* INC r8 */
            case 4:
                switch ((opcode >> 3) & 0x07)
                {
                case 0:
                    z80->bc.b = Z80_INC8(z80, z80->bc.b);
                    break;
                case 1:
                    z80->bc.c = Z80_INC8(z80, z80->bc.c);
                    break;
                case 2:
                    z80->de.d = Z80_INC8(z80, z80->de.d);
                    break;
                case 3:
                    z80->de.e = Z80_INC8(z80, z80->de.e);
                    break;
                case 4:
                    z80->dd->h = Z80_INC8(z80, z80->dd->h);
                    break;
                case 5:
                    z80->dd->l = Z80_INC8(z80, z80->dd->l);
                    break;
                case 6:
                    if (gotDD)
                    {
                        DEC_PC;
                        Z80_ContentionPCBy1(z80, 5);
                        INC_PC;
                    }
                    tmpW = ZADD_WX(z80->dd->w, disp);
                    tmpB = Z80_PeekB3T(z80, tmpW);
                    Z80_ContentionBy1(z80, tmpW, 1);
                    tmpB = Z80_INC8(z80, tmpB);
                    Z80_PokeB3T(z80, tmpW, tmpB);
                    break;
                case 7:
                    z80->af.a = Z80_INC8(z80, z80->af.a);
                    break;
                }
                break;
            /* DEC r8 */
            case 5:
                switch ((opcode >> 3) & 0x07)
                {
                case 0:
                    z80->bc.b = Z80_DEC8(z80, z80->bc.b);
                    break;
                case 1:
                    z80->bc.c = Z80_DEC8(z80, z80->bc.c);
                    break;
                case 2:
                    z80->de.d = Z80_DEC8(z80, z80->de.d);
                    break;
                case 3:
                    z80->de.e = Z80_DEC8(z80, z80->de.e);
                    break;
                case 4:
                    z80->dd->h = Z80_DEC8(z80, z80->dd->h);
                    break;
                case 5:
                    z80->dd->l = Z80_DEC8(z80, z80->dd->l);
                    break;
                case 6:
                    if (gotDD)
                    {
                        DEC_PC;
                        Z80_ContentionPCBy1(z80, 5);
                        INC_PC;
                    }
                    tmpW = ZADD_WX(z80->dd->w, disp);
                    tmpB = Z80_PeekB3T(z80, tmpW);
                    Z80_ContentionBy1(z80, tmpW, 1);
                    tmpB = Z80_DEC8(z80, tmpB);
                    Z80_PokeB3T(z80, tmpW, tmpB);
                    break;
                case 7:
                    z80->af.a = Z80_DEC8(z80, z80->af.a);
                    break;
                }
                break;
            /* LD r8,n */
            case 6:
                tmpB = Z80_PeekB3TA(z80, z80->pc);
                INC_PC;
                switch ((opcode >> 3) & 0x07)
                {
                case 0:
                    z80->bc.b = tmpB;
                    break;
                case 1:
                    z80->bc.c = tmpB;
                    break;
                case 2:
                    z80->de.d = tmpB;
                    break;
                case 3:
                    z80->de.e = tmpB;
                    break;
                case 4:
                    z80->dd->h = tmpB;
                    break;
                case 5:
                    z80->dd->l = tmpB;
                    break;
                case 6:
                    if (gotDD)
                    {
                        DEC_PC;
                        Z80_ContentionPCBy1(z80, 2);
                        INC_PC;
                    }
                    tmpW = ZADD_WX(z80->dd->w, disp);
                    Z80_PokeB3T(z80, tmpW, tmpB);
                    break;
                case 7:
                    z80->af.a = tmpB;
                    break;
                }
                break;
            /* swim-swim-hungry */
            case 7:
                switch ((opcode >> 3) & 0x07)
                {
                case 0:
                    Z80_RLCA(z80);
                    break;
                case 1:
                    Z80_RRCA(z80);
                    break;
                case 2:
                    Z80_RLA(z80);
                    break;
                case 3:
                    Z80_RRA(z80);
                    break;
                case 4:
                    Z80_DAA(z80);
                    break;
                case 5: /* CPL */
                    z80->af.a ^= 0xff;
                    z80->af.f = (z80->af.a & Z80_FLAG_35) | (Z80_FLAG_N | Z80_FLAG_H) | (z80->af.f & (Z80_FLAG_C | Z80_FLAG_PV | Z80_FLAG_Z | Z80_FLAG_S));
                    break;
                case 6: /* SCF */
                    z80->af.f = (z80->af.f & (Z80_FLAG_PV | Z80_FLAG_Z | Z80_FLAG_S)) | (z80->af.a & Z80_FLAG_35) | Z80_FLAG_C;
                    break;
                case 7: /* CCF */
                    tmpB = z80->af.f & Z80_FLAG_C;
                    z80->af.f = (z80->af.f & (Z80_FLAG_PV | Z80_FLAG_Z | Z80_FLAG_S)) | (z80->af.a & Z80_FLAG_35);
                    z80->af.f |= tmpB ? Z80_FLAG_H : Z80_FLAG_C;
                    break;
                }
                break;
            }
            break;
        /* 0x40..0x7F (LD r8,r8) */
        case 0x40:
            if (opcode == 0x76)
            {
                z80->halted = 1;    /* HALT */
                DEC_W(z80->pc);
                continue;
            }
            rsrc = (opcode & 0x07);
            rdst = ((opcode >> 3) & 0x07);
            switch (rsrc)
            {
            case 0:
                tmpB = z80->bc.b;
                break;
            case 1:
                tmpB = z80->bc.c;
                break;
            case 2:
                tmpB = z80->de.d;
                break;
            case 3:
                tmpB = z80->de.e;
                break;
            case 4:
                tmpB = (gotDD && rdst == 6 ? z80->hl.h : z80->dd->h);
                break;
            case 5:
                tmpB = (gotDD && rdst == 6 ? z80->hl.l : z80->dd->l);
                break;
            case 6:
                if (gotDD)
                {
                    DEC_PC;
                    Z80_ContentionPCBy1(z80, 5);
                    INC_PC;
                }
                tmpW = ZADD_WX(z80->dd->w, disp);
                tmpB = Z80_PeekB3T(z80, tmpW);
                break;
            case 7:
                tmpB = z80->af.a;
                break;
            }
            switch (rdst)
            {
            case 0:
                z80->bc.b = tmpB;
                break;
            case 1:
                z80->bc.c = tmpB;
                break;
            case 2:
                z80->de.d = tmpB;
                break;
            case 3:
                z80->de.e = tmpB;
                break;
            case 4:
                if (gotDD && rsrc == 6) z80->hl.h = tmpB;
                else z80->dd->h = tmpB;
                break;
            case 5:
                if (gotDD && rsrc == 6) z80->hl.l = tmpB;
                else z80->dd->l = tmpB;
                break;
            case 6:
                if (gotDD)
                {
                    DEC_PC;
                    Z80_ContentionPCBy1(z80, 5);
                    INC_PC;
                }
                tmpW = ZADD_WX(z80->dd->w, disp);
                Z80_PokeB3T(z80, tmpW, tmpB);
                break;
            case 7:
                z80->af.a = tmpB;
                break;
            }
            break;
        /* 0x80..0xBF (ALU A,r8) */
        case 0x80:
            switch (opcode & 0x07)
            {
            case 0:
                tmpB = z80->bc.b;
                break;
            case 1:
                tmpB = z80->bc.c;
                break;
            case 2:
                tmpB = z80->de.d;
                break;
            case 3:
                tmpB = z80->de.e;
                break;
            case 4:
                tmpB = z80->dd->h;
                break;
            case 5:
                tmpB = z80->dd->l;
                break;
            case 6:
                if (gotDD)
                {
                    DEC_PC;
                    Z80_ContentionPCBy1(z80, 5);
                    INC_PC;
                }
                tmpW = ZADD_WX(z80->dd->w, disp);
                tmpB = Z80_PeekB3T(z80, tmpW);
                break;
            case 7:
                tmpB = z80->af.a;
                break;
            }
            switch ((opcode >> 3) & 0x07)
            {
            case 0:
                Z80_ADD_A(z80, tmpB);
                break;
            case 1:
                Z80_ADC_A(z80, tmpB);
                break;
            case 2:
                Z80_SUB_A(z80, tmpB);
                break;
            case 3:
                Z80_SBC_A(z80, tmpB);
                break;
            case 4:
                Z80_AND_A(z80, tmpB);
                break;
            case 5:
                Z80_XOR_A(z80, tmpB);
                break;
            case 6:
                Z80_OR_A(z80, tmpB);
                break;
            case 7:
                Z80_CP_A(z80, tmpB);
                break;
            }
            break;
        /* 0xC0..0xFF */
        case 0xC0:
            switch (opcode & 0x07)
            {
            /* RET cc */
            case 0:
                Z80_ContentionIRBy1(z80, 1);
                SET_TRUE_CC
                if (trueCC) z80->memptr.w = z80->pc = Z80_Pop6T(z80);
                break;
            /* POP rr/special0 */
            case 1:
                if (opcode & 0x08)
                {
                    /* special 0 */
                    switch ((opcode >> 4) & 0x03)
                    {
                    /* RET */
                    case 0:
                        z80->memptr.w = z80->pc = Z80_Pop6T(z80);
                        break;
                    /* EXX */
                    case 1:
                        Z80_EXX(z80);
                        break;
                    /* JP (HL) */
                    case 2:
                        z80->pc = z80->dd->w;
                        break;
                    /* LD SP,HL */
                    case 3:
                        /*OCR(6)*/
                        Z80_ContentionIRBy1(z80, 2);
                        z80->sp.w = z80->dd->w;
                        break;
                    }
                }
                else
                {
                    /* POP rr */
                    tmpW = Z80_Pop6T(z80);
                    switch ((opcode >> 4) & 0x03)
                    {
                    case 0:
                        z80->bc.w = tmpW;
                        break;
                    case 1:
                        z80->de.w = tmpW;
                        break;
                    case 2:
                        z80->dd->w = tmpW;
                        break;
                    case 3:
                        z80->af.w = tmpW;
                        break;
                    }
                }
                break;
            /* JP cc,nn */
            case 2:
                SET_TRUE_CC
                z80->memptr.w = Z80_GetWordPC(z80, 0);
                if (trueCC) z80->pc = z80->memptr.w;
                break;
            /* special1/special3 */
            case 3:
                switch ((opcode >> 3) & 0x07)
                {
                /* JP nn */
                case 0:
                    z80->memptr.w = z80->pc = Z80_GetWordPC(z80, 0);
                    break;
                /* OUT (n),A */
                case 2:
                    tmpW = Z80_PeekB3TA(z80, z80->pc);
                    INC_PC;
                    z80->memptr.l = (tmpW + 1) & 0xff;
                    z80->memptr.h = z80->af.a;
                    tmpW |= (((uint16_t)(z80->af.a)) << 8);
                    Z80_PortOut(z80, tmpW, z80->af.a);
                    break;
                /* IN A,(n) */
                case 3:
                    tmpW = (((uint16_t)(z80->af.a)) << 8) | Z80_PeekB3TA(z80, z80->pc);
                    INC_PC;
                    z80->memptr.w = (tmpW + 1) & 0xffff;
                    z80->af.a = Z80_PortIn(z80, tmpW);
                    break;
                /* EX (SP),HL */
                case 4:
                    /*SRL(3),SRH(4)*/
                    tmpW = Z80_PeekW6T(z80, z80->sp.w);
                    Z80_ContentionBy1(z80, (z80->sp.w + 1) & 0xffff, 1);
                    /*SWL(3),SWH(5)*/
                    Z80_PokeW6TInv(z80, z80->sp.w, z80->dd->w);
                    Z80_ContentionBy1(z80, z80->sp.w, 2);
                    z80->memptr.w = z80->dd->w = tmpW;
                    break;
                /* EX DE,HL */
                case 5:
                    tmpW = z80->de.w;
                    z80->de.w = z80->hl.w;
                    z80->hl.w = tmpW;
                    break;
                /* DI */
                case 6:
                    z80->iff1 = z80->iff2 = 0;
                    break;
                /* EI */
                case 7:
                    z80->iff1 = z80->iff2 = 1;
                    z80->prev_was_EIDDR = 1;
                    break;
                }
                break;
            /* CALL cc,nn */
            case 4:
                SET_TRUE_CC
                z80->memptr.w = Z80_GetWordPC(z80, trueCC);
                if (trueCC)
                {
                    Z80_Push6T(z80, z80->pc);
                    z80->pc = z80->memptr.w;
                }
                break;
            /* PUSH rr/special2 */
            case 5:
                if (opcode & 0x08)
                {
                    if (((opcode >> 4) & 0x03) == 0)
                    {
                        /* CALL */
                        z80->memptr.w = tmpW = Z80_GetWordPC(z80, 1);
                        Z80_Push6T(z80, z80->pc);
                        z80->pc = tmpW;
                    }
                }
                else
                {
                    /* PUSH rr */
                    /*OCR(5)*/
                    Z80_ContentionIRBy1(z80, 1);
                    switch ((opcode >> 4) & 0x03)
                    {
                    case 0:
                        tmpW = z80->bc.w;
                        break;
                    case 1:
                        tmpW = z80->de.w;
                        break;
                    case 2:
                        tmpW = z80->dd->w;
                        break;
                    default:
                        tmpW = z80->af.w;
                        break;
                    }
                    Z80_Push6T(z80, tmpW);
                }
                break;
            /* ALU A,n */
            case 6:
                tmpB = Z80_PeekB3TA(z80, z80->pc);
                INC_PC;
                switch ((opcode >> 3) & 0x07)
                {
                case 0:
                    Z80_ADD_A(z80, tmpB);
                    break;
                case 1:
                    Z80_ADC_A(z80, tmpB);
                    break;
                case 2:
                    Z80_SUB_A(z80, tmpB);
                    break;
                case 3:
                    Z80_SBC_A(z80, tmpB);
                    break;
                case 4:
                    Z80_AND_A(z80, tmpB);
                    break;
                case 5:
                    Z80_XOR_A(z80, tmpB);
                    break;
                case 6:
                    Z80_OR_A(z80, tmpB);
                    break;
                case 7:
                    Z80_CP_A(z80, tmpB);
                    break;
                }
                break;
            /* RST nnn */
            case 7:
                /*OCR(5)*/
                Z80_ContentionIRBy1(z80, 1);
                Z80_Push6T(z80, z80->pc);
                z80->memptr.w = z80->pc = opcode & 0x38;
                break;
            }
            break;
        } /* end switch */
    }
}


int32_t Z80_ExecuteStep (Z80Info *z80)
{
    int32_t one = z80->next_event_tstate, ots = z80->tstates;
    /***/
    z80->next_event_tstate = ots + 1;
    Z80_Execute(z80);
    z80->next_event_tstate = one;
    return z80->tstates - ots;
}


int32_t Z80_ExecuteTS (Z80Info *z80, int32_t tstates)
{
    if (tstates > 0)
    {
        z80->tstates = 0;
        z80->next_event_tstate = tstates;
        Z80_Execute(z80);
        return z80->tstates;
    }
    return 0;
}


/******************************************************************************/
/* changes z80->tstates if interrupt occurs */
int Z80_Interrupt (Z80Info *z80)
{
    uint16_t a;
    int ots = z80->tstates;
    /***/
    if (z80->prev_was_EIDDR < 0)
    {
        z80->prev_was_EIDDR = 0;    /* Z80 bug */
        z80->af.f &= ~Z80_FLAG_PV;
    }
    if (z80->prev_was_EIDDR || !z80->iff1) return 0; /* not accepted */
    if (z80->halted)
    {
        z80->halted = 0;
        INC_PC;
    }
    z80->iff1 = z80->iff2 = 0; /* disable interrupts */
    /***/
    switch ((z80->im &= 0x03))
    {
    case 3: /* ??? */
        z80->im = 0;
    case 0: /* take instruction from the bus (for now we assume that reading from bus always returns 0xff) */
        /* with a CALL nnnn on the data bus, it takes 19 cycles: */
        /* M1 cycle: 7 T to acknowledge interrupt (where exactly data bus reading occures?) */
        /* M2 cycle: 3 T to read low byte of 'nnnn' from data bus */
        /* M3 cycle: 3 T to read high byte of 'nnnn' and decrement SP */
        /* M4 cycle: 3 T to write high byte of PC to the stack and decrement SP */
        /* M5 cycle: 3 T to write low byte of PC and jump to 'nnnn' */
        z80->tstates += 6;
    case 1: /* just do RST #38 */
        INC_R;
        z80->tstates += 7; /* M1 cycle: 7 T to acknowledge interrupt and decrement SP */
        /* M2 cycle: 3 T states write high byte of PC to the stack and decrement SP */
        /* M3 cycle: 3 T states write the low byte of PC and jump to #0038 */
        Z80_Push6T(z80, z80->pc);
        z80->memptr.w = z80->pc = 0x38;
        break;
    case 2:
        INC_R;
        z80->tstates += 7; /* M1 cycle: 7 T to acknowledge interrupt and decrement SP */
        /* M2 cycle: 3 T states write high byte of PC to the stack and decrement SP */
        /* M3 cycle: 3 T states write the low byte of PC */
        Z80_Push6T(z80, z80->pc);
        /* M4 cycle: 3 T to read high byte from the interrupt vector */
        /* M5 cycle: 3 T to read low byte from bus and jump to interrupt routine */
        a = (((uint16_t)z80->regI) << 8) | 0xff;
        z80->memptr.w = z80->pc = Z80_PeekW6T(z80, a);
        break;
    }
    return z80->tstates - ots; /* accepted */
}


int Z80_NMI (Z80Info *z80)
{
    int ots = z80->tstates;
    /***/
    /* emulate Z80 bug with interrupted LD A,I/R */
    /*if (z80->prev_was_EIDDR < 0) { z80->prev_was_EIDDR = 0; z80->af.f &= ~Z80_FLAG_PV; }*/
    /*if (z80->prev_was_EIDDR) return 0;*/
    z80->prev_was_EIDDR = 0; /* don't care */
    if (z80->halted)
    {
        z80->halted = 0;
        INC_PC;
    }
    INC_R;
    z80->iff1 = 0; /* IFF2 is not changed */
    z80->tstates += 5; /* M1 cycle: 5 T states to do an opcode read and decrement SP */
    /* M2 cycle: 3 T states write high byte of PC to the stack and decrement SP */
    /* M3 cycle: 3 T states write the low byte of PC and jump to #0066 */
    Z80_Push6T(z80, z80->pc);
    z80->memptr.w = z80->pc = 0x66;
    return z80->tstates - ots;
}


/******************************************************************************/
uint16_t Z80_Pop (Z80Info *z80)
{
    uint16_t res = Z80_PeekBI(z80, z80->sp.w);
    /***/
    z80->sp.w = (z80->sp.w + 1) & 0xffff;
    res |= ((uint16_t)Z80_PeekBI(z80, z80->sp.w)) << 8;
    z80->sp.w = (z80->sp.w + 1) & 0xffff;
    return res;
}


void Z80_Push (Z80Info *z80, uint16_t value)
{
    z80->sp.w = (((int32_t)z80->sp.w) - 1) & 0xffff;
    Z80_PokeBI(z80, z80->sp.w, (value >> 8) & 0xff);
    z80->sp.w = (((int32_t)z80->sp.w) - 1) & 0xffff;
    Z80_PokeBI(z80, z80->sp.w, value & 0xff);
}
