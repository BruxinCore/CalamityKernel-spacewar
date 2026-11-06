#include <linux/linkage.h>
#include <asm/assembler.h>

SYM_FUNC_START_WEAK_PI(strrchr)
	mov	x3, #0
	and	w1, w1, #0xff
1:	ldrb	w2, [x0], #1
	cbz	w2, 2f
	cmp	w2, w1
	b.ne	1b
	sub	x3, x0, #1
	b	1b
2:	mov	x0, x3
	ret
SYM_FUNC_END_PI(strrchr)
EXPORT_SYMBOL_NOKASAN(strrchr)
