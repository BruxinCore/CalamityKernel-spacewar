#include <linux/linkage.h>
#include <asm/assembler.h>

SYM_FUNC_START_WEAK(strchr)
	and	w1, w1, #0xff
1:	ldrb	w2, [x0], #1
	cmp	w2, w1
	ccmp	w2, wzr, #4, ne
	b.ne	1b
	sub	x0, x0, #1
	cmp	w2, w1
	csel	x0, x0, xzr, eq
	ret
SYM_FUNC_END(strchr)
EXPORT_SYMBOL_NOKASAN(strchr)
