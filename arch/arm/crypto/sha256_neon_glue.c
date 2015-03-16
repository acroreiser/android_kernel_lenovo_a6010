/*
 * Glue code for the SHA256 Secure Hash Algorithm assembly implementation
 * using NEON instructions.
 *
 * Copyright © 2015 Google Inc.
 *
 * This file is based on sha512_neon_glue.c:
 *   Copyright © 2014 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <linux/string.h>
#include <crypto/sha.h>
#include <asm/byteorder.h>
#include <asm/simd.h>
#include <asm/neon.h>

asmlinkage void sha256_transform_neon(u32 *digest, const void *data,
				      unsigned int num_blks);


static int sha256_neon_init(struct shash_desc *desc)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA256_H0;
	sctx->state[1] = SHA256_H1;
	sctx->state[2] = SHA256_H2;
	sctx->state[3] = SHA256_H3;
	sctx->state[4] = SHA256_H4;
	sctx->state[5] = SHA256_H5;
	sctx->state[6] = SHA256_H6;
	sctx->state[7] = SHA256_H7;
	sctx->count = 0;

	return 0;
}

static int __sha256_neon_update(struct shash_desc *desc, const u8 *data,
				unsigned int len, unsigned int partial)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	unsigned int done = 0;

	sctx->count += len;

	if (partial) {
		done = SHA256_BLOCK_SIZE - partial;
		memcpy(sctx->buf + partial, data, done);
		sha256_transform_neon(sctx->state, sctx->buf, 1);
	}

	if (len - done >= SHA256_BLOCK_SIZE) {
		const unsigned int rounds = (len - done) / SHA256_BLOCK_SIZE;

		sha256_transform_neon(sctx->state, data + done, rounds);
		done += rounds * SHA256_BLOCK_SIZE;
	}

	memcpy(sctx->buf, data + done, len - done);

	return 0;
}

static int sha256_neon_update(struct shash_desc *desc, const u8 *data,
			     unsigned int len)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	unsigned int partial = sctx->count % SHA256_BLOCK_SIZE;
	int res;

	/* Handle the fast case right here */
	if (partial + len < SHA256_BLOCK_SIZE) {
		sctx->count += len;
		memcpy(sctx->buf + partial, data, len);

		return 0;
	}

	if (!may_use_simd()) {
		res = crypto_sha256_update(desc, data, len);
	} else {
		kernel_neon_begin();
		res = __sha256_neon_update(desc, data, len, partial);
		kernel_neon_end();
	}

	return res;
}

/* Add padding and return the message digest. */
static int sha256_neon_final(struct shash_desc *desc, u8 *out)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	unsigned int i, index, padlen;
	__be32 *dst = (__be32 *)out;
	__be64 bits;
	static const u8 padding[SHA256_BLOCK_SIZE] = { 0x80, };

	/* save number of bits */
	bits = cpu_to_be64(sctx->count << 3);

	/* Pad out to 56 mod 64 and append length */
	index = sctx->count % SHA256_BLOCK_SIZE;
	padlen = (index < 56) ? (56 - index) : ((SHA256_BLOCK_SIZE+56)-index);

	if (!may_use_simd()) {
		crypto_sha256_update(desc, padding, padlen);
		crypto_sha256_update(desc, (const u8 *)&bits, sizeof(bits));
	} else {
		kernel_neon_begin();
		/* We need to fill a whole block for __sha256_neon_update() */
		if (padlen <= 56) {
			sctx->count += padlen;
			memcpy(sctx->buf + index, padding, padlen);
		} else {
			__sha256_neon_update(desc, padding, padlen, index);
		}
		__sha256_neon_update(desc, (const u8 *)&bits,
					sizeof(bits), 56);
		kernel_neon_end();
	}

	/* Store state in digest */
	for (i = 0; i < 8; i++)
		dst[i] = cpu_to_be32(sctx->state[i]);

	/* Wipe context */
	memset(sctx, 0, sizeof(*sctx));

	return 0;
}

static int sha256_neon_export(struct shash_desc *desc, void *out)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	memcpy(out, sctx, sizeof(*sctx));

	return 0;
}

static int sha256_neon_import(struct shash_desc *desc, const void *in)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	memcpy(sctx, in, sizeof(*sctx));

	return 0;
}

static struct shash_alg alg = {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_neon_init,
	.update		=	sha256_neon_update,
	.final		=	sha256_neon_final,
	.export		=	sha256_neon_export,
	.import		=	sha256_neon_import,
	.descsize	=	sizeof(struct sha256_state),
	.statesize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name =	"sha256-neon",
		.cra_priority	=	350,
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int __init sha256_neon_mod_init(void)
{
	if (!cpu_has_neon())
		return -ENODEV;

	return crypto_register_shash(&alg);
}

static void __exit sha256_neon_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(sha256_neon_mod_init);
module_exit(sha256_neon_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA256 Secure Hash Algorithm, NEON accelerated");

MODULE_ALIAS("sha256");
