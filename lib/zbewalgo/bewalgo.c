// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Benjamin Warnke <4bwarnke@informatik.uni-hamburg.de>
 *
 * Benjamin Warnke invented this algorithm for his bachelors thesis
 * 'Page-Based compression in the Linux Kernel'. This algorithm is
 * mainly inspired by lz4, focusing on increasing the speed even more,
 * with the help of page aligned read an write access. To achieve the
 * page alignment, the input and output data is accessed only in
 * blocks of 8 Bytes, therefore the encoding of the compressed data is
 * changed.
 *
 * His thesis is available at:
 * https://wr.informatik.uni-hamburg.de/_media/research:theses:benjamin_warnke_page_based_compression_in_the_linux_kernel.pdf
 */

#include "bewalgo.h"

#define BEWALGO_ACCELERATION_DEFAULT 1

#define BEWALGO_MEMORY_USAGE 14

#define BEWALGO_SKIPTRIGGER 6

#define BEWALGO_LENGTH_BITS 8
#define BEWALGO_LENGTH_MAX ((1 << BEWALGO_LENGTH_BITS) - 1)
#define BEWALGO_OFFSET_BITS 16
#define BEWALGO_OFFSET_MAX ((1 << BEWALGO_OFFSET_BITS) - 1)

#define BEWALGO_HASHLOG (BEWALGO_MEMORY_USAGE - 2)

/*
 * this macro is faster than memcpy if mostly short byte sequences are
 * copied
 */
#define bewalgo_copy_helper_src(d, s, t) \
do { \
	while (s + 32 <= t) { \
		put_unaligned_le64(get_unaligned_le64(s), d);\
		put_unaligned_le64(get_unaligned_le64(s + 8), d + 8);\
		put_unaligned_le64(get_unaligned_le64(s + 16), d + 16);\
		put_unaligned_le64(get_unaligned_le64(s + 24), d + 24);\
		d += 32; \
		s += 32; \
	} \
	if (s + 16 <= t) { \
		put_unaligned_le64(get_unaligned_le64(s), d);\
		put_unaligned_le64(get_unaligned_le64(s + 8), d + 8);\
		d += 16; \
		s += 16; \
	} \
	if (s < t) {\
		put_unaligned_le64(get_unaligned_le64(s), d);\
		d += 8; \
		s += 8; \
	} \
} while (0)

static __always_inline int decompress_bewalgo(const u8 * const source,
					      u8 * const dest,
					      u16 * const wrkmem,
					      const u16 source_length,
					      const int safe_mode)
{
	const u16 dest_length =  get_unaligned_le16(source);
	const u8 * const source_end = source + ((source_length - 2) & ~0x7);
	const u8 * const dest_end = dest + (dest_length & ~0x7);
	const u8 *ip = source + 2;
	u8 *match = dest;
	u8 *op = dest;
	const u8 *control_block_ptr;
	const u8 *target;
	u16 length[4];

	if (safe_mode
	    && unlikely(((source_length - 2) & 0x7) != (dest_length & 0x7)))
		return -EINVAL;
	if (safe_mode
	    && unlikely(ip + 8 >= source_end))
		return -EINVAL;
	if (safe_mode
	    && unlikely(dest_length > ZBEWALGO_BUFFER_SIZE))
		return -EINVAL;
	do {
		control_block_ptr = ip;
		length[0] = control_block_ptr[0] << 3;
		length[1] = control_block_ptr[1] << 3;
		length[2] = control_block_ptr[4] << 3;
		length[3] = control_block_ptr[5] << 3;
		if (safe_mode
		    && unlikely(ip + length[0] + length[2] > source_end))
			return -EINVAL;
		if (safe_mode
		    && unlikely(op + length[0] + length[1] + length[2]
			     + length[3] > dest_end))
			return -EINVAL;
		ip += 8;
		target = ip + length[0];
		bewalgo_copy_helper_src(op, ip, target);
		match = op - (get_unaligned_le16(control_block_ptr + 2) << 3);
		if (safe_mode && unlikely(match < dest))
			return -EINVAL;
		target = match + (control_block_ptr[1] << 3);
		bewalgo_copy_helper_src(op, match, target);
		target = ip + (control_block_ptr[4] << 3);
		bewalgo_copy_helper_src(op, ip, target);
		match = op - (get_unaligned_le16(control_block_ptr + 6) << 3);
		if (safe_mode && unlikely(match < dest))
			return -EINVAL;
		target = match + (control_block_ptr[5] << 3);
		bewalgo_copy_helper_src(op, match, target);
	} while (likely(ip < source_end));
	memcpy(op, ip, (source_length - 2) & 0x7);
	op += (source_length - 2) & 0x7;
	if (safe_mode && (dest_length != (op - dest)))
		return -EINVAL;
	return dest_length;
}

static __always_inline int decompress_bewalgo_safe(const u8 * const source,
						   u8 * const dest,
						   u16 * const wrkmem,
						   const u16 source_length){
	return decompress_bewalgo(source, dest, wrkmem, source_length, 1);
}

static __always_inline int decompress_bewalgo_fast(const u8 * const source,
						   u8 * const dest,
						   u16 * const wrkmem,
						   const u16 source_length){
	return decompress_bewalgo(source, dest, wrkmem, source_length, 0);
}

/*
 * The hashtable 'wrkmem' allows indicees in the range
 * [0 .. ((1 << BEWALGO_HASHLOG) - 1)].
 * Each input sequence hashed and mapped to a fixed index in that array. The
 * final shift '>> (64 - BEWALGO_HASHLOG)' guarantees, that the index is
 * valid. The hashtable is used to find known sequences in the input array.
 */
static __always_inline u32 bewalgo_compress_hash(u64 sequence)
{
	return ((sequence >> 24) * 11400714785074694791ULL)
		 >> (64 - BEWALGO_HASHLOG);
}

static __always_inline int
compress_bewalgo_(u16 *wrkmem,
		  const u8 * const source, u8 * const dest,
		  const u16 source_length, u8 acceleration)
{
	u32 * const table = (u32 *)wrkmem;
	const int acceleration_start =
		(acceleration < 4 ? 32 >> acceleration : 4);
	const u8 * const dest_end_ptr = dest
		+ ((zbewalgo_max_output_size + 0x7) & ~0x7) + 2;
	const u8 * const source_end_ptr = source
		+ (source_length & ~0x7);
	const u8 * const source_end_ptr_1 = source_end_ptr - 8;
	const u8 *match = source;
	const u8 *anchor = source;
	const u8 *ip = source;
	u8 *op = dest + 2;
	u8 *op_control = NULL;
	u32 op_control_available = 0;
	const u8 *target;
	int length;
	u16 offset;
	u32 h;
	int j;
	int tmp_literal_length;
	int match_nodd;
	int match_nzero_nodd;

	put_unaligned_le16(source_length, dest);
	memset(wrkmem, 0, 1 << BEWALGO_MEMORY_USAGE);
	do {
		j = acceleration_start;
		length = (source_end_ptr_1 - ip) >> 3;
		j = j < length ? j : length;
		for (length = 1; length <= j; length++) {
			ip += 8;
			h = bewalgo_compress_hash(get_unaligned_le64(ip));
			match = source + table[h];
			table[h] = ip - source;
			if (get_unaligned_le64(match)
			    == get_unaligned_le64(ip))
				goto _find_match_left;
		}
		length = acceleration_start
			+ (acceleration << BEWALGO_SKIPTRIGGER);
		ip += 8;
		do {
			if (unlikely(ip >= source_end_ptr))
				goto _encode_last_literal;
			h = bewalgo_compress_hash(get_unaligned_le64(ip));
			match = source + table[h];
			table[h] = ip - source;
			if (get_unaligned_le64(match)
			    == get_unaligned_le64(ip))
				goto _find_match_left;
			ip += (length++ >> BEWALGO_SKIPTRIGGER) << 3;
		} while (1);
_find_match_left:
		while ((match != source)
		       && (get_unaligned_le64(match - 8)
			   == get_unaligned_le64(ip - 8))) {
			ip -= 8;
			match -= 8;
			if (ip == anchor)
				goto _find_match_right;
		}
		length = (ip - anchor) >> 3;
		tmp_literal_length = length
			- (op_control_available ? BEWALGO_LENGTH_MAX : 0);
		if (unlikely(op
			 + (((tmp_literal_length / (BEWALGO_LENGTH_MAX * 2))
			 + ((tmp_literal_length % (BEWALGO_LENGTH_MAX * 2))
			    > 0)
			 + length) << 3) > dest_end_ptr)) {
			/* not compressible */
			return -EINVAL;
		}
		while (length > BEWALGO_LENGTH_MAX) {
			if (op_control_available == 0) {
				op_control = op;
				put_unaligned_le64(0, op);
				op += 8;
			}
			op_control_available = !op_control_available;
			*op_control = BEWALGO_LENGTH_MAX;
			op_control += 4;
			target = anchor + (BEWALGO_LENGTH_MAX << 3);
			bewalgo_copy_helper_src(op, anchor, target);
			length -= BEWALGO_LENGTH_MAX;
		}
		if (likely(length > 0)) {
			if (op_control_available == 0) {
				op_control = op;
				put_unaligned_le64(0, op);
				op += 8;
			}
			op_control_available = !op_control_available;
			*op_control = length;
			op_control += 4;
			bewalgo_copy_helper_src(op, anchor, ip);
		}
_find_match_right:
		do {
			ip += 8;
			match += 8;
		} while ((ip < source_end_ptr)
			 && (get_unaligned_le64(match)
			     == get_unaligned_le64(ip)));
		length = (ip - anchor) >> 3;
		offset = (ip - match) >> 3;
		anchor = ip;
		if (length > BEWALGO_LENGTH_MAX) {
			u32 val =
				(BEWALGO_LENGTH_MAX << 8) | (offset << 16);
			size_t match_length_div_255 = length / 255;
			size_t match_length_mod_255 = length % 255;
			u32 match_zero = match_length_mod_255 == 0;
			u32 match_nzero = !match_zero;
			int control_blocks_needed = match_length_div_255
				+ match_nzero
				- op_control_available;

			if (unlikely(op + (((control_blocks_needed >> 1)
					+ (control_blocks_needed & 1))
					 << 3) > dest_end_ptr)) {
				/* not compressible */
				return -EINVAL;
			}
			op_control = op_control_available > 0
				? op_control
				: op;
			put_unaligned_le32(val, op_control);
			match_length_div_255 -= op_control_available > 0;
			match_nodd = !(match_length_div_255 & 1);
			match_nzero_nodd = match_nzero && match_nodd;
			if (match_length_div_255 > 0) {
				u64 val_l =
					val
					| (((u64)val)
						<< 32);
				target = op + (((match_length_div_255 >> 1)
						+ (match_length_div_255 & 1))
					       << 3);
				do {
					put_unaligned_le64(val_l, op);
					op += 8;
				} while (op < target);
			}
			op_control = op - 4;
			put_unaligned_le32(0, op_control
					   + (match_nzero_nodd << 3));
			put_unaligned_le32(0, op_control
					    + (match_nzero_nodd << 2));
			*(op_control + (match_nzero_nodd << 2) + 1) =
				(match_zero & match_nodd)
				 ? BEWALGO_LENGTH_MAX
				 : match_length_mod_255;
			put_unaligned_le16(offset, op_control
					   + (match_nzero_nodd << 2) + 2);
			op_control += match_nzero_nodd << 3;
			op += match_nzero_nodd << 3;
			op_control_available =
				(match_length_div_255 & 1) == match_zero;
		} else {
			if (unlikely(op_control_available == 0
				     && op >= dest_end_ptr
				     && op_control[-3] != 0))
				return -EINVAL;
			if (op_control[-3] != 0) {
				if (op_control_available == 0) {
					op_control = op;
					put_unaligned_le64(0, op);
					op += 8;
				}
				op_control_available = !op_control_available;
				op_control += 4;
			}
			op_control[-3] = length;
			put_unaligned_le16(offset, op_control - 2);
		}
		if (unlikely(ip == source_end_ptr))
			goto _finish;
		h = bewalgo_compress_hash(get_unaligned_le64(ip));
		match = source + table[h];
		table[h] = ip - source;
		if (get_unaligned_le64(match) == get_unaligned_le64(ip))
			goto _find_match_right;
	} while (1);
_encode_last_literal:
	length = (source_end_ptr - anchor) >> 3;
	tmp_literal_length = length
		- (op_control_available ? BEWALGO_LENGTH_MAX : 0);
	if (op + ((((tmp_literal_length / (BEWALGO_LENGTH_MAX * 2)))
		+ ((tmp_literal_length % (BEWALGO_LENGTH_MAX * 2)) > 0)
		+ length) << 3) > dest_end_ptr)
		return -EINVAL;
	while (length > BEWALGO_LENGTH_MAX) {
		if (op_control_available == 0) {
			op_control = op;
			put_unaligned_le64(0, op);
			op += 8;
		}
		op_control_available = !op_control_available;
		*op_control = BEWALGO_LENGTH_MAX;
		op_control += 4;
		target = anchor + (BEWALGO_LENGTH_MAX << 3);
		bewalgo_copy_helper_src(op, anchor, target);
		length -= BEWALGO_LENGTH_MAX;
	}
	if (length > 0) {
		if (op_control_available == 0) {
			op_control = op;
			put_unaligned_le64(0, op);
			op += 8;
		}
		op_control_available = !op_control_available;
		*op_control = length;
		op_control += 4;
		bewalgo_copy_helper_src(op, anchor, source_end_ptr);
	}
_finish:
	memcpy(op, anchor, source_length & 0x7);
	op += source_length & 0x7;
	return op - dest;
}

static __always_inline int compress_bewalgo(const u8 * const source,
					    u8 * const dest,
					    u16 * const wrkmem,
					    const u16 source_length)
{
	return compress_bewalgo_(wrkmem,
				 source, dest,
				 source_length, 1);
}

static int init_bewalgo(void)
{
	return 0;
}

static void exit_bewalgo(void)
{
}

struct zbewalgo_alg alg_bewalgo = {
	.name = "bewalgo",
	.flags = ZBEWALGO_ALG_FLAG_COMPRESS,
	.wrkmem_size = 1 << BEWALGO_MEMORY_USAGE,
	.init = init_bewalgo,
	.exit = exit_bewalgo,
	.compress = compress_bewalgo,
	.decompress_safe = decompress_bewalgo_safe,
	.decompress_fast = decompress_bewalgo_fast
};
