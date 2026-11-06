#include <linux/crc32.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/alternative.h>
#include <asm/cpufeature.h>
#include <asm/neon.h>
#include <asm/simd.h>

#define CRC32_PMULL_MIN_LEN	1024

u32 __pure crc32_le_base(u32 crc, unsigned char const *p, size_t len);
u32 __pure __crc32c_le_base(u32 crc, unsigned char const *p, size_t len);
u32 __pure crc32_be_base(u32 crc, unsigned char const *p, size_t len);

asmlinkage u32 crc32_le_arm64(u32 crc, unsigned char const *p, size_t len);
asmlinkage u32 crc32c_le_arm64(u32 crc, unsigned char const *p, size_t len);
asmlinkage u32 crc32_be_arm64(u32 crc, unsigned char const *p, size_t len);

asmlinkage u32 crc32_le_arm64_4way(u32 crc, unsigned char const *p, size_t len);
asmlinkage u32 crc32c_le_arm64_4way(u32 crc, unsigned char const *p, size_t len);
asmlinkage u32 crc32_be_arm64_4way(u32 crc, unsigned char const *p, size_t len);

u32 __pure crc32_le(u32 crc, unsigned char const *p, size_t len)
{
	if (!cpus_have_const_cap(ARM64_HAS_CRC32))
		return crc32_le_base(crc, p, len);

	if (len >= CRC32_PMULL_MIN_LEN && cpu_have_named_feature(PMULL) &&
	    likely(may_use_simd())) {
		kernel_neon_begin();
		crc = crc32_le_arm64_4way(crc, p, len);
		kernel_neon_end();

		p += round_down(len, 64);
		len %= 64;
		if (!len)
			return crc;
	}

	return crc32_le_arm64(crc, p, len);
}

u32 __pure __crc32c_le(u32 crc, unsigned char const *p, size_t len)
{
	if (!cpus_have_const_cap(ARM64_HAS_CRC32))
		return __crc32c_le_base(crc, p, len);

	if (len >= CRC32_PMULL_MIN_LEN && cpu_have_named_feature(PMULL) &&
	    likely(may_use_simd())) {
		kernel_neon_begin();
		crc = crc32c_le_arm64_4way(crc, p, len);
		kernel_neon_end();

		p += round_down(len, 64);
		len %= 64;
		if (!len)
			return crc;
	}

	return crc32c_le_arm64(crc, p, len);
}

u32 __pure crc32_be(u32 crc, unsigned char const *p, size_t len)
{
	if (!cpus_have_const_cap(ARM64_HAS_CRC32))
		return crc32_be_base(crc, p, len);

	if (len >= CRC32_PMULL_MIN_LEN && cpu_have_named_feature(PMULL) &&
	    likely(may_use_simd())) {
		kernel_neon_begin();
		crc = crc32_be_arm64_4way(crc, p, len);
		kernel_neon_end();

		p += round_down(len, 64);
		len %= 64;
		if (!len)
			return crc;
	}

	return crc32_be_arm64(crc, p, len);
}
