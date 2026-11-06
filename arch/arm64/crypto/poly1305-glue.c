#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>
#include <asm/unaligned.h>
#include <crypto/algapi.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/poly1305.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sizes.h>

asmlinkage void caly_poly1305_init_arm64(void *state, const u8 *key);
asmlinkage void caly_poly1305_blocks_arm64(void *state, const u8 *src, u32 len, u32 hibit);
asmlinkage void caly_poly1305_blocks_neon_arm64(void *state, const u8 *src, u32 len, u32 hibit);
asmlinkage void caly_poly1305_emit_arm64(void *state, u8 *digest, const u32 *nonce);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);

struct neon_poly1305_desc_ctx {
	u8 buf[POLY1305_BLOCK_SIZE];
	unsigned int buflen;
	unsigned short rset;
	bool sset;
	u32 s[4];
	u64 state[32] __aligned(8);
};

static int neon_poly1305_init(struct shash_desc *desc)
{
	struct neon_poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	dctx->buflen = 0;
	dctx->rset = 0;
	dctx->sset = false;

	return 0;
}

static void neon_caly_poly1305_blocks_arm64(struct neon_poly1305_desc_ctx *dctx,
				 const u8 *src, u32 len, u32 hibit,
				 bool do_neon)
{
	if (unlikely(!dctx->sset)) {
		if (!dctx->rset) {
			caly_poly1305_init_arm64(dctx->state, src);
			src += POLY1305_BLOCK_SIZE;
			len -= POLY1305_BLOCK_SIZE;
			dctx->rset = 1;
		}
		if (len >= POLY1305_BLOCK_SIZE) {
			dctx->s[0] = get_unaligned_le32(src + 0);
			dctx->s[1] = get_unaligned_le32(src + 4);
			dctx->s[2] = get_unaligned_le32(src + 8);
			dctx->s[3] = get_unaligned_le32(src + 12);
			src += POLY1305_BLOCK_SIZE;
			len -= POLY1305_BLOCK_SIZE;
			dctx->sset = true;
		}
		if (len < POLY1305_BLOCK_SIZE)
			return;
	}

	len &= ~(POLY1305_BLOCK_SIZE - 1);

	if (static_branch_likely(&have_neon) && likely(do_neon))
		caly_poly1305_blocks_neon_arm64(dctx->state, src, len, hibit);
	else
		caly_poly1305_blocks_arm64(dctx->state, src, len, hibit);
}

static void neon_poly1305_do_update(struct neon_poly1305_desc_ctx *dctx,
				    const u8 *src, u32 len, bool do_neon)
{
	if (unlikely(dctx->buflen)) {
		u32 bytes = min(len, POLY1305_BLOCK_SIZE - dctx->buflen);

		memcpy(dctx->buf + dctx->buflen, src, bytes);
		src += bytes;
		len -= bytes;
		dctx->buflen += bytes;

		if (dctx->buflen == POLY1305_BLOCK_SIZE) {
			neon_caly_poly1305_blocks_arm64(dctx, dctx->buf,
					     POLY1305_BLOCK_SIZE, 1, false);
			dctx->buflen = 0;
		}
	}

	if (likely(len >= POLY1305_BLOCK_SIZE)) {
		neon_caly_poly1305_blocks_arm64(dctx, src, len, 1, do_neon);
		src += round_down(len, POLY1305_BLOCK_SIZE);
		len %= POLY1305_BLOCK_SIZE;
	}

	if (unlikely(len)) {
		dctx->buflen = len;
		memcpy(dctx->buf, src, len);
	}
}

static int neon_poly1305_update(struct shash_desc *desc,
				const u8 *src, unsigned int srclen)
{
	bool do_neon = crypto_simd_usable() && srclen > 128;
	struct neon_poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	if (static_branch_likely(&have_neon) && do_neon) {
		do {
			unsigned int todo = min_t(unsigned int, srclen, SZ_4K);

			kernel_neon_begin();
			neon_poly1305_do_update(dctx, src, todo, true);
			kernel_neon_end();

			src += todo;
			srclen -= todo;
		} while (srclen);
	} else {
		neon_poly1305_do_update(dctx, src, srclen, false);
	}
	return 0;
}

static int neon_poly1305_final(struct shash_desc *desc, u8 *dst)
{
	struct neon_poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	if (unlikely(!dctx->sset))
		return -ENOKEY;

	if (unlikely(dctx->buflen)) {
		dctx->buf[dctx->buflen++] = 1;
		memset(dctx->buf + dctx->buflen, 0,
		       POLY1305_BLOCK_SIZE - dctx->buflen);
		caly_poly1305_blocks_arm64(dctx->state, dctx->buf, POLY1305_BLOCK_SIZE, 0);
	}

	caly_poly1305_emit_arm64(dctx->state, dst, dctx->s);
	memzero_explicit(dctx, sizeof(*dctx));
	return 0;
}

static struct shash_alg neon_poly1305_alg = {
	.init			= neon_poly1305_init,
	.update			= neon_poly1305_update,
	.final			= neon_poly1305_final,
	.digestsize		= POLY1305_DIGEST_SIZE,
	.descsize		= sizeof(struct neon_poly1305_desc_ctx),
	.base.cra_name		= "poly1305",
	.base.cra_driver_name	= "poly1305-neon",
	.base.cra_priority	= 200,
	.base.cra_blocksize	= POLY1305_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
};

static int __init neon_poly1305_mod_init(void)
{
	if (!cpu_have_named_feature(ASIMD))
		return 0;

	static_branch_enable(&have_neon);

	return IS_REACHABLE(CONFIG_CRYPTO_HASH) ?
		crypto_register_shash(&neon_poly1305_alg) : 0;
}

static void __exit neon_poly1305_mod_exit(void)
{
	if (IS_REACHABLE(CONFIG_CRYPTO_HASH) && cpu_have_named_feature(ASIMD))
		crypto_unregister_shash(&neon_poly1305_alg);
}

module_init(neon_poly1305_mod_init);
module_exit(neon_poly1305_mod_exit);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("poly1305");
MODULE_ALIAS_CRYPTO("poly1305-neon");
