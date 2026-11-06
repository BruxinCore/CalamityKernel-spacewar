#include <linux/linkage.h>
#include <asm/assembler.h>

srcin		.req	x0
len		.req	x0
limit		.req	x1

src		.req	x2
data1		.req	x3
data2		.req	x4
data2a		.req	x5
has_nul1	.req	x6
has_nul2	.req	x7
tmp1		.req	x8
tmp2		.req	x9
tmp3		.req	x10
tmp4		.req	x11
zeroones	.req	x12
pos		.req	x13
limit_wd	.req	x14

#define REP8_01 0x0101010101010101
#define REP8_7f 0x7f7f7f7f7f7f7f7f
#define REP8_80 0x8080808080808080

SYM_FUNC_START_WEAK_PI(strnlen)
	cbz	limit, .Lhit_limit
	mov	zeroones, #REP8_01
	bic	src, srcin, #15
	ands	tmp1, srcin, #15
	b.ne	.Lmisaligned

	sub	limit_wd, limit, #1
	lsr	limit_wd, limit_wd, #4

.Lloop:
	ldp	data1, data2, [src], #16
.Lrealigned:
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, #REP8_7f
	sub	tmp3, data2, zeroones
	orr	tmp4, data2, #REP8_7f
	bic	has_nul1, tmp1, tmp2
	bic	has_nul2, tmp3, tmp4
	subs	limit_wd, limit_wd, #1
	orr	tmp1, has_nul1, has_nul2
	ccmp	tmp1, #0, #0, pl
	b.eq	.Lloop

	cbz	tmp1, .Lhit_limit

	sub	len, src, srcin
	cbz	has_nul1, .Lnul_in_data2
CPU_BE( mov	data2, data1 )

	sub	len, len, #8
	mov	has_nul2, has_nul1
.Lnul_in_data2:

CPU_BE( rev	data2, data2 )
CPU_BE( sub	tmp1, data2, zeroones )
CPU_BE( orr	tmp2, data2, #REP8_7f )
CPU_BE( bic	has_nul2, tmp1, tmp2 )

	sub	len, len, #8
	rev	has_nul2, has_nul2
	clz	pos, has_nul2
	add	len, len, pos, lsr #3
	cmp	len, limit
	csel	len, len, limit, ls
	ret

.Lmisaligned:

	ldp	data1, data2, [src], #16

	sub	limit_wd, limit, #1
	and	tmp3, limit_wd, #15
	lsr	limit_wd, limit_wd, #4

	add	tmp3, tmp3, tmp1
	add	limit_wd, limit_wd, tmp3, lsr #4

	neg	tmp4, tmp1
	lsl	tmp4, tmp4, #3

	mov	tmp2, #~0

CPU_BE( lsl	tmp2, tmp2, tmp4 )

CPU_LE( lsr	tmp2, tmp2, tmp4 )

	cmp	tmp1, #8

	orr	data1, data1, tmp2
	orr	data2a, data2, tmp2

	csinv	data1, data1, xzr, le
	csel	data2, data2, data2a, le
	b	.Lrealigned

.Lhit_limit:
	mov	len, limit
	ret
SYM_FUNC_END_PI(strnlen)
EXPORT_SYMBOL_NOKASAN(strnlen)
