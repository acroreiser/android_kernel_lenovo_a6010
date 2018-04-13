// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Benjamin Warnke <4bwarnke@informatik.uni-hamburg.de>
 */

#include "RLE.h"

#define RLE_REPEAT 0x80
#define RLE_SIMPLE 0x00
#define RLE_MAX_LENGTH ((1 << 7) - 1)

/*
 * Run Length Encoder
 */
static __always_inline int compress_rle(const u8 * const source,
					u8 * const dest, u16 * const wrkmem,
					const u16 source_length)
{
	const u8 *anchor = source;
	const u8 *source_end = source + source_length;
	unsigned int count;
	u8 lastval;
	u8 *op = dest;
	const u8 *ip = source;

	do {
		/* RLE_SIMPLE */
		do {
			lastval = *ip++;
		} while ((ip < source_end) && (lastval != *ip));
		count = ip - anchor - (ip < source_end);
		if (count > 0) {
			while (count > RLE_MAX_LENGTH) {
				*op++ = RLE_SIMPLE | RLE_MAX_LENGTH;
				memcpy(op, anchor, RLE_MAX_LENGTH + 1);
				anchor += RLE_MAX_LENGTH + 1;
				op += RLE_MAX_LENGTH + 1;
				count -= RLE_MAX_LENGTH + 1;
			}
			if (count > 0) {
				*op++ = RLE_SIMPLE | (count - 1);
				memcpy(op, anchor, count);
				anchor += count;
				op += count;
			}
		}
		if (ip == source_end)
			return op - dest;
		/* RLE_REPEAT */
		do {
			lastval = *ip++;
		} while ((ip < source_end) && (lastval == *ip));
		count = ip - anchor;
		if (count > 0) {
			anchor += count;
			while (count > RLE_MAX_LENGTH) {
				*op++ = RLE_REPEAT | RLE_MAX_LENGTH;
				*op++ = lastval;
				count -= RLE_MAX_LENGTH + 1;
			}
			if (count > 0) {
				*op++ = RLE_REPEAT | (count - 1);
				*op++ = lastval;
			}
		}
	} while (ip != source_end);
	return op - dest;
}

static __always_inline int decompress_rle(const u8 * const source,
					  u8 * const dest, u16 * const wrkmem,
					  const u16 source_length,
					  const int safe_mode)
{
	unsigned int length;
	const u8 *ip = source;
	u8 *op = dest;
	const u8 *const source_end = source + source_length;

	while (ip + 1 < source_end) {
		if ((*ip & RLE_REPEAT) == RLE_REPEAT) {
			length = *ip++ - RLE_REPEAT + 1;
			if (safe_mode
			    && unlikely(op + length
					> dest + ZBEWALGO_BUFFER_SIZE))
				return -EINVAL;
			memset(op, *ip++, length);
			op += length;
		} else {
			length = *ip++ - RLE_SIMPLE + 1;
			if (safe_mode && unlikely(ip + length > source_end))
				return -EINVAL;
			if (safe_mode
			    && unlikely(op + length
					> dest + ZBEWALGO_BUFFER_SIZE))
				return -EINVAL;
			memcpy(op, ip, length);
			ip += length;
			op += length;
		}
	}
	return op - dest;
}

static __always_inline int decompress_rle_safe(const u8 * const source,
					       u8 * const dest,
					       u16 * const wrkmem,
					       const u16 source_length){
	return decompress_rle(source, dest, wrkmem, source_length, 1);
}

static __always_inline int decompress_rle_fast(const u8 * const source,
					       u8 * const dest,
					       u16 * const wrkmem,
					       const u16 source_length){
	return decompress_rle(source, dest, wrkmem, source_length, 0);
}

static int init_rle(void)
{
	return 0;
}

static void exit_rle(void)
{
}

struct zbewalgo_alg alg_rle = {
	.name = "rle",
	.flags = ZBEWALGO_ALG_FLAG_COMPRESS,
	.wrkmem_size = 0,
	.init = init_rle,
	.exit = exit_rle,
	.compress = compress_rle,
	.decompress_safe = decompress_rle_safe,
	.decompress_fast = decompress_rle_fast
};
