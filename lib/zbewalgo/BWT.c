// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Benjamin Warnke <4bwarnke@informatik.uni-hamburg.de>
 *
 * The Burrows-Wheeler-Transformation was published by 'M. Burrows' and
 * 'D. J. Wheeler' in 1994. This implementation uses counting sort for
 * sorting the data. Their original paper is online available at:
 * http://www.hpl.hp.com/techreports/Compaq-DEC/SRC-RR-124.pdf
 */

#include "BWT.h"

unsigned long zbewalgo_bwt_max_alphabet = 90;

/*
 * implementation of the Burrows-Wheeler transformation. Optimized for speed
 * and small input sizes
 */
static __always_inline int compress_bwt(const u8 * const source,
					u8 * const dest, u16 * const wrkmem,
					const u16 source_length)
{
	u16 * const C = wrkmem;
	u16 i;
	u16 alphabet;
	u8 * const op = dest + 1;

	*dest = source[source_length - 1];
	memset(C, 0, 512);
	for (i = 0; i < source_length; i++)
		C[source[i]]++;
	alphabet = (C[0] > 0);
	for (i = 1; i < 256; i++) {
		alphabet += (C[i] > 0);
		C[i] += C[i - 1];
	}
	if (alphabet > zbewalgo_bwt_max_alphabet) {
		/* not compressible */
		return -EINVAL;
	}
	i = source_length - 1;
	while (i > 0) {
		C[source[i]]--;
		op[C[source[i]]] = source[i - 1];
		i--;
	}
	C[source[0]]--;
	op[C[source[0]]] = source[source_length - 1];
	return source_length + 1;
}

/*
 * reverses the transformation
 */
static __always_inline int decompress_bwt(const u8 * const source,
					  u8 * const dest, u16 * const wrkmem,
					  const u16 source_length,
					  const int safe_mode)
{
	const u16 dest_length = source_length - 1;
	u16 * const C = wrkmem;
	u8 * const L = (u8 *)(wrkmem + 256);
	u8 key = *source;
	u8 *dest_end = dest + dest_length;
	const u8 *ip = source + 1;
	int i, j;

	if (safe_mode && source_length == 0)
		return -EINVAL;

	memset(C, 0, 512);
	for (i = 0; i < dest_length; i++)
		C[ip[i]]++;
	for (i = 1; i < 256; i++)
		C[i] += C[i - 1];
	j = 0;
	for (i = 0; i < 256; i++)
		while (j < C[i])
			L[j++] = i;
	do {
		C[key]--;
		*--dest_end = L[C[key]];
		key = ip[C[key]];
	} while (dest < dest_end);
	return dest_length;
}

static __always_inline int decompress_bwt_safe(const u8 * const source,
					       u8 * const dest,
					       u16 * const wrkmem,
					       const u16 source_length){
	return decompress_bwt(source, dest, wrkmem, source_length, 1);
}

static __always_inline int decompress_bwt_fast(const u8 * const source,
					       u8 * const dest,
					       u16 * const wrkmem,
					       const u16 source_length){
	return decompress_bwt(source, dest, wrkmem, source_length, 0);
}

static int init_bwt(void)
{
	return 0;
}

static void exit_bwt(void)
{
}

struct zbewalgo_alg alg_bwt = {
	.name = "bwt",
	.flags = ZBEWALGO_ALG_FLAG_TRANSFORM,
	.wrkmem_size = PAGE_SIZE << 1,
	.init = init_bwt,
	.exit = exit_bwt,
	.compress = compress_bwt,
	.decompress_safe = decompress_bwt_safe,
	.decompress_fast = decompress_bwt_fast
};
