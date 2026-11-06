#include <linux/linkage.h>
#include <asm/assembler.h>

#define L(label) .L ## label

#define REP8_01 0x0101010101010101
#define REP8_7f 0x7f7f7f7f7f7f7f7f
#define REP8_80 0x8080808080808080

#define src1		x0
#define src2		x1
#define result		x0

#define data1		x2
#define data1w		w2
#define data2		x3
#define data2w		w3
#define has_nul		x4
#define diff		x5
#define syndrome	x6
#define tmp1		x7
#define tmp2		x8
#define tmp3		x9
#define zeroones	x10
#define pos		x11

	.align 6
SYM_FUNC_START_WEAK_PI(strcmp)
	eor	tmp1, src1, src2
	mov	zeroones, #REP8_01
	tst	tmp1, #7
	b.ne	L(misaligned8)
	ands	tmp1, src1, #7
	b.ne	L(mutual_align)

L(loop_aligned):
	ldr	data1, [src1], #8
	ldr	data2, [src2], #8
L(start_realigned):
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, #REP8_7f
	eor	diff, data1, data2
	bic	has_nul, tmp1, tmp2
	orr	syndrome, diff, has_nul
	cbz	syndrome, L(loop_aligned)

L(end):
#ifndef	__AARCH64EB__
	rev	syndrome, syndrome
	rev	data1, data1

	clz	pos, syndrome
	rev	data2, data2
	lsl	data1, data1, pos
	lsl	data2, data2, pos

	lsr	data1, data1, #56
	sub	result, data1, data2, lsr #56
	ret
#else

	cbnz	has_nul, 1f
	cmp	data1, data2
	cset	result, ne
	cneg	result, result, lo
	ret
1:

	rev	tmp3, data1
	sub	tmp1, tmp3, zeroones
	orr	tmp2, tmp3, #REP8_7f
	bic	has_nul, tmp1, tmp2
	rev	has_nul, has_nul
	orr	syndrome, diff, has_nul
	clz	pos, syndrome

	lsl	data1, data1, pos
	lsl	data2, data2, pos

	lsr	data1, data1, #56
	sub	result, data1, data2, lsr #56
	ret
#endif

L(mutual_align):

	bic	src1, src1, #7
	bic	src2, src2, #7
	lsl	tmp1, tmp1, #3
	ldr	data1, [src1], #8
	neg	tmp1, tmp1
	ldr	data2, [src2], #8
	mov	tmp2, #~0
#ifdef __AARCH64EB__

	lsl	tmp2, tmp2, tmp1
#else

	lsr	tmp2, tmp2, tmp1
#endif
	orr	data1, data1, tmp2
	orr	data2, data2, tmp2
	b	L(start_realigned)

L(misaligned8):

	tst	src1, #7
	b.eq	L(loop_misaligned)
L(do_misaligned):
	ldrb	data1w, [src1], #1
	ldrb	data2w, [src2], #1
	cmp	data1w, #1
	ccmp	data1w, data2w, #0, cs
	b.ne	L(done)
	tst	src1, #7
	b.ne	L(do_misaligned)

L(loop_misaligned):

	and	tmp1, src2, #0xff8
	eor	tmp1, tmp1, #0xff8
	cbz	tmp1, L(do_misaligned)
	ldr	data1, [src1], #8
	ldr	data2, [src2], #8

	sub	tmp1, data1, zeroones
	orr	tmp2, data1, #REP8_7f
	eor	diff, data1, data2
	bic	has_nul, tmp1, tmp2
	orr	syndrome, diff, has_nul
	cbz	syndrome, L(loop_misaligned)
	b	L(end)

L(done):
	sub	result, data1, data2
	ret

SYM_FUNC_END_PI(strcmp)
EXPORT_SYMBOL_NOKASAN(strcmp)
