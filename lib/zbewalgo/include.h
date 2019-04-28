/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 Benjamin Warnke <4bwarnke@informatik.uni-hamburg.de>
 */

#ifndef LIB_ZBEWALGO_INCLUDE_H
#define LIB_ZBEWALGO_INCLUDE_H

#include "linux/module.h"
#include "asm/unaligned.h"

#define ZBEWALGO_ALG_MAX_NAME 128
#define ZBEWALGO_ALG_FLAG_COMPRESS 1
#define ZBEWALGO_ALG_FLAG_TRANSFORM 2
#define ZBEWALGO_COMBINATION_MAX_IDS 7
#define ZBEWALGO_MAX_BASE_ALGORITHMS 16
#define ZBEWALGO_COMBINATION_MAX_ACTIVE 256
#define ZBEWALGO_BUFFER_SIZE 8192

/*
 * __KERNEL_DIV_ROUND_UP(..) is not used since shifting is faster than dividing
 * if the divisor is a power of 2.
 */
#define DIV_BY_8_ROUND_UP(val) ((val + 0x7) >> 3)

struct zbewalgo_alg {
	char name[ZBEWALGO_ALG_MAX_NAME];
	/* flag wheather this algorithm compresses the data or
	 * transforms the data only
	 */
	u8 flags;
	/* the wrkmem required for this algorithm */
	u32 wrkmem_size;
	int (*init)(void);
	void (*exit)(void);
	/* the compression function must store the size of
	 * input/output data itself
	 */
	int (*compress)(const u8 * const source, u8 * const dest,
			u16 * const wrkmem, const u16 source_length);
	int (*decompress_safe)(const u8 * const source, u8 * const dest,
			       u16 * const wrkmem, const u16 source_length);
	int (*decompress_fast)(const u8 * const source, u8 * const dest,
			       u16 * const wrkmem, const u16 source_length);
	u8 id;
};

/*
 * to gain speed the compression starts with the algorithm which was good for
 * the last compressed page.
 */
struct zbewalgo_combination {
	u8 count;
	u8 ids[ZBEWALGO_COMBINATION_MAX_IDS];
};

struct zbewalgo_main_data {
	/*
	 * best_id contains the id of the best combination for the last page
	 */
	u8 best_id;

	/*
	 * if zero search again for best_id - must be unsigned - underflow of
	 * values is intended
	 */
	u8 counter_search;

	/*
	 * a bit larger than original compressed size to be still accepted
	 * immediately
	 */
	u16 best_id_accepted_size;
};

/*
 * compression aborts automatically if the compressed data is too large.
 */
extern unsigned long zbewalgo_max_output_size;

/*
 * add a combination to the current active ones. All combinations are the
 * same for all instances of this algorithm
 */
int zbewalgo_add_combination(const char * const string,
			     const int string_length);

/*
 * replace ALL combinations with the one specified.
 */
int zbewalgo_set_combination(const char * const string,
			     const int string_length);

#endif
