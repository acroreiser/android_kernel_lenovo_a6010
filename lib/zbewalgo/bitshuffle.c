// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Benjamin Warnke <4bwarnke@informatik.uni-hamburg.de>
 */

#include "bitshuffle.h"

/*
 * performs a static transformation on the data. Moves every eighth byte into
 * a consecutive range
 */
static __always_inline int compress_bitshuffle(const u8 *const source,
					       u8 *const dest,
					       u16 * const wrkmem,
					       const u16 source_length)
{
	u16 i;
	const u16 source_length2 = source_length & ~0x7;
	u8 *op = dest;

	for (i = 0; i < source_length2; i += 8)
		*op++ = source[i];
	for (i = 1; i < source_length2; i += 8)
		*op++ = source[i];
	for (i = 2; i < source_length2; i += 8)
		*op++ = source[i];
	for (i = 3; i < source_length2; i += 8)
		*op++ = source[i];
	for (i = 4; i < source_length2; i += 8)
		*op++ = source[i];
	for (i = 5; i < source_length2; i += 8)
		*op++ = source[i];
	for (i = 6; i < source_length2; i += 8)
		*op++ = source[i];
	for (i = 7; i < source_length2; i += 8)
		*op++ = source[i];
	memcpy(dest + source_length2, source + source_length2,
	       source_length & 0x7);
	return source_length;
}

/*
 * reverses the transformation step
 */
static __always_inline int decompress_bitshuffle(const u8 *const source,
						 u8 *const dest,
						 u16 * const wrkmem,
						 const u16 source_length)
{
	u16 i;
	const u16 source_length2 = source_length & ~0x7;
	const u8 *ip = source;

	for (i = 0; i < source_length2; i += 8)
		dest[i] = *ip++;
	for (i = 1; i < source_length2; i += 8)
		dest[i] = *ip++;
	for (i = 2; i < source_length2; i += 8)
		dest[i] = *ip++;
	for (i = 3; i < source_length2; i += 8)
		dest[i] = *ip++;
	for (i = 4; i < source_length2; i += 8)
		dest[i] = *ip++;
	for (i = 5; i < source_length2; i += 8)
		dest[i] = *ip++;
	for (i = 6; i < source_length2; i += 8)
		dest[i] = *ip++;
	for (i = 7; i < source_length2; i += 8)
		dest[i] = *ip++;
	memcpy(dest + source_length2, source + source_length2,
	       source_length & 0x7);
	return source_length;
}

static int init_bitshuffle(void)
{
	return 0;
}

static void exit_bitshuffle(void)
{
}

struct zbewalgo_alg alg_bitshuffle = {
	.name = "bitshuffle",
	.flags = ZBEWALGO_ALG_FLAG_TRANSFORM,
	.wrkmem_size = 0,
	.init = init_bitshuffle,
	.exit = exit_bitshuffle,
	.compress = compress_bitshuffle,
	.decompress_safe = decompress_bitshuffle,
	.decompress_fast = decompress_bitshuffle
};
