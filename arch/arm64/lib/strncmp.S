#include <linux/linkage.h>
#include <asm/assembler.h>

#define L(label) .L ## label

#define REP8_01 0x0101010101010101
#define REP8_7f 0x7f7f7f7f7f7f7f7f

#define src1		x0
#define src2		x1
#define limit		x2
#define result		x0

#define data1		x3
#define data1w		w3
#define data2		x4
#define data2w		w4
#define has_nul		x5
#define diff		x6
#define syndrome	x7
#define tmp1		x8
#define tmp2		x9
#define tmp3		x10
#define zeroones	x11
#define pos		x12
#define mask		x13
#define endloop		x14
#define count		mask
#define offset		pos
#define neg_offset	x15

#ifdef __AARCH64EB__
#define LS_FW lsl
#define LS_BK lsr
#else
#define LS_FW lsr
#define LS_BK lsl
#endif

SYM_FUNC_START_WEAK_PI(strncmp)
	cbz	limit, L(ret0)
	eor	tmp1, src1, src2
	mov	zeroones, #REP8_01
	tst	tmp1, #7
	and	count, src1, #7
	b.ne	L(misaligned8)
	cbnz	count, L(mutual_align)

	.p2align 4
L(loop_aligned):
	ldr	data1, [src1], #8
	ldr	data2, [src2], #8
L(start_realigned):
	subs	limit, limit, #8
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, #REP8_7f
	eor	diff, data1, data2
	csinv	endloop, diff, xzr, hi
	bics	has_nul, tmp1, tmp2
	ccmp	endloop, #0, #0, eq
	b.eq	L(loop_aligned)

L(full_check):
#ifndef __AARCH64EB__
	orr	syndrome, diff, has_nul
	add	limit, limit, 8
L(syndrome_check):

	rev	syndrome, syndrome
	rev	data1, data1
	clz	pos, syndrome
	rev	data2, data2
	lsl	data1, data1, pos
	cmp	limit, pos, lsr #3
	lsl	data2, data2, pos

	lsr	data1, data1, #56
	sub	result, data1, data2, lsr #56
	csel result, result, xzr, hi
	ret
#else

	tbz	limit, #63, L(not_limit)
	add	tmp1, limit, 8
	cbz	limit, L(not_limit)

	lsl	limit, tmp1, #3
	mov	mask, #~0
	lsr	mask, mask, limit
	bic	data1, data1, mask
	bic	data2, data2, mask

	orr	has_nul, has_nul, mask

L(not_limit):

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

L(end_quick):
	lsl	data1, data1, pos
	lsl	data2, data2, pos

	lsr	data1, data1, #56
	sub	result, data1, data2, lsr #56
	ret
#endif

L(mutual_align):

	bic	src1, src1, #7
	bic	src2, src2, #7
	ldr	data1, [src1], #8
	neg	tmp3, count, lsl #3
	ldr	data2, [src2], #8
	mov	tmp2, #~0
	LS_FW	tmp2, tmp2, tmp3

	adds	limit, limit, count
	csinv	limit, limit, xzr, lo
	orr	data1, data1, tmp2
	orr	data2, data2, tmp2
	b	L(start_realigned)

	.p2align 4

L(misaligned8):
	cmp	limit, #16
	b.hs	L(try_misaligned_words)

L(byte_loop):

	ldrb	data1w, [src1], #1
	ldrb	data2w, [src2], #1
	subs	limit, limit, #1
	ccmp	data1w, #1, #0, hi
	ccmp	data1w, data2w, #0, cs
	b.eq	L(byte_loop)
L(done):
	sub	result, data1, data2
	ret

L(try_misaligned_words):
	cbz	count, L(src1_aligned)

	neg	count, count
	and	count, count, #7
	sub	limit, limit, count

L(page_end_loop):
	ldrb	data1w, [src1], #1
	ldrb	data2w, [src2], #1
	cmp	data1w, #1
	ccmp	data1w, data2w, #0, cs
	b.ne	L(done)
	subs	count, count, #1
	b.hi	L(page_end_loop)

L(src1_aligned):

	lsl	offset, src2, #3
	bic	src2, src2, #0xf
	mov	mask, -1
	neg	neg_offset, offset
	ldr	data1, [src1], #8
	ldp	tmp1, tmp2, [src2], #16
	LS_BK	mask, mask, neg_offset
	and	neg_offset, neg_offset, #63

	tbnz	offset, 6, L(misaligned_mid_loop)

L(loop_misaligned):

	LS_FW	data2, tmp1, offset
	LS_BK	tmp1, tmp2, neg_offset
	subs	limit, limit, #8
	orr	data2, data2, tmp1
	sub	has_nul, data1, zeroones
	eor	diff, data1, data2
	orr	tmp3, data1, #REP8_7f
	csinv	endloop, diff, xzr, hi
	bic	has_nul, has_nul, tmp3
	orr	tmp3, endloop, has_nul
	cbnz	tmp3, L(full_check)

	ldr	data1, [src1], #8
L(misaligned_mid_loop):

	LS_FW	data2, tmp2, offset
#ifdef __AARCH64EB__

	rev	tmp3, data1
	#define data1_fixed tmp3
#else
	#define data1_fixed data1
#endif
	sub	has_nul, data1_fixed, zeroones
	orr	tmp3, data1_fixed, #REP8_7f
	eor	diff, data2, data1
	bic	has_nul, has_nul, tmp3
#ifdef __AARCH64EB__
	rev	has_nul, has_nul
#endif
	cmp	limit, neg_offset, lsr #3
	orr	syndrome, diff, has_nul
	bic	syndrome, syndrome, mask
	csinv	tmp3, syndrome, xzr, hi
	cbnz	tmp3, L(syndrome_check)

	ldp	tmp1, tmp2, [src2], #16
	cmp	limit, #8
	LS_BK	data2, tmp1, neg_offset
	eor	diff, data2, data1
	orr	syndrome, diff, has_nul
	and	syndrome, syndrome, mask
	csinv	tmp3, syndrome, xzr, hi
	cbnz	tmp3, L(syndrome_check)

	ldr	data1, [src1], #8
	sub	limit, limit, #8
	b	L(loop_misaligned)

#ifdef	__AARCH64EB__
L(syndrome_check):
	clz	pos, syndrome
	cmp	pos, limit, lsl #3
	b.lo	L(end_quick)
#endif

L(ret0):
	mov	result, #0
	ret
SYM_FUNC_END_PI(strncmp)
EXPORT_SYMBOL_NOKASAN(strncmp)
