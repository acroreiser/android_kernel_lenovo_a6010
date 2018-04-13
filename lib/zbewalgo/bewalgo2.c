// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Benjamin Warnke <4bwarnke@informatik.uni-hamburg.de>
 *
 * BeWalgo2 reads it's input in blocks of 8 Bytes. These Blocks
 * are added to an avl-tree. The avl-tree is mapped directly to an
 * array. The encoding is a variation of Run Length Encoding using the
 * indices in the avl-tree as data. The reason for using the tree
 * with indices is, that the indices can be encoded in less then
 * 8 Bytes each.
 */

#include "bewalgo2.h"

#define MAX_LITERALS ((zbewalgo_max_output_size >> 3) - 4)

#define OFFSET_BITS 9
#define OFFSET_SHIFT (16 - OFFSET_BITS)
#define MATCH_BIT_SHIFT 6
#define MATCH_BIT_MASK BIT(MATCH_BIT_SHIFT)
#define LENGTH_BITS 6
#define LENGTH_MASK ((1 << LENGTH_BITS) - 1)
#define LEFT 0
#define RIGHT 1
#define NEITHER 2
#define TREE_NODE_NULL 0xffff

/*
 * insert first node into an empty avl-tree
 * the function returns the index of the node in the preallocated array
 */
static __always_inline int avl_insert_first(u64 *wrk_literal, u16 *wrk_tree,
					    u16 *treep, u64 target,
					    u16 *treesize)
{
	u16 g_a_tree, g_o_tree2;

	g_a_tree = *treesize;
	g_o_tree2 = g_a_tree << 2;
	*treesize = *treesize + 1;
	wrk_tree[g_o_tree2 + 0] = TREE_NODE_NULL;
	wrk_tree[g_o_tree2 + 1] = TREE_NODE_NULL;
	wrk_tree[g_o_tree2 + 2] = NEITHER;
	wrk_literal[g_a_tree] = target;
	*treep = g_a_tree;
	return g_a_tree;
}

/*
 * insert a node into a non-empty avl-tree
 * for speed, the nodes are saved in preallocated arrays
 * the function returns the index of the node in the preallocated array
 */
static __always_inline int avl_insert(u64 *wrk_literal, u16 *wrk_tree,
				      u16 *treep, u64 target,
				      u16 *treesize)
{
	u16 *path_top[2];
	u16 g_a_tree, g_o_tree2, g_o_next_step;
	u16 r_1_arr[10];
	u16 path, path2, first[2], second;

	if (unlikely(target == wrk_literal[*treep]))
		return *treep;
	path_top[0] = treep;
	g_o_next_step = target > wrk_literal[*treep];
	g_o_tree2 = *treep << 2;
	path_top[wrk_tree[g_o_tree2 + 2] == NEITHER] = treep;
	treep = &wrk_tree[g_o_tree2 + g_o_next_step];
	if (unlikely(*treep == TREE_NODE_NULL))
		goto _insert_new_node;
_search_node:
	if (target == wrk_literal[*treep])
		return *treep;
	g_o_next_step = target > wrk_literal[*treep];
	g_o_tree2 = *treep << 2;
	path_top[wrk_tree[g_o_tree2 + 2] == NEITHER] = treep;
	treep = &wrk_tree[g_o_tree2 + g_o_next_step];
	if (likely(*treep != TREE_NODE_NULL))
		goto _search_node;
_insert_new_node:
	g_a_tree = *treesize;
	g_o_tree2 = g_a_tree << 2;
	*treesize = *treesize + 1;
	wrk_tree[g_o_tree2 + 0] = TREE_NODE_NULL;
	wrk_tree[g_o_tree2 + 1] = TREE_NODE_NULL;
	wrk_tree[g_o_tree2 + 2] = NEITHER;
	wrk_literal[g_a_tree] = target;
	*treep = g_a_tree;
	path = *path_top[0];
	path2 = path << 2;
	if (wrk_tree[path2 + 2] == NEITHER) {
		do {
			r_1_arr[0] = target > wrk_literal[path];
			wrk_tree[path2 + 2] = r_1_arr[0];
			path = wrk_tree[path2 + r_1_arr[0]];
			path2 = path << 2;
		} while (target != wrk_literal[path]);
		return g_a_tree;
	}
	first[0] = target > wrk_literal[path];
	if (wrk_tree[path2 + 2] != first[0]) {
		wrk_tree[path2 + 2] = NEITHER;
		r_1_arr[2] = wrk_tree[path2 + first[0]];
		if (target == wrk_literal[r_1_arr[2]])
			return g_a_tree;
		do {
			r_1_arr[0] = target > wrk_literal[r_1_arr[2]];
			r_1_arr[1] = r_1_arr[2] << 2;
			r_1_arr[2] = wrk_tree[r_1_arr[1] + r_1_arr[0]];
			wrk_tree[r_1_arr[1] + 2] = r_1_arr[0];
		} while (target != wrk_literal[r_1_arr[2]]);
		return g_a_tree;
	}
	second = target > wrk_literal[wrk_tree[path2 + first[0]]];
	first[1] = 1 - first[0];
	if (first[0] == second) {
		r_1_arr[2] = *path_top[0];
		r_1_arr[3] = r_1_arr[2] << 2;
		r_1_arr[4] = wrk_tree[r_1_arr[3] + first[0]];
		r_1_arr[5] = r_1_arr[4] << 2;
		wrk_tree[r_1_arr[3] + first[0]] =
			wrk_tree[r_1_arr[5] + first[1]];
		path = wrk_tree[r_1_arr[5] + first[0]];
		*path_top[0] = r_1_arr[4];
		wrk_tree[r_1_arr[5] + first[1]] = r_1_arr[2];
		wrk_tree[r_1_arr[3] + 2] = NEITHER;
		wrk_tree[r_1_arr[5] + 2] = NEITHER;
		if (target == wrk_literal[path])
			return g_a_tree;
		do {
			r_1_arr[0] = target > wrk_literal[path];
			r_1_arr[1] = path << 2;
			wrk_tree[r_1_arr[1] + 2] = r_1_arr[0];
			path = wrk_tree[r_1_arr[1] + r_1_arr[0]];
		} while (target != wrk_literal[path]);
		return g_a_tree;
	}
	path = wrk_tree[(wrk_tree[path2 + first[0]] << 2) + second];
	r_1_arr[5] = *path_top[0];
	r_1_arr[1] = r_1_arr[5] << 2;
	r_1_arr[8] = wrk_tree[r_1_arr[1] + first[0]];
	r_1_arr[0] = r_1_arr[8] << 2;
	r_1_arr[6] = wrk_tree[r_1_arr[0] + first[1]];
	r_1_arr[7] = r_1_arr[6] << 2;
	r_1_arr[2] = wrk_tree[r_1_arr[7] + first[1]];
	r_1_arr[3] = wrk_tree[r_1_arr[7] + first[0]];
	*path_top[0] = r_1_arr[6];
	wrk_tree[r_1_arr[7] + first[1]] = r_1_arr[5];
	wrk_tree[r_1_arr[7] + first[0]] = r_1_arr[8];
	wrk_tree[r_1_arr[1] + first[0]] = r_1_arr[2];
	wrk_tree[r_1_arr[0] + first[1]] = r_1_arr[3];
	wrk_tree[r_1_arr[7] + 2] = NEITHER;
	wrk_tree[r_1_arr[1] + 2] = NEITHER;
	wrk_tree[r_1_arr[0] + 2] = NEITHER;
	if (target == wrk_literal[path])
		return g_a_tree;
	r_1_arr[9] = (target > wrk_literal[path]) == first[0];
	wrk_tree[r_1_arr[r_1_arr[9]] + 2] = first[r_1_arr[9]];
	path = r_1_arr[r_1_arr[9] + 2];
	if (target == wrk_literal[path])
		return g_a_tree;
	do {
		path2 = path << 2;
		r_1_arr[4] = target > wrk_literal[path];
		wrk_tree[path2 + 2] = r_1_arr[4];
		path = wrk_tree[path2 + r_1_arr[4]];
	} while (target != wrk_literal[path]);
	return g_a_tree;
}

/*
 * compress the given data using a binary tree holding all the previously
 * seen 64-bit values
 * returns the length of the compressed data
 * wrkmem should be aligned to at least 8 by the caller
 */
static __always_inline int compress_bewalgo2(const u8 * const source,
					     u8 * const dest,
					     u16 * const wrkmem,
					     const u16 source_length)
{
	const u8 * const source_begin = source;
	u64 *wrk_literal = (u64 *)wrkmem;
	u16 *wrk_tree = wrkmem + 2048;
	u8 *op_match = dest + 4;
	int count;
	u16 i;
	u16 j;
	u16 tree_root = TREE_NODE_NULL;
	u16 tree_size = 0;
	const u8 *ip = source + (source_length & ~0x7) - 8;

	/*
	 * add first value into the tree
	 */
	i = avl_insert_first(wrk_literal, wrk_tree, &tree_root,
			     get_unaligned_le64(ip), &tree_size);
	/*
	 * to gain performance abort if the data does not seem to be
	 * compressible
	 */
	if (source_length > 512) {
		/*
		 * verify that at there are at most 5 different values
		 * at the first 10 positions
		 */
		for (i = 2; i < 11; i++)
			avl_insert(wrk_literal, wrk_tree, &tree_root,
				   get_unaligned_le64(ip - (i << 3)),
				   &tree_size);
		if (tree_size < 6)
			goto _start;
		/*
		 * verify that there are at most 12 different values
		 * at the first 10 and last 10 positions
		 */
		for (i = 0; i < 10; i++)
			avl_insert(wrk_literal, wrk_tree, &tree_root,
				   get_unaligned_le64(source_begin + (i << 3)),
				   &tree_size);
		if (tree_size < 13)
			goto _start;
		/*
		 * if the previous conditions do not pass, check some bytes
		 * in the middle for matches.
		 * if not enough matches found: abort
		 */
		for (i = 0; i < 10; i++)
			avl_insert(wrk_literal, wrk_tree, &tree_root,
				   get_unaligned_le64(source_begin
						       + (256 + (i << 3))),
						      &tree_size);
		if (tree_size >= 21) {
			/* not compressible */
			return -EINVAL;
		}
	}
_start:
	i = 0;
_loop1_after_insert:
	count = 0;
	do {
		ip -= 8;
		count++;
	} while (likely(ip > source_begin)
		 && (get_unaligned_le64(ip) == wrk_literal[i]));
	if (count == 1) {
		count = 0;
		ip += 8;
		j = i + count;
		do {
			ip -= 8;
			count++;
			j++;
		} while (likely(ip > source_begin)
			 && (j < tree_size)
			 && (get_unaligned_le64(ip) == wrk_literal[j]));
		ip += 8;
		count--;
		if (likely(ip > source_begin)) {
			do {
				ip -= 8;
				count++;
				j = avl_insert(wrk_literal, wrk_tree,
					       &tree_root,
					       get_unaligned_le64(ip),
					       &tree_size);
				if (unlikely(tree_size > MAX_LITERALS)) {
					/* not compressible */
					return -EINVAL;
				}
			} while ((j == i + count)
				 && likely(ip > source_begin));
		}
		while (unlikely(count > LENGTH_MASK)) {
			put_unaligned_le16((i << OFFSET_SHIFT)
					   + MATCH_BIT_MASK + LENGTH_MASK,
					   op_match);
			op_match += 2;
			count -= LENGTH_MASK;
			i += LENGTH_MASK;
		}
		put_unaligned_le16((i << OFFSET_SHIFT) + MATCH_BIT_MASK
				   + count, op_match);
		op_match += 2;
		if (unlikely(ip <= source_begin))
			goto _finish;
		i = j;
		goto _loop1_after_insert;
	} else {
		while (unlikely(count > LENGTH_MASK)) {
			put_unaligned_le16((i << OFFSET_SHIFT) + LENGTH_MASK,
					   op_match);
			op_match += 2;
			count -= LENGTH_MASK;
		}
		put_unaligned_le16((i << OFFSET_SHIFT) + count, op_match);
		op_match += 2;
	}
	if (unlikely(ip <= source_begin))
		goto _finish;
	i = avl_insert(wrk_literal, wrk_tree, &tree_root,
		       get_unaligned_le64(ip), &tree_size);
	goto _loop1_after_insert;
_finish:
	j = avl_insert(wrk_literal, wrk_tree, &tree_root,
		       get_unaligned_le64(ip), &tree_size);
	put_unaligned_le16((j << OFFSET_SHIFT) + 1, op_match);
	op_match += 2;
	put_unaligned_le16(op_match - dest, dest);
	put_unaligned_le16(source_length, dest + 2);
	count = sizeof(u64) * (tree_size);
	memcpy(op_match, wrk_literal, count);
	op_match += count;
	memcpy(op_match, source + (source_length & ~0x7), source_length & 0x7);
	op_match += source_length & 0x7;
	return op_match - dest;
}

static __always_inline int decompress_bewalgo2(const u8 * const source,
					       u8 * const dest,
					       u16 * const wrkmem,
					       const u16 source_length,
					       const int safe_mode)
{
	const u16 dest_length = get_unaligned_le16(source + 2);
	const u8 * const source_end1 = source + source_length
					- (dest_length & 0x7);
	const u8 *ip_match1 = source + 4;
	const u8 *ip_match_end1 = source + get_unaligned_le16(source);
	const u8 *ip_literal1 = ip_match_end1;
	u16 i;
	u16 count;
	u64 tmp;
	u8 *op1 = dest + (dest_length & ~0x7) - 8;

	if (safe_mode && unlikely(op1 - dest > ZBEWALGO_BUFFER_SIZE))
		return -EINVAL;
	while (ip_match1 < ip_match_end1) {
		i = (get_unaligned_le16(ip_match1) >> OFFSET_SHIFT) << 3;
		count = get_unaligned_le16(ip_match1) & LENGTH_MASK;
		if (get_unaligned_le16(ip_match1) & MATCH_BIT_MASK) {
			if (safe_mode && unlikely(ip_literal1 + i
				+ (count << 3) > source_end1))
				return -EINVAL;
			if (safe_mode
			    && unlikely(op1 - ((count - 1) << 3) < dest))
				return -EINVAL;
			while (count-- > 0) {
				tmp = get_unaligned_le64(ip_literal1 + i);
				i += 8;
				put_unaligned_le64(tmp, op1);
				op1 -= 8;
			}
		} else{
			if (safe_mode
			    && unlikely(ip_literal1 + i > source_end1))
				return -EINVAL;
			if (safe_mode
			    && unlikely(op1 - ((count - 1) << 3) < dest))
				return -EINVAL;
			while (count-- > 0) {
				tmp = get_unaligned_le64(ip_literal1 + i);
				put_unaligned_le64(tmp, op1);
				op1 -= 8;
			}
		}
		ip_match1 += 2;
	}
	memcpy(dest + (dest_length & ~0x7), source_end1, dest_length & 0x7);
	return dest_length;
}

static __always_inline int decompress_bewalgo2_safe(const u8 * const source,
						    u8 * const dest,
						    u16 * const wrkmem,
						    const u16 source_length){
	return decompress_bewalgo2(source, dest, wrkmem, source_length, 1);
}

static __always_inline int decompress_bewalgo2_fast(const u8 * const source,
						    u8 * const dest,
						    u16 * const wrkmem,
						    const u16 source_length){
	return decompress_bewalgo2(source, dest, wrkmem, source_length, 0);
}

static int init_bewalgo2(void)
{
	return 0;
}

static void exit_bewalgo2(void)
{
}

struct zbewalgo_alg alg_bewalgo2 = {
	.name = "bewalgo2",
	.flags = ZBEWALGO_ALG_FLAG_COMPRESS,
	.wrkmem_size = 8192,
	.init = init_bewalgo2,
	.exit = exit_bewalgo2,
	.compress = compress_bewalgo2,
	.decompress_safe = decompress_bewalgo2_safe,
	.decompress_fast = decompress_bewalgo2_fast
};
