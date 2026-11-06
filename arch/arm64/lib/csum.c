#include <linux/compiler.h>
#include <linux/kasan-checks.h>
#include <linux/kernel.h>

#include <net/checksum.h>

static u64 accumulate(u64 sum, u64 data)
{
	__uint128_t tmp = (__uint128_t)sum + data;
	return tmp + (tmp >> 64);
}

unsigned int __no_sanitize_address do_csum(const unsigned char *buff, int len)
{
	unsigned int offset, shift, sum;
	const u64 *ptr;
	u64 data, sum64 = 0;

	if (unlikely(len <= 0))
		return 0;

	offset = (unsigned long)buff & 7;

	kasan_check_read(buff, len);
	ptr = (u64 *)(buff - offset);
	len = len + offset - 8;

	shift = offset * 8;
	data = *ptr++;
#ifdef __LITTLE_ENDIAN
	data = (data >> shift) << shift;
#else
	data = (data << shift) >> shift;
#endif

	while (unlikely(len > 64)) {
		__uint128_t tmp1, tmp2, tmp3, tmp4;

		tmp1 = *(__uint128_t *)ptr;
		tmp2 = *(__uint128_t *)(ptr + 2);
		tmp3 = *(__uint128_t *)(ptr + 4);
		tmp4 = *(__uint128_t *)(ptr + 6);

		len -= 64;
		ptr += 8;

		tmp1 += (tmp1 >> 64) | (tmp1 << 64);
		tmp2 += (tmp2 >> 64) | (tmp2 << 64);
		tmp3 += (tmp3 >> 64) | (tmp3 << 64);
		tmp4 += (tmp4 >> 64) | (tmp4 << 64);
		tmp1 = ((tmp1 >> 64) << 64) | (tmp2 >> 64);
		tmp1 += (tmp1 >> 64) | (tmp1 << 64);
		tmp3 = ((tmp3 >> 64) << 64) | (tmp4 >> 64);
		tmp3 += (tmp3 >> 64) | (tmp3 << 64);
		tmp1 = ((tmp1 >> 64) << 64) | (tmp3 >> 64);
		tmp1 += (tmp1 >> 64) | (tmp1 << 64);
		tmp1 = ((tmp1 >> 64) << 64) | sum64;
		tmp1 += (tmp1 >> 64) | (tmp1 << 64);
		sum64 = tmp1 >> 64;
	}
	while (len > 8) {
		__uint128_t tmp;

		sum64 = accumulate(sum64, data);
		tmp = *(__uint128_t *)ptr;

		len -= 16;
		ptr += 2;

#ifdef __LITTLE_ENDIAN
		data = tmp >> 64;
		sum64 = accumulate(sum64, tmp);
#else
		data = tmp;
		sum64 = accumulate(sum64, tmp >> 64);
#endif
	}
	if (len > 0) {
		sum64 = accumulate(sum64, data);
		data = *ptr;
		len -= 8;
	}

	shift = len * -8;
#ifdef __LITTLE_ENDIAN
	data = (data << shift) >> shift;
#else
	data = (data >> shift) << shift;
#endif
	sum64 = accumulate(sum64, data);

	sum64 += (sum64 >> 32) | (sum64 << 32);
	sum = sum64 >> 32;
	sum += (sum >> 16) | (sum << 16);
	if (offset & 1)
		return (u16)swab32(sum);

	return sum >> 16;
}

__sum16 csum_ipv6_magic(const struct in6_addr *saddr,
			const struct in6_addr *daddr,
			__u32 len, __u8 proto, __wsum csum)
{
	__uint128_t src, dst;
	u64 sum = (__force u64)csum;

	src = *(const __uint128_t *)saddr->s6_addr;
	dst = *(const __uint128_t *)daddr->s6_addr;

	sum += (__force u32)htonl(len);
#ifdef __LITTLE_ENDIAN
	sum += (u32)proto << 24;
#else
	sum += proto;
#endif
	src += (src >> 64) | (src << 64);
	dst += (dst >> 64) | (dst << 64);

	sum = accumulate(sum, src >> 64);
	sum = accumulate(sum, dst >> 64);

	sum += ((sum >> 32) | (sum << 32));
	return csum_fold((__force __wsum)(sum >> 32));
}
EXPORT_SYMBOL(csum_ipv6_magic);
