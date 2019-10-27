/* ureadahead
 *
 * Copyright © 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef UREADAHEAD_PACK_H
#define UREADAHEAD_PACK_H

#include <sys/types.h>

#include <limits.h>

#include <nih/macros.h>
#include <nih/list.h>


/**
 * PACK_PATH_MAX:
 *
 * PATH_MAX is far too long for our needs, we end up with multi-megabyte
 * pack files of \0s that take make up a significant portion of the data
 * we have to read.
 *
 * Long filenames are boring anyway.
 **/
#define PACK_PATH_MAX 255


typedef struct pack_path {
	int   group;
	ino_t ino;
	char  path[PACK_PATH_MAX+1];
} PackPath;

typedef struct pack_block {
	size_t pathidx;
	off_t  offset;
	off_t  length;
	off_t  physical;
} PackBlock;

typedef struct pack_file {
	dev_t      dev;
	int        rotational;
	size_t     num_groups;
	int *      groups;
	size_t     num_paths;
	PackPath * paths;
	size_t     num_blocks;
	PackBlock *blocks;
} PackFile;


typedef enum sort_option {
	SORT_OPEN,
	SORT_PATH,
	SORT_DISK,
	SORT_SIZE
} SortOption;


NIH_BEGIN_EXTERN

char *    pack_file_name            (const void *parent, const char *arg);
char *    pack_file_name_for_mount  (const void *parent, const char *mount);
char *    pack_file_name_for_device (const void *parent, dev_t dev);

PackFile *read_pack                 (const void *parent, const char *filename,
				     int dump);
int       write_pack                (const char *filename, PackFile *file);

void      pack_dump                 (PackFile *file, SortOption sort);

int       do_readahead              (PackFile *file, int daemonise);

NIH_END_EXTERN

#endif /* UREADAHEAD_PACK_H */

