/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 Benjamin Warnke <4bwarnke@informatik.uni-hamburg.de>
 */

#ifndef ZBEWALGO_INCLUDE_H
#define ZBEWALGO_INCLUDE_H

/*
 * zbewalgo_get_wrkmem_size must be used to determine the size which is
 * required for allocating the working memory for the compression and
 * decompression algorithm
 */
int zbewalgo_get_wrkmem_size(void);

/*
 * this function compresses the data given by 'source' into the
 * preallocated memory given by 'dest'.
 * The maximum allowed source_length is 4096 Bytes. If larger values are
 * given, the algorithm returns an error.
 * If the data is not compressible the function returns -1. Otherwise the
 * size of the compressed data is returned.
 * wrkmem must point to a memory region of at least the size returned by
 * 'zbewalgo_get_wrkmem_size'.
 */
int zbewalgo_compress(const u8 * const source, u8 * const dest,
		      u16 * const wrkmem, const u16 source_length);

/*
 * this function decompresses data which was already successfully compressed
 * with 'zbewalgo_compress'.
 * the function decompresses the data given by 'source' into the preallocated
 * buffer 'dest'.
 * wrkmem must point to a memory region of at least the size returned by
 * 'zbewalgo_get_wrkmem_size'.
 * the wrkmem for compression and decompression does not need to be the same
 * the function 'zbewalgo_decompress_safe' detects any errors in the given
 * compressed data and decompresses it safely.
 */
int zbewalgo_decompress_safe(const u8 * const source, u8 * const dest,
			     u16 * const wrkmem, const u16 source_length);

/*
 * like 'zbewalgo_decompress_safe', but all safeness branches are disabled at
 * compiletime which leads to a much higher decompression speed.
 */
int zbewalgo_decompress_fast(const u8 * const source, u8 * const dest,
			     u16 * const wrkmem, const u16 source_length);

#endif
