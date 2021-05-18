# sfx2h.py

This tool requires Python 3 and can generate a C include file from a sfx file
as saved by `sfxed`.

Being Python is easier to modify and adapt to your own needs!

Example:
```
sfx2h.py ../test.sfx sounds
```

Will generate:
```

/* file: ../example/test.sfx */
#ifndef SOUNDS_H_
#define SOUNDS_H_

#include "beeper.h"

enum sounds_enum {
	SOUNDS_LASER = 1,
	SOUNDS_ZAP,
	SOUNDS_DRILL,
	SOUNDS_EXPLO,
};

#ifdef LOCAL

const struct beeper_sfx sounds[] = {
{ 0x01, 0x20, 0x78, 0xfc, 0x00 },
{ 0x02, 0x10, 0x0c, 0x00, 0x00 },
{ 0x01, 0x20, 0x01, 0x00, 0x00 },
{ 0x02, 0x20, 0x80, 0xff, 0x00 },
};

#else
extern const struct beeper_sfx sounds[];
#endif
#endif // SOUNDS_H_
```

You can include it normally on any C file, just remember to define `LOCAL`
before included in the module containing the data (only once!).

## CAVEATS

* Is not perfuming exhaustive checks on the effects' names or the provided id
* `LOCAL` is not undefined after used

