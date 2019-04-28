/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 Benjamin Warnke <4bwarnke@informatik.uni-hamburg.de>
 */

#ifndef ZBEWALGO_BWT_H
#define ZBEWALGO_BWT_H

#include "include.h"

extern struct zbewalgo_alg alg_bwt;

/*
 * If more than the specified number of chars are to be transformed,
 * it is unlikely that the compression will achieve high ratios.
 * As a consequence the Burrows-Wheeler Transformation will abort if the number
 * of different bytes exeeds this value.
 */
extern unsigned long zbewalgo_bwt_max_alphabet;

#endif
