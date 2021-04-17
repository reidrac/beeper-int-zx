#ifndef _SFX_H
#define _SFX_H

enum sfx_enum {
	// laser
	SFX1 = 1,
	// zap
	SFX2,
	// drill
	SFX3,
	// explo
	SFX4,
};

const struct beeper_sfx sfx_table[] = {
	{ 1, 32, 120, 252, 0 },
	{ 2, 16, 12, 0, 0 },
	{ 1, 32, 1, 0, 0 },
	{ 2, 32, 128, 255, 0 },
};

#endif /* _SFX_H */
