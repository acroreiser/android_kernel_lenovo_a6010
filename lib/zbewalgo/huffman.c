// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Benjamin Warnke <4bwarnke@informatik.uni-hamburg.de>
 */

#include "huffman.h"

/*
 * compression using the bare huffman encoding. Optimized for speed and small
 * input buffers.
 * wrkmem should be aligned by the caller to at least 4
 */
static __always_inline int compress_huffman(const u8 * const source,
					    u8 * const dest,
					    u16 * const wrkmem,
					    const u16 source_length)
{
	const u8 * const source_end = source + source_length;
	const u8 * const dest_end = dest + zbewalgo_max_output_size;
	short * const nodes_index = (short *)(wrkmem);
	u16 * const leaf_parent_index = wrkmem + 768;
	u16 * const nodes_weight = wrkmem + 1024;
	u16 * const frequency = wrkmem + 1536;
	u16 * const code_lengths = wrkmem + 1792;
	u32 * const code_words = (u32 *)(wrkmem + 2048);
	short i;
	u16 node_index;
	int i2;
	short max_codeword_length;
	u32 weight2;
	short num_nodes;
	u32 bits_in_buffer;
	u32 bits_in_stack;
	u16 free_index;
	const u8 *ip;
	u8 *op;
	u32 stack;
	u8 * const stack_ptr = (u8 *)&stack;

	memset(dest, 0, zbewalgo_max_output_size);
	memset(wrkmem, 0, 5120);
	op = dest;
	ip = source;
	put_unaligned_le16(source_length, dest);
	do {
		frequency[*ip++]++;
	} while (ip < source_end);
	ip = source;
	num_nodes = 0;
	for (i = 0; i < 256; i++) {
		if (frequency[i] > 0) {
			i2 = num_nodes++;
			while ((i2 > 0) && (nodes_weight[i2] > frequency[i])) {
				nodes_weight[i2 + 1] = nodes_weight[i2];
				nodes_index[i2 + 1] = nodes_index[i2];
				leaf_parent_index[nodes_index[i2]]++;
				i2--;
			}
			i2++;
			nodes_index[i2] = -(i + 1);
			leaf_parent_index[-(i + 1)] = i2;
			nodes_weight[i2] = frequency[i];
		}
	}
	dest[2] = num_nodes - 1;
	op = dest + 3;
	for (i = 1; i <= num_nodes; ++i) {
		*op++ = -(nodes_index[i] + 1);
		put_unaligned_le16(nodes_weight[i], op);
		op += 2;
	}
	free_index = 2;
	while (free_index <= num_nodes) {
		weight2 = nodes_weight[free_index - 1]
			+ nodes_weight[free_index];
		i2 = num_nodes++;
		while ((i2 > 0) && (nodes_weight[i2] > weight2)) {
			nodes_weight[i2 + 1] = nodes_weight[i2];
			nodes_index[i2 + 1] = nodes_index[i2];
			leaf_parent_index[nodes_index[i2]]++;
			i2--;
		}
		i2++;
		nodes_index[i2] = free_index >> 1;
		leaf_parent_index[free_index >> 1] = i2;
		nodes_weight[i2] = weight2;
		free_index += 2;
	}
	if (num_nodes > 400) {
		/* not compressible */
		return -EINVAL;
	}
	for (i = 0; i < 256; i++) {
		if (frequency[i] == 0)
			continue;
		bits_in_stack = 0;
		stack = 0;
		node_index = leaf_parent_index[-(i + 1)];
		while (node_index < num_nodes) {
			stack |= (node_index & 0x1) << bits_in_stack;
			bits_in_stack++;
			node_index = leaf_parent_index[(node_index + 1) >> 1];
		}
		code_lengths[i] = bits_in_stack;
		code_words[i] = stack;
	}
	max_codeword_length = 0;
	node_index = leaf_parent_index[nodes_index[1]];
	while (node_index < num_nodes) {
		node_index = leaf_parent_index[(node_index + 1) >> 1];
		max_codeword_length++;
	}
	if (max_codeword_length > 24) {
		/* not encodeable with optimizations */
		return -EINVAL;
	}
	bits_in_buffer = 0;
	do {
		bits_in_stack = code_lengths[*ip];
		stack = code_words[*ip];
		ip++;
		bits_in_buffer += bits_in_stack;
		stack = stack << (32 - bits_in_buffer);
#ifdef __LITTLE_ENDIAN
		op[0] |= stack_ptr[3];
		op[1] |= stack_ptr[2];
		op[2] |= stack_ptr[1];
		op[3] |= stack_ptr[0];
#else
		op[0] |= stack_ptr[0];
		op[1] |= stack_ptr[1];
		op[2] |= stack_ptr[2];
		op[3] |= stack_ptr[3];
#endif
		op += bits_in_buffer >> 3;
		bits_in_buffer = bits_in_buffer & 0x7;
		if (unlikely(op >= dest_end)) {
			/* not compressible */
			return -EINVAL;
		}
	} while (likely(ip < source_end));
	return op - dest + (bits_in_buffer > 0);
}

/*
 * reverses the huffman compression
 */
static __always_inline int decompress_huffman(const u8 * const source,
					      u8 * const dest,
					      u16 * const wrkmem,
					      const u16 source_length,
					      const int safe_mode)
{
	const u8 * const source_end = source + source_length;
	const u32 dest_length = get_unaligned_le16(source);
	const u8 * const dest_end = dest + dest_length;
	short * const nodes_index = (short *)(wrkmem);
	u16 * const nodes_weight = wrkmem + 512;
	u32 i;
	int node_index;
	u32 i2;
	u32 weight2;
	u32 num_nodes;
	u32 current_bit;
	u32 free_index;
	const u8 *ip;
	u8 *op;

	if (safe_mode && unlikely(dest_length > ZBEWALGO_BUFFER_SIZE))
		return -EINVAL;

	memset(wrkmem, 0, 2048);
	num_nodes = source[2] + 1;
	ip = source + 3;
	op = dest;
	if (safe_mode && unlikely(ip + 3 * num_nodes > source_end)) {
		/* source_length too small */
		return -EINVAL;
	}
	if (safe_mode && unlikely(num_nodes == 0))
		return -EINVAL;
	for (i = 1; i <= num_nodes; ++i) {
		nodes_index[i] = -(*ip++ + 1);
		nodes_weight[i] = get_unaligned_le16(ip);
		ip += 2;
	}
	free_index = 2;
	while (free_index <= num_nodes) {
		weight2 = nodes_weight[free_index - 1]
			+ nodes_weight[free_index];
		i2 = num_nodes++;
		while (i2 > 0 && nodes_weight[i2] > weight2) {
			nodes_weight[i2 + 1] = nodes_weight[i2];
			nodes_index[i2 + 1] = nodes_index[i2];
			i2--;
		}
		i2++;
		nodes_index[i2] = free_index >> 1;
		nodes_weight[i2] = weight2;
		free_index += 2;
	}
	current_bit = 0;
	do {
		node_index = nodes_index[num_nodes];
		while (node_index > 0) {
			if (safe_mode) {
				/* evaluated at compiletime */
				if (current_bit == 8) {
					ip++;
					if (ip >= source_end) {
						/* source_length too small */
						return -EINVAL;
					}
				}
			} else{
				ip += current_bit == 8;
			}
			current_bit = ((current_bit) & 0x7) + 1;
			node_index = (node_index << 1)
				   - ((*ip >> (8 - current_bit)) & 0x1);
			if (safe_mode && node_index >= num_nodes)
				return -EINVAL;
			node_index = nodes_index[node_index];
		}
		*op++ = -(node_index + 1);
	} while (op < dest_end);
	return dest_length;
}

static __always_inline int decompress_huffman_safe(const u8 * const source,
						   u8 * const dest,
						   u16 * const wrkmem,
						   const u16 source_length){
	return decompress_huffman(source, dest, wrkmem, source_length, 1);
}

static __always_inline int decompress_huffman_fast(const u8 * const source,
						   u8 * const dest,
						   u16 * const wrkmem,
						   const u16 source_length){
	return decompress_huffman(source, dest, wrkmem, source_length, 0);
}

static int init_huffman(void)
{
	return 0;
}

static void exit_huffman(void)
{
}

struct zbewalgo_alg alg_huffman = {
	.name = "huffman",
	.flags = ZBEWALGO_ALG_FLAG_COMPRESS,
	.wrkmem_size = 5120,
	.init = init_huffman,
	.exit = exit_huffman,
	.compress = compress_huffman,
	.decompress_safe = decompress_huffman_safe,
	.decompress_fast = decompress_huffman_fast
};
