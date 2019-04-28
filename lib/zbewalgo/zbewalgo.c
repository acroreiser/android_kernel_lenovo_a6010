// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Benjamin Warnke <4bwarnke@informatik.uni-hamburg.de>
 *
 * zBeWalgo is a container compression algorithm, which can execute
 * multiple different compression and transformation algorithms after each
 * other. The execution of different compression algorithms after each other
 * will be called 'combination' in this description and in the code.
 * Additionally to be able to execute combinations of algorithms, zBeWalgo
 * can try different combinations on the same input. This allows high
 * compression ratios on completely different datasets, which would otherwise
 * require its own algorithm each. Executing all known combinations on each
 * input page would be very slow. Therefore the data is compressed at first
 * with that combination, which was already successful on the last input
 * page. If the compressed data size of the current page is similar to that
 * of the last page, the compressed data is returned immediately without even
 * trying the other combinations. Even if there is no guarantee that
 * consecutive calls to the algorithm belong to each other, the speed
 * improvement is obvious.
 *
 * ZRAM uses zsmalloc for the management of the compressed pages. The largest
 * size-class in zsmalloc is 3264 Bytes. If the compressed data is larger than
 * that threshold, ZRAM ignores the compression and writes the uncompressed
 * page instead. As a consequence it is useless to continue compression, if the
 * algorithm detects, that the data can not be compressed using the current
 * combination. The threshold for aborting compression can be changed via
 * sysfs at any time, even if the algorithm is currently in use. If a
 * combination fails to compress the data, zBeWalgo tries the next
 * combination. If no combination is able to reduce the data in size,
 * zBeWalgo returns a negative value.
 *
 * Each combination consists of up to 7 compression and transformation steps.
 * Combinations can be added and removed at any time via sysfs. Already
 * compressed Data can always be decompressed, even if the combination used
 * to produce it does not exist anymore. Technically the user could add up to
 * 256 combinations concurrently, but that would be very time consuming if
 * the data can not be compressed.
 *
 * To be able to build combinations and call different algorithms, all those
 * algorithms are implementing the same interface. This enables the user to
 * specify additional combinations while ZRAM is running.
 */

#include <linux/init.h>
#include <linux/zbewalgo.h>

#include "include.h"

#include "BWT.h"
#include "JBE.h"
#include "JBE2.h"
#include "MTF.h"
#include "RLE.h"
#include "bewalgo.h"
#include "bewalgo2.h"
#include "bitshuffle.h"
#include "huffman.h"

static atomic64_t zbewalgo_stat_combination[256];
static atomic64_t zbewalgo_stat_count[256];

unsigned long zbewalgo_max_output_size;

/*
 * all currently available combination sequences of algorithms
 */
static struct zbewalgo_combination
	zbewalgo_combinations[ZBEWALGO_COMBINATION_MAX_ACTIVE];
static u8 zbewalgo_combinations_count;

/*
 * maximum required wrkmem for compression and decompression for each instance
 * of the algorithm
 */
static u32 zbewalgo_wrkmem_size;

/*
 * compression can be aborted if the data is smaller than this threshold to
 * speed up the algorithm.
 */
static u16 zbewalgo_early_abort_size;

/*
 * each cpu has its own independent compression history to avoid locks
 */
static struct zbewalgo_main_data __percpu *zbewalgo_main_data_ptr;

/*
 * all available algorithms
 */
static struct zbewalgo_alg
	zbewalgo_base_algorithms[ZBEWALGO_MAX_BASE_ALGORITHMS];
static u8 zbewalgo_base_algorithms_count;

/*
 * returns the required size of wrkmem for compression and decompression
 */
int zbewalgo_get_wrkmem_size(void)
{
	return zbewalgo_wrkmem_size;
}
EXPORT_SYMBOL(zbewalgo_get_wrkmem_size);

/*
 * this function adds a combination to the set of enabled combinations or
 * if 'flag' is set, replaces all combinations with the newly supplied one.
 * this function is called from the sysfs context, and therefore accepts
 * a string as input.
 */
static __always_inline int add_set_combination(const char * const string,
					       const int string_length,
					       int flag)
{
	/* points behind the string for fast looping over the entire string */
	const char * const string_end = string + string_length;
	/* the string up to 'anchor' is parsed */
	const char *anchor = string;
	const char *pos = string;
	struct zbewalgo_combination combination;
	u8 i;

	memset(&combination, 0, sizeof(struct zbewalgo_combination));
	/* loop over entire string */
	while ((pos < string_end) && (*pos != 0)) {
		while ((pos < string_end)
		       && (*pos != 0)
		       && (*pos != '-')
		       && (*pos != '\n'))
			pos++;
		if (pos - anchor <= 0) {
			/* skipp leading or consecutive '-' chars */
			pos++;
			anchor = pos;
			continue;
		}
		/* find the algorithm by name in the list of all algorithms */
		for (i = 0; i < zbewalgo_base_algorithms_count; i++) {
			if (((unsigned int)(pos - anchor)
			     == strlen(zbewalgo_base_algorithms[i].name))
			    && (memcmp(anchor,
				       zbewalgo_base_algorithms[i].name,
				       pos - anchor)
					 == 0)) {
				/* found algorithm */
				combination.ids[combination.count++] =
					zbewalgo_base_algorithms[i].id;
				break;
			}
		}
		/*
		 * abort parsing if maximum of algorithms reached or
		 * if string is parsed completely
		 */
		if (combination.count >= ZBEWALGO_COMBINATION_MAX_IDS
		    || (*pos != '-'))
			goto _finalize;
		if (i == zbewalgo_base_algorithms_count)
			/* misstyped arguments */
			return -EINVAL;
		pos++;
		anchor = pos;
	}
_finalize:
	if (combination.count) {
		/* if combination has at least 1 algorithm */
		if (flag == 1)
			zbewalgo_combinations_count = 0;
		/* don't store the same combination twice */
		for (i = 0; i < zbewalgo_combinations_count; i++)
			if (memcmp(&zbewalgo_combinations[i], &combination,
				   sizeof(struct zbewalgo_combination)) == 0) {
				return 0;
			}
		/* store the new combination */
		memcpy(&zbewalgo_combinations[zbewalgo_combinations_count],
		       &combination, sizeof(struct zbewalgo_combination));
		zbewalgo_combinations_count++;
		return 0;
	}
	/* empty algorithm is not allowed */
	return -EINVAL;
}

int zbewalgo_add_combination(const char * const string,
			     const int string_length)
{
	return add_set_combination(string, string_length, 0);
}
EXPORT_SYMBOL(zbewalgo_add_combination);

int zbewalgo_set_combination(const char * const string,
			     const int string_length)
{
	return add_set_combination(string, string_length, 1);
}
EXPORT_SYMBOL(zbewalgo_set_combination);

int zbewalgo_compress(const u8 * const source, u8 * const dest,
		      u16 * const wrkmem, const u16 source_length)
{
	struct zbewalgo_main_data * const main_data =
		get_cpu_ptr(zbewalgo_main_data_ptr);
	u16 * const wrkmem1 = PTR_ALIGN(wrkmem, 8);
	u8 *dest_best = (u8 *)wrkmem1;
	u8 *dest1 = (u8 *)(wrkmem1 + 4096);
	u8 *dest2 = (u8 *)(wrkmem1 + 4096 * 2);
	u16 * const wrk = wrkmem1 + 4096 * 3;
	u8 *dest_tmp;
	const u8 *current_source;
	u8 i, j;
	u16 dest_best_size = ZBEWALGO_BUFFER_SIZE;
	int dest_current_size;
	u8 dest_best_id = 0;
	u8 i_from = main_data->best_id
		+ (main_data->counter_search-- == 0);
	u8 i_to = zbewalgo_combinations_count;
	u8 looped = 0;
	u16 local_abort_size = max_t(u16,
		main_data->best_id_accepted_size,
		zbewalgo_early_abort_size);
	u16 counter = 0;
	struct zbewalgo_alg *alg;
	struct zbewalgo_combination * const dest_best_combination =
		(struct zbewalgo_combination *)dest;
	u8 k;

	if (source_length > 4096) {
		/*
		 * This algorithm is optimized for small data buffers
		 * and can not handle larger inputs.
		 */
		return -EINVAL;
	}
_begin:
	/*
	 * loop over zbewalgo_combinations_count starting with the last
	 * successful combination
	 */
	i = i_from;
	while (i < i_to) {
		current_source	  = source;
		dest_current_size = source_length;
		counter++;
		for (j = 0; j < zbewalgo_combinations[i].count; j++) {
			k = zbewalgo_combinations[i].ids[j];
			alg =  &zbewalgo_base_algorithms[k];
			dest_current_size = alg->compress(current_source,
							  dest2, wrk,
							  dest_current_size);
			if (dest_current_size <= 0)
				goto _next_algorithm;
			current_source = dest2;
			dest_tmp = dest2;
			dest2 = dest1;
			dest1 = dest_tmp;
			if (dest_current_size < dest_best_size) {
				/*
				 * if a higher compression ratio is found,
				 * update to the best
				 */
				dest_best_id = i;
				dest_best_size = dest_current_size;
				dest_tmp = dest_best;
				dest_best = dest1;
				dest1 = dest_tmp;
				memcpy(dest,
				       &zbewalgo_combinations[i],
				       sizeof(struct zbewalgo_combination));
				/*
				 * partial combination is allowed, if its
				 * compression ratio is high
				 */
				dest_best_combination->count = j;
			}
		}
_next_algorithm:
		if (dest_best_size < local_abort_size)
			goto _early_abort;
		local_abort_size = zbewalgo_early_abort_size;
		i++;
	}
	if (!(looped++) && i_from > 0) {
		i_to = min_t(u8, i_from, zbewalgo_combinations_count);
		i_from = 0;
		goto _begin;
	}
	if (dest_best_size > zbewalgo_max_output_size) {
		/* not compressible */
		return -EINVAL;
	}
_early_abort:
	atomic64_inc(&zbewalgo_stat_combination[dest_best_id]);
	atomic64_inc(&zbewalgo_stat_count[counter]);
	main_data->best_id = dest_best_id;
	main_data->best_id_accepted_size =
		max_t(u16,
		      dest_best_size + (dest_best_size >> 3),
			zbewalgo_early_abort_size);
	main_data->best_id_accepted_size =
		min_t(u16,
		      main_data->best_id_accepted_size,
			zbewalgo_max_output_size);
	memcpy(dest + sizeof(struct zbewalgo_combination),
	       dest_best, dest_best_size);
	return sizeof(struct zbewalgo_combination) + dest_best_size;
}
EXPORT_SYMBOL(zbewalgo_compress);

static __always_inline int zbewalgo_decompress(const u8 * const source,
					       u8 * const dest,
					       u16 * const wrkmem,
					       const u16 source_length,
					       const int safe_mode)
{
	const u16 s_length = source_length
			     - sizeof(struct zbewalgo_combination);
	u16 * const wrkmem1 = PTR_ALIGN(wrkmem, 8);
	u8 *dest1 = (u8 *)wrkmem1;
	u8 *dest2 = (u8 *)(wrkmem1 + 4096);
	u16 *wrk = wrkmem1 + 4096 * 2;
	const struct zbewalgo_combination * const combination =
		(struct zbewalgo_combination *)source;
	u8 j;
	short res;
	struct zbewalgo_alg *alg;
	int (*decompress)(const u8 * const source, u8 * const dest,
			  u16 * const wrkmem, const u16 source_length);
	u8 count = combination->count + 1;

	if (safe_mode && unlikely(s_length > 4096))
		return -EINVAL;
	if (safe_mode && unlikely(count > 7))
		return -EINVAL;

	if (count == 1) {
		if (safe_mode
		    && unlikely(combination->ids[0]
				>= zbewalgo_base_algorithms_count))
			return -EINVAL;
		alg = &zbewalgo_base_algorithms[combination->ids[0]];
		if (safe_mode)
			decompress = alg->decompress_safe;
		else
			decompress = alg->decompress_fast;
		res = decompress(source + sizeof(struct zbewalgo_combination),
				 dest, wrk, s_length);
		return res;
	}
	if (safe_mode && unlikely(combination->ids[count - 1]
	    >= zbewalgo_base_algorithms_count))
		return -EINVAL;
	alg = &zbewalgo_base_algorithms[combination->ids[count - 1]];
		if (safe_mode)
			decompress = alg->decompress_safe;
		else
			decompress = alg->decompress_fast;
	res = decompress(source + sizeof(struct zbewalgo_combination),
			 dest1, wrk, s_length);
	if (res < 0)
		return res;
	for (j = count - 2; j > 1; j -= 2) {
		if (safe_mode
		    && unlikely(combination->ids[j]
				>= zbewalgo_base_algorithms_count))
			return -EINVAL;
		alg =  &zbewalgo_base_algorithms[combination->ids[j]];
		if (safe_mode)
			decompress = alg->decompress_safe;
		else
			decompress = alg->decompress_fast;
		res = decompress(dest1, dest2, wrk, res);
		if (res < 0)
			return res;
		if (safe_mode
		    && unlikely(combination->ids[j - 1]
				>= zbewalgo_base_algorithms_count))
			return -EINVAL;
		alg =  &zbewalgo_base_algorithms[combination->ids[j - 1]];
		if (safe_mode)
			decompress = alg->decompress_safe;
		else
			decompress = alg->decompress_fast;
		res = decompress(dest2, dest1, wrk, res);
		if (res < 0)
			return res;
	}
	if (j == 1) {
		if (safe_mode
		    && unlikely(combination->ids[1]
				>= zbewalgo_base_algorithms_count))
			return -EINVAL;
		alg =  &zbewalgo_base_algorithms[combination->ids[1]];
		if (safe_mode)
			decompress = alg->decompress_safe;
		else
			decompress = alg->decompress_fast;
		res = decompress(dest1, dest2, wrk, res);
		if (res < 0)
			return res;
		if (safe_mode
		    && unlikely(combination->ids[0]
				>= zbewalgo_base_algorithms_count))
			return -EINVAL;
		alg =  &zbewalgo_base_algorithms[combination->ids[0]];
		if (safe_mode)
			decompress = alg->decompress_safe;
		else
			decompress = alg->decompress_fast;
		res = decompress(dest2, dest, wrk, res);
		return res;
	}
	if (safe_mode
	    && unlikely(combination->ids[0]
			>= zbewalgo_base_algorithms_count))
		return -EINVAL;
	alg =  &zbewalgo_base_algorithms[combination->ids[0]];
		if (safe_mode)
			decompress = alg->decompress_safe;
		else
			decompress = alg->decompress_fast;
	res = decompress(dest1, dest, wrk, res);
	return res;
}

int zbewalgo_decompress_safe(const u8 * const source, u8 * const dest,
			     u16 * const wrkmem, const u16 source_length)
{
	return zbewalgo_decompress(source, dest, wrkmem, source_length, 1);
}
EXPORT_SYMBOL(zbewalgo_decompress_safe);
int zbewalgo_decompress_fast(const u8 * const source, u8 * const dest,
			     u16 * const wrkmem, const u16 source_length)
{
	return zbewalgo_decompress(source, dest, wrkmem, source_length, 0);
}
EXPORT_SYMBOL(zbewalgo_decompress_fast);

#define add_combination_compile_time(name) \
	zbewalgo_add_combination(name, sizeof(name))

static ssize_t zbewalgo_combinations_show(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  char *buf)
{
	ssize_t res = 0;
	ssize_t tmp;
	u8 i, j;
	struct zbewalgo_combination *com;

	tmp = scnprintf(buf, PAGE_SIZE - res, "combinations={\n");
	res = tmp;
	buf += tmp;
	for (i = 0; i < zbewalgo_combinations_count; i++) {
		com = &zbewalgo_combinations[i];
		res += tmp = scnprintf(buf, PAGE_SIZE - res,
				       "\tcombination[%d]=", i);
		buf += tmp;
		for (j = 0; j < com->count - 1; j++) {
			res += tmp = scnprintf(buf, PAGE_SIZE - res, "%s-",
				zbewalgo_base_algorithms[com->ids[j]].name);
			buf += tmp;
		}
		res += tmp = scnprintf(buf, PAGE_SIZE - res, "%s\n",
			zbewalgo_base_algorithms[com->ids[j]].name);
		buf += tmp;
	}
	res += tmp = scnprintf(buf, PAGE_SIZE - res, "}\n");
	return res;
}

static __always_inline void zbewalgo_combinations_reset(void)
{
	zbewalgo_combinations_count = 0;
	add_combination_compile_time("bwt-mtf-huffman-jbe-rle");
	add_combination_compile_time("bitshuffle-rle-bitshuffle-rle");
	add_combination_compile_time("bewalgo2-bitshuffle-rle");
	add_combination_compile_time("bitshuffle-jbe-mtf-huffman-jbe");
	add_combination_compile_time("bitshuffle-bewalgo2-mtf-bewalgo-jbe2");
	add_combination_compile_time("mtf-bewalgo-huffman-jbe-rle");
	add_combination_compile_time("jbe-rle-bitshuffle-rle");
	add_combination_compile_time("mtf-mtf-jbe-jbe-rle");
	add_combination_compile_time("jbe2-bitshuffle-rle");
	add_combination_compile_time("jbe-mtf-jbe-rle");
//	add_combination_compile_time("bewalgo2-bitshuffle-jbe-rle");
//	add_combination_compile_time("bwt-mtf-bewalgo-huffman");
//	add_combination_compile_time("bitshuffle-bewalgo2-mtf-bewalgo-jbe");
//	add_combination_compile_time("bitshuffle-rle-bitshuffle-rle");
}

static ssize_t zbewalgo_combinations_store(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	ssize_t res;

	if (count < 5)
		return -EINVAL;
	if (memcmp(buf, "add ", 4) == 0) {
		res = zbewalgo_add_combination(buf + 4, count - 4);
		return res < 0 ? res : count;
	}
	if (memcmp(buf, "set ", 4) == 0) {
		res = zbewalgo_set_combination(buf + 4, count - 4);
		return res < 0 ? res : count;
	}
	if (memcmp(buf, "reset", 5) == 0) {
		zbewalgo_combinations_reset();
		return count;
	}
	return -EINVAL;
}

static ssize_t zbewalgo_max_output_size_show(struct kobject *kobj,
					     struct kobj_attribute *attr,
					     char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%lu", zbewalgo_max_output_size);
}

static ssize_t zbewalgo_max_output_size_store(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      const char *buf, size_t count)
{
	char localbuf[10];
	ssize_t res;
	unsigned long tmp;

	memcpy(&localbuf[0], buf, count < 10 ? count : 10);
	localbuf[count < 9 ? count : 9] = 0;
	res = kstrtoul(localbuf, 10, &tmp);
	if (tmp <= 4096 - sizeof(struct zbewalgo_combination)) {
		zbewalgo_max_output_size = tmp;
		if (zbewalgo_max_output_size < zbewalgo_early_abort_size)
			zbewalgo_early_abort_size
				= zbewalgo_max_output_size >> 1;
	} else {
		return -EINVAL;
	}
	return res < 0 ? res : count;
}

static ssize_t zbewalgo_early_abort_size_show(struct kobject *kobj,
					      struct kobj_attribute *attr,
char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u", zbewalgo_early_abort_size);
}

static ssize_t zbewalgo_early_abort_size_store(struct kobject *kobj,
					       struct kobj_attribute *attr,
					       const char *buf, size_t count)
{
	char localbuf[10];
	ssize_t res;
	unsigned long tmp;

	memcpy(&localbuf[0], buf, count < 10 ? count : 10);
	localbuf[count < 9 ? count : 9] = 0;
	res = kstrtoul(localbuf, 10, &tmp);
	if (tmp <= zbewalgo_max_output_size)
		zbewalgo_early_abort_size = tmp;
	else
		return -EINVAL;
	return res < 0 ? res : count;
}

static ssize_t zbewalgo_bwt_max_alphabet_show(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%lu", zbewalgo_bwt_max_alphabet);
}

static ssize_t zbewalgo_bwt_max_alphabet_store(struct kobject *kobj,
					       struct kobj_attribute *attr,
					       const char *buf, size_t count)
{
	char localbuf[10];
	ssize_t res;

	memcpy(&localbuf[0], buf, count < 10 ? count : 10);
	localbuf[count < 9 ? count : 9] = 0;
	res = kstrtoul(localbuf, 10, &zbewalgo_bwt_max_alphabet);
	return res < 0 ? res : count;
}

static struct kobj_attribute zbewalgo_combinations_attribute =
	__ATTR(combinations, 0664, zbewalgo_combinations_show,
	       zbewalgo_combinations_store);
static struct kobj_attribute zbewalgo_max_output_size_attribute =
	__ATTR(max_output_size, 0664, zbewalgo_max_output_size_show,
	       zbewalgo_max_output_size_store);
static struct kobj_attribute zbewalgo_early_abort_size_attribute =
	__ATTR(early_abort_size, 0664, zbewalgo_early_abort_size_show,
	       zbewalgo_early_abort_size_store);
static struct kobj_attribute zbewalgo_bwt_max_alphabet_attribute =
	__ATTR(bwt_max_alphabet, 0664, zbewalgo_bwt_max_alphabet_show,
	       zbewalgo_bwt_max_alphabet_store);
static struct attribute *attrs[] = {
	&zbewalgo_combinations_attribute.attr,
	&zbewalgo_max_output_size_attribute.attr,
	&zbewalgo_early_abort_size_attribute.attr,
	&zbewalgo_bwt_max_alphabet_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static struct kobject *zbewalgo_kobj;

static int __init zbewalgo_mod_init(void)
{
	u16 i;
	int j;
	int res = 0;

	zbewalgo_early_abort_size = 400;
	/*
	 * this algorithm is intended to be used for zram with zsmalloc.
	 * Therefore zbewalgo_max_output_size equals zsmalloc max size class
	 */
	i = 0;
	zbewalgo_max_output_size =
		3264 /* largest reported size_class by zsmalloc */
		 - (sizeof(unsigned long)) /* zsmalloc internal overhead */
		 - sizeof(struct zbewalgo_combination);/* zbewalgo overhead */
	zbewalgo_base_algorithms[i++] = alg_bewalgo;
	zbewalgo_base_algorithms[i++] = alg_bewalgo2;
	zbewalgo_base_algorithms[i++] = alg_bitshuffle;
	zbewalgo_base_algorithms[i++] = alg_bwt;
	zbewalgo_base_algorithms[i++] = alg_jbe;
	zbewalgo_base_algorithms[i++] = alg_jbe2;
	zbewalgo_base_algorithms[i++] = alg_mtf;
	zbewalgo_base_algorithms[i++] = alg_rle;
	zbewalgo_base_algorithms[i++] = alg_huffman;
	zbewalgo_base_algorithms_count = i;
	/*
	 * the wrkmem size is the largest wrkmem required by any callable
	 * algorithm
	 */
	zbewalgo_wrkmem_size = 0;
	for (i = 0; i < zbewalgo_base_algorithms_count; i++) {
		res = zbewalgo_base_algorithms[i].init();
		if (res < 0) {
			if (i > 0)
				zbewalgo_base_algorithms[0].exit();
			i--;
			while (i > 0)
				zbewalgo_base_algorithms[i].exit();
			return res;
		}
		zbewalgo_base_algorithms[i].id = i;
		zbewalgo_wrkmem_size = max_t(u32,
					     zbewalgo_wrkmem_size,
			zbewalgo_base_algorithms[i].wrkmem_size);
	}
	/* adding some pages for temporary compression results */
	zbewalgo_wrkmem_size += 4096 * 6;
	zbewalgo_wrkmem_size += 8;/* for alignment */
	zbewalgo_main_data_ptr = alloc_percpu(struct zbewalgo_main_data);
	for_each_possible_cpu(j) {
		memset(per_cpu_ptr(zbewalgo_main_data_ptr, j),
		       0, sizeof(struct zbewalgo_main_data));
	}
	for (i = 0; i < 256; i++) {
		atomic64_set(&zbewalgo_stat_combination[i], 0);
		atomic64_set(&zbewalgo_stat_count[i], 0);
	}

	zbewalgo_kobj = kobject_create_and_add("zbewalgo", kernel_kobj);
	if (!zbewalgo_kobj) {
		/* allocation error */
		return -ENOMEM;
	}
	res = sysfs_create_group(zbewalgo_kobj, &attr_group);
	if (res)
		kobject_put(zbewalgo_kobj);
	zbewalgo_combinations_reset();
	return res;
}

void static __exit zbewalgo_mod_fini(void)
{
	u16 i;
	u64 tmp;

	kobject_put(zbewalgo_kobj);
	for (i = 0; i < zbewalgo_base_algorithms_count; i++)
		zbewalgo_base_algorithms[i].exit();
	free_percpu(zbewalgo_main_data_ptr);
	/* log statistics via printk for debugging purpose */
	for (i = 0; i < 256; i++) {
		tmp = atomic64_read(&zbewalgo_stat_combination[i]);
		if (tmp > 0)
			printk(KERN_INFO "%s %4d -> %llu combination\n",
			       __func__, i, tmp);
	}
	for (i = 0; i < 256; i++) {
		tmp = atomic64_read(&zbewalgo_stat_count[i]);
		if (tmp > 0)
			printk(KERN_INFO "%s %4d -> %llu counter\n",
			       __func__, i, tmp);
	}
}

module_init(zbewalgo_mod_init);
module_exit(zbewalgo_mod_fini);

MODULE_AUTHOR("Benjamin Warnke <4bwarnke@informatik.uni-hamburg.de>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("zBeWalgo Compression Algorithm");
