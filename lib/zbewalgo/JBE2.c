// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Benjamin Warnke <4bwarnke@informatik.uni-hamburg.de>
 *
 * j-bit-encoding was published by 'I Made Agus Dwi Suarjaya' in 2012.
 * https://arxiv.org/pdf/1209.1045.pdf
 *
 * jbe2 is a minor modification of jbe. Swapping groups of 4 Bit in consecutive
 * Bytes can increase the compression ratio, if for example the first
 * 4 Bits of each Byte are zero. If jbe2 is called after mtf, this
 * happens ofthen.
 */

#include "JBE2.h"

/*
 * This implementation is modified to swap groups of 4 bits before processing
 */
static __always_inline int compress_jbe2(const u8 * const source,
					 u8 * const dest, u16 * const wrkmem,
					 const u16 source_length)
{
	u64 source_tmp;
	u8 tmp;
	const u8 *source_end = source + (source_length & ~0x7);
	const u8 *ip = source;
	u8 *data1 = dest + 2;
	u8 *data2 = data1 + (source_length >> 3);
	u8 * const source_tmp_ptr = (u8 *)(&source_tmp);

	put_unaligned_le16(source_length, dest);
	do {
		source_tmp = get_unaligned_le64(ip);
		ip += 8;
		source_tmp = (source_tmp & 0xF0F0F0F00F0F0F0FULL)
			| ((source_tmp & 0x0F0F0F0F00000000ULL) >> 28)
			| ((source_tmp & 0x00000000F0F0F0F0ULL) << 28);
		tmp = source_tmp_ptr[0] > 0;
		*data2 = source_tmp_ptr[0];
		*data1 = tmp << 7;
		data2 += tmp;
		tmp = source_tmp_ptr[1] > 0;
		*data2 = source_tmp_ptr[1];
		*data1 |= tmp << 6;
		data2 += tmp;
		tmp = source_tmp_ptr[2] > 0;
		*data2 = source_tmp_ptr[2];
		*data1 |= tmp << 5;
		data2 += tmp;
		tmp = source_tmp_ptr[3] > 0;
		*data2 = source_tmp_ptr[3];
		*data1 |= tmp << 4;
		data2 += tmp;
		tmp = source_tmp_ptr[4] > 0;
		*data2 = source_tmp_ptr[4];
		*data1 |= tmp << 3;
		data2 += tmp;
		tmp = source_tmp_ptr[5] > 0;
		*data2 = source_tmp_ptr[5];
		*data1 |= tmp << 2;
		data2 += tmp;
		tmp = source_tmp_ptr[6] > 0;
		*data2 = source_tmp_ptr[6];
		*data1 |= tmp << 1;
		data2 += tmp;
		tmp = source_tmp_ptr[7] > 0;
		*data2 = source_tmp_ptr[7];
		*data1 |= tmp;
		data2 += tmp;
		data1++;
	} while (ip < source_end);
	memcpy(data2, ip, source_length & 0x7);
	data2 += source_length & 0x7;
	return data2 - dest;
}

static __always_inline int decompress_jbe2(const u8 * const source,
					   u8 * const dest, u16 * const wrkmem,
					   const u16 source_length,
					   const int safe_mode)
{
	u64 dest_tmp;
	const u16 dest_length = get_unaligned_le16(source);
	const u8 *data1 = source + 2;
	const u8 *data2 = data1 + (dest_length >> 3);
	const u8 *dest_end = dest + (dest_length & ~0x7);
	u8 * const dest_tmp_ptr = (u8 *)(&dest_tmp);
	u8 *op = dest;
	const u8 * const source_end = source + source_length;
	const u8 * const source_end_8 = source_end - 8;

	if (safe_mode && unlikely(dest_length > ZBEWALGO_BUFFER_SIZE))
		return -EINVAL;
	if (safe_mode && unlikely(dest_length > source_length << 3))
		return -EINVAL;
	do {
		if (data2 >= source_end_8)
			goto _last_8;
		dest_tmp_ptr[0] = ((*data1 & 0x80) ? *data2 : 0);
		data2 += (*data1 & 0x80) > 0;
		dest_tmp_ptr[1] = ((*data1 & 0x40) ? *data2 : 0);
		data2 += (*data1 & 0x40) > 0;
		dest_tmp_ptr[2] = ((*data1 & 0x20) ? *data2 : 0);
		data2 += (*data1 & 0x20) > 0;
		dest_tmp_ptr[3] = ((*data1 & 0x10) ? *data2 : 0);
		data2 += (*data1 & 0x10) > 0;
		dest_tmp_ptr[4] = ((*data1 & 0x08) ? *data2 : 0);
		data2 += (*data1 & 0x08) > 0;
		dest_tmp_ptr[5] = ((*data1 & 0x04) ? *data2 : 0);
		data2 += (*data1 & 0x04) > 0;
		dest_tmp_ptr[6] = ((*data1 & 0x02) ? *data2 : 0);
		data2 += (*data1 & 0x02) > 0;
		dest_tmp_ptr[7] = ((*data1 & 0x01) ? *data2 : 0);
		data2 += (*data1 & 0x01) > 0;
		data1++;
		dest_tmp = (dest_tmp & 0xF0F0F0F00F0F0F0FULL)
			| ((dest_tmp & 0x0F0F0F0F00000000ULL) >> 28)
			| ((dest_tmp & 0x00000000F0F0F0F0ULL) << 28);
		put_unaligned_le64(dest_tmp, op);
		op += 8;
	} while (op < dest_end);
	goto _finish;
_last_8:
	/*
	 * Reading last 8 bytes from data2. This may produce a lot of output,
	 * if data1 indicates to NOT read from - and inrement - data2
	 */
	do {
		dest_tmp = 0;
		if (safe_mode && unlikely(data1 >= source_end))
			return -EINVAL;
		if (*data1 & 0x80) {
			if (safe_mode && unlikely(data2 >= source_end))
				return -EINVAL;
			dest_tmp_ptr[0] = *data2++;
		}
		if (*data1 & 0x40) {
			if (safe_mode && unlikely(data2 >= source_end))
				return -EINVAL;
			dest_tmp_ptr[1] = *data2++;
		}
		if (*data1 & 0x20) {
			if (safe_mode && unlikely(data2 >= source_end))
				return -EINVAL;
			dest_tmp_ptr[2] = *data2++;
		}
		if (*data1 & 0x10) {
			if (safe_mode && unlikely(data2 >= source_end))
				return -EINVAL;
			dest_tmp_ptr[3] = *data2++;
		}
		if (*data1 & 0x08) {
			if (safe_mode && unlikely(data2 >= source_end))
				return -EINVAL;
			dest_tmp_ptr[4] = *data2++;
		}
		if (*data1 & 0x04) {
			if (safe_mode && unlikely(data2 >= source_end))
				return -EINVAL;
			dest_tmp_ptr[5] = *data2++;
		}
		if (*data1 & 0x02) {
			if (safe_mode && unlikely(data2 >= source_end))
				return -EINVAL;
			dest_tmp_ptr[6] = *data2++;
		}
		if (*data1 & 0x01) {
			if (safe_mode && unlikely(data2 >= source_end))
				return -EINVAL;
			dest_tmp_ptr[7] = *data2++;
		}
		data1++;
		dest_tmp = (dest_tmp & 0xF0F0F0F00F0F0F0FULL)
			| ((dest_tmp & 0x0F0F0F0F00000000ULL) >> 28)
			| ((dest_tmp & 0x00000000F0F0F0F0ULL) << 28);
		put_unaligned_le64(dest_tmp, op);
		op += 8;
	} while (op < dest_end);
_finish:
	memcpy(op, data2, dest_length & 0x7);
	op += dest_length & 0x7;
	if (safe_mode && (dest_length != (op - dest)))
		return -EINVAL;
	return dest_length;
}

static __always_inline int decompress_jbe2_safe(const u8 * const source,
						u8 * const dest,
						u16 * const wrkmem,
						const u16 source_length)
{
	return decompress_jbe2(source, dest, wrkmem, source_length, 1);
}

static __always_inline int decompress_jbe2_fast(const u8 * const source,
						u8 * const dest,
						u16 * const wrkmem,
						const u16 source_length)
{
	return decompress_jbe2(source, dest, wrkmem, source_length, 0);
}

static int init_jbe2(void)
{
	return 0;
}

static void exit_jbe2(void)
{
}

struct zbewalgo_alg alg_jbe2 = {
	.name = "jbe2",
	.flags = ZBEWALGO_ALG_FLAG_COMPRESS,
	.wrkmem_size = 0,
	.init = init_jbe2,
	.exit = exit_jbe2,
	.compress = compress_jbe2,
	.decompress_safe = decompress_jbe2_safe,
	.decompress_fast = decompress_jbe2_fast
};
