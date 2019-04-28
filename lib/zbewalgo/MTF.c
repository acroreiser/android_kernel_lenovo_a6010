// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Benjamin Warnke <4bwarnke@informatik.uni-hamburg.de>
 *
 * The Move-To-Front algorithm as described by 'M. Burrows' and
 * 'D. J. Wheeler' in the same paper as bwt.
 * Their original paper is online available at:
 * http://www.hpl.hp.com/techreports/Compaq-DEC/SRC-RR-124.pdf
 */

#include "MTF.h"

static u8 initial_data[256];

static __always_inline int compress_mtf(const u8 * const source,
					u8 * const dest, u16 * const wrkmem,
					const u16 source_length)
{
	u8 * const wrk = (u8 *)wrkmem;
	const u8 *source_end = source + source_length;
	const u8 *ip = source;
	u8 *op = dest;
	u16 i;
	u8 tmp;
	u64 tmp64;
	u32 tmp32;
	u16 tmp16;

	memcpy(wrk, &initial_data[0], 256);
	do {
		i = 0;
		while (wrk[i] != *ip)
			i++;
		ip++;
		*op++ = i;
		tmp = wrk[i];
		while (i >= 8) {
			tmp64 = get_unaligned_le64(&wrk[i - 8]);
			put_unaligned_le64(tmp64, &wrk[i - 7]);
			i -= 8;
		}
		if (i & 0x4) {
			tmp32 = get_unaligned_le32(&wrk[i - 4]);
			put_unaligned_le32(tmp32, &wrk[i - 3]);
			i -= 4;
		}
		if (i & 0x2) {
			tmp16 = get_unaligned_le16(&wrk[i - 2]);
			put_unaligned_le16(tmp16, &wrk[i - 1]);
			i -= 2;
		}
		if (i & 0x1)
			wrk[1] = wrk[0];
		wrk[0] = tmp;
	} while (ip < source_end);
	return source_length;
}

static __always_inline int decompress_mtf(const u8 * const source,
					  u8 * const dest, u16 * const wrkmem,
					  const u16 source_length)
{
	u8 * const wrk = (u8 *)wrkmem;
	u8 *dest_end = dest + source_length;
	u16 i;
	u8 tmp;
	const u8 *ip = source;
	u8 *op = dest;
	u64 tmp64;
	u32 tmp32;
	u16 tmp16;

	memcpy(wrk, &initial_data[0], 256);
	do {
		i = *ip++;
		*op++ = wrk[i];
		tmp = wrk[i];
		while (i >= 8) {
			tmp64 = get_unaligned_le64(&wrk[i - 8]);
			put_unaligned_le64(tmp64, &wrk[i - 7]);
			i -= 8;
		}
		if (i & 0x4) {
			tmp32 = get_unaligned_le32(&wrk[i - 4]);
			put_unaligned_le32(tmp32, &wrk[i - 3]);
			i -= 4;
		}
		if (i & 0x2) {
			tmp16 = get_unaligned_le16(&wrk[i - 2]);
			put_unaligned_le16(tmp16, &wrk[i - 1]);
			i -= 2;
		}
		if (i & 0x1)
			wrk[1] = wrk[0];
		wrk[0] = tmp;
	} while (op < dest_end);
	return source_length;
}

static int init_mtf(void)
{
	int i;

	for (i = 0; i < 256; i++)
		initial_data[i] = i;
	return 0;
}

static void exit_mtf(void)
{
}

struct zbewalgo_alg alg_mtf = {
	.name = "mtf",
	.flags = ZBEWALGO_ALG_FLAG_TRANSFORM,
	.wrkmem_size = 256,
	.init = init_mtf,
	.exit = exit_mtf,
	.compress = compress_mtf,
	.decompress_safe = decompress_mtf,
	.decompress_fast = decompress_mtf
};
