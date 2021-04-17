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
#ifndef _ZYMOSIS_H_
#define _ZYMOSIS_H_

#define ZYMOSIS_LITTLE_ENDIAN

/* define either ZYMOSIS_LITTLE_ENDIAN or ZYMOSIS_BIG_ENDIAN */

#if !defined(ZYMOSIS_LITTLE_ENDIAN) && !defined(ZYMOSIS_BIG_ENDIAN)
# error wtf?! Zymosis endiannes is not defined!
#endif

#if defined(ZYMOSIS_LITTLE_ENDIAN) && defined(ZYMOSIS_BIG_ENDIAN)
# error wtf?! Zymosis endiannes double defined! are you nuts?
#endif

#if defined(__GNUC__)
# ifndef ZYMOSIS_PACKED
#  define ZYMOSIS_PACKED  __attribute__((packed)) __attribute__((gcc_struct))
# endif
# ifndef ZYMOSIS_INLINE
#  define ZYMOSIS_INLINE  __inline
# endif
#else
# ifndef ZYMOSIS_PACKED
#  define ZYMOSIS_PACKED
# endif
# ifndef ZYMOSIS_INLINE
#  define ZYMOSIS_INLINE
# endif
#endif


#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/* flag masks */
enum {
  Z80_FLAG_C = 0x01,
  Z80_FLAG_N = 0x02,
  Z80_FLAG_PV= 0x04,
  Z80_FLAG_3 = 0x08,
  Z80_FLAG_H = 0x10,
  Z80_FLAG_5 = 0x20,
  Z80_FLAG_Z = 0x40,
  Z80_FLAG_S = 0x80,

  Z80_FLAG_35 = Z80_FLAG_3|Z80_FLAG_5,
  Z80_FLAG_S35 = Z80_FLAG_S|Z80_FLAG_3|Z80_FLAG_5
};


typedef union ZYMOSIS_PACKED {
  uint16_t w;
#ifdef ZYMOSIS_LITTLE_ENDIAN
  struct ZYMOSIS_PACKED { uint8_t c, b; };
  struct ZYMOSIS_PACKED { uint8_t e, d; };
  struct ZYMOSIS_PACKED { uint8_t l, h; };
  struct ZYMOSIS_PACKED { uint8_t f, a; };
  struct ZYMOSIS_PACKED { uint8_t xl, xh; };
  struct ZYMOSIS_PACKED { uint8_t yl, yh; };
#else
  struct ZYMOSIS_PACKED { uint8_t b, c; };
  struct ZYMOSIS_PACKED { uint8_t d, e; };
  struct ZYMOSIS_PACKED { uint8_t h, l; };
  struct ZYMOSIS_PACKED { uint8_t a, f; };
  struct ZYMOSIS_PACKED { uint8_t xh, xl; };
  struct ZYMOSIS_PACKED { uint8_t yh, yl; };
#endif
} Z80WordReg;


typedef enum {
  Z80_MEMIO_OPCODE = 0x00, /* reading opcode */
  Z80_MEMIO_OPCEXT = 0x01, /* 'ext' opcode (after CB/ED/DD/FD prefix) */
  Z80_MEMIO_OPCARG = 0x02, /* opcode argument (jump destination, register value, etc) */
  Z80_MEMIO_DATA   = 0x03, /* reading/writing data */
  Z80_MEMIO_OTHER  = 0x04, /* other 'internal' reads (for memptr, etc; don't do contention, breakpoints or so) */
  Z80_MEMIO_MASK   = 0x0f,
  /* values for memory contention */
  Z80_MREQ_NONE  = 0x00,
  Z80_MREQ_WRITE = 0x10,
  Z80_MREQ_READ  = 0x20,
  Z80_MREQ_MASK  = 0xf0
} Z80MemIOType;


typedef enum {
  Z80_PIO_NORMAL = 0x00,   /* normal call in Z80 execution loop */
  Z80_PIO_INTERNAL = 0x01, /* call from debugger or other place outside of Z80 execution loop */
  /* flags for port contention */
  Z80_PIOFLAG_IN = 0x10,   /* doing 'in' if set */
  Z80_PIOFLAG_EARLY = 0x20 /* 'early' port contetion, if set */
} Z80PIOType;


typedef struct Z80Info Z80Info;

/* will be called when memory contention is necessary */
/* must increase z80->tstates to at least 'tstates' arg */
/* mio: Z80_MEMIO_xxx | Z80_MREQ_xxx */
/* Zymosis will never call this CB for Z80_MEMIO_OTHER memory acces */
typedef void (*Z80ContentionCB) (Z80Info *z80, uint16_t addr, int tstates, Z80MemIOType mio);

/* will be called when port contention is necessary */
/* must increase z80->tstates to at least 'tstates' arg */
/* pio: can contain only Z80_PIOFLAG_xxx flags */
/* `tstates` is always 1 when Z80_PIOFLAG_EARLY is set and 2 otherwise */
typedef void (*Z80PortContentionCB) (Z80Info *z80, uint16_t port, int tstates, Z80PIOType pio);

/* miot: only Z80_MEMIO_xxx, no need in masking */
typedef uint8_t (*Z80MemReadCB) (Z80Info *z80, uint16_t addr, Z80MemIOType miot);
typedef void (*Z80MemWriteCB) (Z80Info *z80, uint16_t addr, uint8_t value, Z80MemIOType miot);

/* pio: only Z80_PIO_xxx, no need in masking */
typedef uint8_t (*Z80PortInCB) (Z80Info *z80, uint16_t port, Z80PIOType pio);
typedef void (*Z80PortOutCB) (Z80Info *z80, uint16_t port, uint8_t value, Z80PIOType pio);

/* return !0 to exit immediately */
typedef int (*Z80EDTrapCB) (Z80Info *z80, uint8_t trapCode);

/* return !0 to break */
typedef int (*Z80CheckBPCB) (Z80Info *z80);

typedef void (*Z80PagerCB) (Z80Info *z80);


struct Z80Info {
  /* registers */
  Z80WordReg bc, de, hl, af, sp, ix, iy;
  /* alternate registers */
  Z80WordReg bcx, dex, hlx, afx;
  Z80WordReg *dd; /* pointer to current HL/IX/IY (inside this struct) for the current command */
  Z80WordReg memptr;
  uint16_t pc; /* program counter */
  uint16_t prev_pc; /* first byte of the previous command */
  uint16_t org_pc; /* first byte of the current command */
  uint8_t regI;
  uint8_t regR;
  int iff1, iff2; /* boolean */
  uint8_t im; /* IM (0-2) */
  int halted; /* boolean; is CPU halted? main progam must manually reset this flag when it's appropriate */
  int32_t tstates; /* t-states passed from previous interrupt (0-...) */
  int32_t next_event_tstate; /* Z80Execute() will exit when tstates>=next_event_tstate */
  int prev_was_EIDDR; /* 1: previous instruction was EI/FD/DD? (they blocks /INT); -1: prev vas LD A,I or LD A,R */
                      /* Zymosis will reset this flag only if it executed at least one instruction */
  int evenM1; /* boolean; emulate 128K/Scorpion M1 contention? */
  Z80MemReadCB memReadFn;
  Z80MemWriteCB memWriteFn;
  Z80ContentionCB contentionFn; /* can be NULL */
  /* port I/O functions should add 4 t-states by themselves */
  Z80PortInCB portInFn; /* in: +3; do read; +1 */
  Z80PortOutCB portOutFn; /* out: +1; do out; +3 */
  Z80PortContentionCB portContentionFn; /* can be NULL */
  /* RETI/RETN traps; called with opcode, *AFTER* iff changed and return address set; return !0 to break execution */
  Z80EDTrapCB retiFn;
  Z80EDTrapCB retnFn;
  /***/
  Z80EDTrapCB trapEDFn; /* can be NULL */
    /* called when invalid ED command found */
    /* PC points to the next instruction */
    /* trapCode=0xFB: */
    /*   .SLT trap */
    /*    HL: address to load; */
    /*    A: A --> level number */
    /*    return: CARRY complemented --> error */
  Z80PagerCB pagerFn; /* can be NULL */
    /* pagerFn is called before fetching opcode to allow, for example, TR-DOS ROM paging in/out */
  Z80CheckBPCB checkBPFn; /* can be NULL */
    /* checkBPFn is called just after pagerFn (before fetching opcode) */
    /* emulator can check various breakpoint conditions there */
    /* and return non-zero to immediately stop executing and return from Z80_Execute[XXX]() */
  /***/
  void *user; /* arbitrary user data */
};


/******************************************************************************/
/* Z80InitTables() should be called before anyting else! */
extern void Z80_InitTables (void); /* this will be automatically called by Z80_Reset() */

extern void Z80_ResetCallbacks (Z80Info *z80);
extern void Z80_Reset (Z80Info *z80);
extern void Z80_Execute (Z80Info *z80);
extern int32_t Z80_ExecuteStep (Z80Info *z80); /* returns number of executed ticks */
extern int Z80_Interrupt (Z80Info *z80); /* !0: interrupt was accepted (returns # of t-states eaten); changes z80->tstates if interrupt occurs */
extern int Z80_NMI (Z80Info *z80); /* !0: interrupt was accepted (returns # of t-states eaten); changes z80->tstates if interrupt occurs */

/* without contention, using Z80_MEMIO_OTHER */
extern uint16_t Z80_Pop (Z80Info *z80);
extern void Z80_Push (Z80Info *z80, uint16_t value);

/* execute at least 'tstates' t-states; return real number of executed t-states */
/* WARNING: this function resets both z80->tstates and z80->next_event_tstate! */
extern int32_t Z80_ExecuteTS (Z80Info *z80, int32_t tstates);


#ifdef __cplusplus
}
#endif
#endif
