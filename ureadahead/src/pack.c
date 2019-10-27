/* ureadahead
 *
 * pack.c - pack file handling
 *
 * Copyright © 2009 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <sys/resource.h>

#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <blkid.h>
#include <ext2fs.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "pack.h"
#include "values.h"
#include "file.h"
#include "errors.h"


/* From linux/ioprio.h */
#define IOPRIO_CLASS_SHIFT 13

#define IOPRIO_CLASS_RT    1
#define IOPRIO_CLASS_IDLE  3

#define IOPRIO_WHO_PROCESS 1

#define IOPRIO_RT_HIGHEST  (0 | (IOPRIO_CLASS_RT << IOPRIO_CLASS_SHIFT))
#define IOPRIO_IDLE_LOWEST (7 | (IOPRIO_CLASS_IDLE << IOPRIO_CLASS_SHIFT))


/**
 * PATH_PACKDIR:
 *
 * Path to the directory in which we write our pack files.
 **/
#define PATH_PACKDIR "/data/ureadahead"

/**
 * NUM_THREADS:
 *
 * Number of threads to use when reading on an SSD.
 **/
#define NUM_THREADS 4


typedef enum pack_flags {
	PACK_ROTATIONAL = 0x01,
} PackFlags;


/* Prototypes for static functions */
static void  print_time          (const char *message, struct timespec *start);
static int   do_readahead_hdd    (PackFile *file, int daemonise);
static void  preload_inode_group (ext2_filsys fs, int group);
static int   do_readahead_ssd    (PackFile *file, int daemonise);
static void *ra_thread           (void *ptr);


char *
pack_file_name (const void *parent,
		const char *arg)
{
	struct stat statbuf;

	/* If we're not given an argument, fall back to the root pack */
	if (! arg)
		return NIH_MUST (nih_strdup (NULL, PATH_PACKDIR "/pack"));

	/* Stat the path given, if it was a file, just return that as the
	 * filename.
	 */
	if (stat (arg, &statbuf) < 0)
		nih_return_system_error (NULL);

	if (S_ISREG (statbuf.st_mode))
		return NIH_MUST (nih_strdup (NULL, arg));

	/* Otherwise treat it as a mountpoint name */
	return pack_file_name_for_mount (parent, arg);
}

char *
pack_file_name_for_mount (const void *parent,
			  const char *mount)
{
	char *file;

	nih_assert (mount != NULL);

	/* Strip the initial slash, if it's the root mountpoint, just return
	 * the default pack filename.
	 */
	if (mount[0] == '/')
		mount++;
	if (mount[0] == '\0')
		return NIH_MUST (nih_strdup (NULL, PATH_PACKDIR "/pack"));

	/* Prepend the mount point to the extension, and replace extra /s
	 * with periods.
	 */
	file = NIH_MUST (nih_sprintf (parent, "%s/%s.pack",
				      PATH_PACKDIR, mount));

	for (char *ptr = file + strlen (PATH_PACKDIR) + 1; *ptr; ptr++)
		if (*ptr == '/')
			*ptr = '.';

	return file;
}

char *
pack_file_name_for_device (const void *parent,
			   dev_t       dev)
{
	FILE *fp;
	char *line;

	fp = fopen ("/proc/self/mountinfo", "r");
	if (! fp)
		nih_return_system_error (NULL);

	while ((line = fgets_alloc (NULL, fp)) != NULL) {
		char *       saveptr;
		char *       ptr;
		char *       device;
		unsigned int maj;
		unsigned int min;
		char *       mount;
		struct stat  statbuf;

		/* mount ID */
		ptr = strtok_r (line, " \t\n", &saveptr);
		if (! ptr) {
			nih_free (line);
			continue;
		}

		/* parent ID */
		ptr = strtok_r (NULL, " \t\n", &saveptr);
		if (! ptr) {
			nih_free (line);
			continue;
		}

		/* major:minor */
		device = strtok_r (NULL, " \t\n", &saveptr);
		if (! ptr) {
			nih_free (line);
			continue;
		}

		/* root */
		ptr = strtok_r (NULL, " \t\n", &saveptr);
		if (! ptr) {
			nih_free (line);
			continue;
		}

		/* mount point */
		mount = strtok_r (NULL, " \t\n", &saveptr);
		if (! mount) {
			nih_free (line);
			continue;
		}

		/* Check whether this is the right device */
		if (stat (mount, &statbuf) || statbuf.st_dev != dev) {
			nih_free (line);
			continue;
		}

		/* Done, convert the mountpoint to a pack filename */
		if (fclose (fp) < 0)
			nih_return_system_error (NULL);

		return pack_file_name_for_mount (parent, mount);
	}

	if (fclose (fp) < 0)
		nih_return_system_error (NULL);

	/* Fell through, can't generate pack file */
	errno = ENOENT;
	nih_return_system_error (NULL);
}


PackFile *
read_pack (const void *parent,
	   const char *filename,
	   int         dump)
{
	struct timespec start;
	FILE *          fp;
	struct stat     stat;
	PackFile *      file;
	char            hdr[8];
	time_t          created;
	char            buf[80];

	nih_assert (filename != NULL);

	clock_gettime (CLOCK_MONOTONIC, &start);

	/* Open the file, and then allocate the PackFile structure for it. */
	fp = fopen (filename, "r");
	if (! fp)
		nih_return_system_error (NULL);

	/* Obvious really... */
	if (fstat (fileno (fp), &stat) == 0)
		readahead (fileno (fp), 0, stat.st_size);

	file = NIH_MUST (nih_new (parent, PackFile));

	/* Read and verify the header */
	if (fread (hdr, 1, 8, fp) < 8) {
		nih_debug ("Short read of header");
		goto error;
	}

	if ((hdr[0] != 'u')
	    || (hdr[1] != 'r')
	    || (hdr[2] != 'a')) {
		nih_debug ("Header format error");
		goto error;
	}

	if (hdr[3] != 2) {
		nih_debug ("Pack version error");
		goto error;
	}

	file->rotational = (hdr[4] & PACK_ROTATIONAL ? TRUE : FALSE);

	if (fread (&file->dev, sizeof file->dev, 1, fp) < 1) {
		nih_debug ("Short read of device number");
		goto error;
	}

	if (fread (&created, sizeof created, 1, fp) < 1) {
		nih_debug ("Short read of creation time");
		goto error;
	}

	/* If the file is too old, close and ignore it */
	if ((! dump) && (created < (time (NULL) - 86400 * 365))) {
		nih_error_raise (PACK_TOO_OLD, _(PACK_TOO_OLD_STR));
		nih_free (file);
		fclose (fp);
		return NULL;
	}

	strftime (buf, sizeof buf, "%a, %d %b %Y %H:%M:%S %z",
		  gmtime (&created));
	nih_log_message (dump ? NIH_LOG_MESSAGE : NIH_LOG_INFO,
			 "%s: created %s for %s %d:%d", filename, buf,
			 file->rotational ? "hdd" : "ssd",
			 major (file->dev), minor (file->dev));


	/* Read in the number of group entries */
	if (fread (&file->num_groups, sizeof file->num_groups, 1, fp) < 1) {
		nih_debug ("Short read of number of group entries");
		goto error;
	}

	file->groups = NIH_MUST (nih_alloc (file, sizeof (int) * file->num_groups));

	/* Read in the group entries */
	if (fread (file->groups, sizeof (int), file->num_groups, fp) < file->num_groups) {
		nih_debug ("Short read of group entries");
		goto error;
	}

	/* Read in the number of path entries */
	if (fread (&file->num_paths, sizeof file->num_paths, 1, fp) < 1) {
		nih_debug ("Short read of number of path entries");
		goto error;
	}

	file->paths = NIH_MUST (nih_alloc (file, sizeof (PackPath) * file->num_paths));

	/* Read in the path entries */
	if (fread (file->paths, sizeof (PackPath), file->num_paths, fp) < file->num_paths) {
		nih_debug ("Short read of path entries");
		goto error;
	}

	/* Read in the number of block entries */
	if (fread (&file->num_blocks, sizeof file->num_blocks, 1, fp) < 1) {
		nih_debug ("Short read of number of block entries");
		goto error;
	}

	file->blocks = NIH_MUST (nih_alloc (file, sizeof (PackBlock) * file->num_blocks));

	/* Read in the block entries */
	if (fread (file->blocks, sizeof (PackBlock), file->num_blocks, fp) < file->num_blocks) {
		nih_debug ("Short read of block entries");
		goto error;
	}

	if ((nih_log_priority <= NIH_LOG_INFO) || dump) {
		off_t bytes;

		bytes = 0;
		for (size_t i = 0; i < file->num_blocks; i++)
			bytes += file->blocks[i].length;

		nih_log_message (dump ? NIH_LOG_MESSAGE : NIH_LOG_INFO,
				 "%zu inode groups, %zu files, %zu blocks (%zu kB)",
				 file->num_groups, file->num_paths, file->num_blocks,
				 (size_t)bytes / 1024);
	}

	/* Done */
	if (fclose (fp) < 0) {
		nih_error_raise_system ();
		nih_free (file);
		return NULL;
	}

	print_time ("Read pack", &start);

	return file;
error:
	nih_error_raise (PACK_DATA_ERROR, _(PACK_DATA_ERROR_STR));
	nih_free (file);
	fclose (fp);
	return NULL;
}

int
write_pack (const char *filename,
	    PackFile *  file)
{
	int    fd;
	FILE * fp;
	char   hdr[8];
	time_t now;

	nih_assert (filename != NULL);
	nih_assert (file != NULL);

	/* Open the file, making sure we truncate it and give it a
	 * sane mode
	 */
	fd = open (filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		nih_return_system_error (-1);

	fp = fdopen (fd, "w");
	if (! fp) {
		nih_error_raise_system ();
		close (fd);
		return -1;
	}

	/* Write out the header */
	hdr[0] = 'u';
	hdr[1] = 'r';
	hdr[2] = 'a';

	hdr[3] = 2;

	hdr[4] = 0;
	hdr[4] |= file->rotational ? PACK_ROTATIONAL : 0;

	hdr[5] = hdr[6] = hdr[7] = 0;

	if (fwrite (hdr, 1, 8, fp) < 8)
		goto error;

	if (fwrite (&file->dev, sizeof file->dev, 1, fp) < 1)
		goto error;

	time (&now);
	if (fwrite (&now, sizeof now, 1, fp) < 1)
		goto error;

	/* Write out the number of group entries */
	if (fwrite (&file->num_groups, sizeof file->num_groups, 1, fp) < 1)
		goto error;

	/* Write out the group entries */
	if (fwrite (file->groups, sizeof (int), file->num_groups, fp) < file->num_groups)
		goto error;

	/* Write out the number of path entries */
	if (fwrite (&file->num_paths, sizeof file->num_paths, 1, fp) < 1)
		goto error;

	/* Write out the path entries */
	if (fwrite (file->paths, sizeof (PackPath), file->num_paths, fp) < file->num_paths)
		goto error;

	/* Write out the number of block entries */
	if (fwrite (&file->num_blocks, sizeof file->num_blocks, 1, fp) < 1)
		goto error;

	/* Write out the block entries */
	if (fwrite (file->blocks, sizeof (PackBlock), file->num_blocks, fp) < file->num_blocks)
		goto error;

	if (nih_log_priority <= NIH_LOG_INFO) {
		off_t bytes;

		bytes = 0;
		for (size_t i = 0; i < file->num_blocks; i++)
			bytes += file->blocks[i].length;

		nih_info ("%zu inode groups, %zu files, %zu blocks (%zu kB)",
			  file->num_groups, file->num_paths, file->num_blocks,
			  (size_t)bytes / 1024);
	}

	/* Flush, sync and close */
	if ((fflush (fp) < 0)
	    || (fsync (fd) < 0)
	    || (fclose (fp) < 0))
		goto error;

	return 0;
error:
	nih_error_raise_system ();
	fclose (fp);
	return -1;
}

static void
print_time (const char *     message,
	    struct timespec *start)
{
 	struct timespec end;
 	struct timespec span;

	nih_assert (message != NULL);
	nih_assert (start != NULL);

	clock_gettime (CLOCK_MONOTONIC, &end);

	span.tv_sec = end.tv_sec - start->tv_sec;
	span.tv_nsec = end.tv_nsec - start->tv_nsec;

	if (span.tv_nsec < 0) {
		span.tv_sec--;
		span.tv_nsec += 1000000000;
	}

	nih_info ("%s: %ld.%03lds", message,
		  span.tv_sec, span.tv_nsec / 1000000);

	start->tv_sec = end.tv_sec;
	start->tv_nsec = end.tv_nsec;
}


struct pack_sort {
	size_t    idx;
	PackPath *path;
	off_t     sort;
};

int
pack_sort_compar (const void *a,
		  const void *b)
{
	const struct pack_sort *ps_a;
	const struct pack_sort *ps_b;

	nih_assert (a != NULL);
	nih_assert (b != NULL);

	ps_a = a;
	ps_b = b;

	if (ps_a->sort < ps_b->sort) {
		return -1;
	} else if (ps_a->sort > ps_b->sort) {
		return 1;
	} else {
		return strcmp (ps_a->path->path, ps_b->path->path);
	}
}

void
pack_dump (PackFile * file,
	   SortOption sort)
{
	nih_local struct pack_sort *pack = NULL;
	int                         page_size;

	nih_assert (file != NULL);

	page_size = sysconf (_SC_PAGESIZE);

	/* Sort the pack file before we dump it */
	pack = NIH_MUST (nih_alloc (NULL, (sizeof (struct pack_sort)
					   * file->num_paths)));

	for (size_t i = 0; i < file->num_paths; i++) {
		pack[i].idx = i;
		pack[i].path = &file->paths[i];

		switch (sort) {
		case SORT_OPEN:
		case SORT_PATH:
			pack[i].sort = 0;
			break;
		case SORT_DISK:
			pack[i].sort = LLONG_MAX;
			for (size_t j = 0; j < file->num_blocks; j++) {
				if (file->blocks[j].pathidx != pack[i].idx)
					continue;

				pack[i].sort = file->blocks[j].physical;
				break;
			}
			break;
		case SORT_SIZE:
			pack[i].sort = 0;
			for (size_t j = 0; j < file->num_blocks; j++) {
				if (file->blocks[j].pathidx != pack[i].idx)
					continue;

				pack[i].sort += file->blocks[j].length;
			}
			break;
		default:
			nih_assert_not_reached ();
		}
	}

	if (sort != SORT_OPEN)
		qsort (pack, file->num_paths, sizeof (struct pack_sort),
		       pack_sort_compar);

	/* Iterated the sorted pack */
	for (size_t i = 0; i < file->num_paths; i++) {
		struct stat     statbuf;
		off_t           num_pages;
		size_t          block_count;
		off_t           block_bytes;
		nih_local char *buf = NULL;
		char *          ptr;

		if (stat (pack[i].path->path, &statbuf) < 0) {
			nih_warn ("%s: %s", pack[i].path->path,
				  strerror (errno));
			continue;
		}

		num_pages = (statbuf.st_size
			     ? (statbuf.st_size - 1) / page_size + 1
			     : 0);

		buf = NIH_MUST (nih_alloc (NULL, num_pages + 1));
		memset (buf, '.', num_pages);
		buf[num_pages] = '\0';

		block_count = 0;
		block_bytes = 0;

		for (size_t j = 0; j < file->num_blocks; j++) {
			if (file->blocks[j].pathidx != pack[i].idx)
				continue;

			if (file->blocks[j].offset / page_size < num_pages)
				buf[file->blocks[j].offset / page_size] = '@';

			for (off_t k = file->blocks[j].offset / page_size + 1;
			     ((k < (file->blocks[j].offset + file->blocks[j].length) / page_size)
			      && (k < num_pages));
			     k++)
				buf[k] = '#';

			block_count++;
			block_bytes += file->blocks[j].length;
		}

		nih_message ("%s (%zu kB), %zu blocks (%zu kB)",
			     pack[i].path->path, (size_t)statbuf.st_size / 1024,
			     block_count, (size_t)block_bytes / 1024);

		ptr = buf;
		while (strlen (ptr) > 74) {
			nih_message ("  [%.74s]", ptr);
			ptr += 74;
		}

		if (strlen (ptr))
			nih_message ("  [%-74s]", ptr);

		nih_message ("%s", "");

		for (size_t j = 0; j < file->num_blocks; j++) {
			if (file->blocks[j].pathidx != pack[i].idx)
				continue;

			nih_message ("\t%zu, %zu bytes (at %zu)",
				     (size_t)file->blocks[j].offset,
				     (size_t)file->blocks[j].length,
				     (size_t)file->blocks[j].physical);
		}

		nih_message ("%s", "");
	}
}


int
do_readahead (PackFile *file,
	      int       daemonise)
{
	int             nr_open;
	struct rlimit   nofile;

	nih_assert (file != NULL);

	/* Increase our maximum file open count so that we can actually
	 * open everything; if the file is larger than the kernel limit,
	 * then silently pretend the rest doesn't exist.
	 */
	if (get_value (AT_FDCWD, "/proc/sys/fs/nr_open", &nr_open) < 0)
		return -1;

	if ((size_t)(nr_open - 10) < file->num_paths) {
		file->num_paths = nr_open - 10;
		nih_info ("Truncating to first %zu paths", file->num_paths);
	}

	/* Adjust our resource limits */
	nofile.rlim_cur = 10 + file->num_paths;
	nofile.rlim_max = 10 + file->num_paths;

	if (setrlimit (RLIMIT_NOFILE, &nofile) < 0)
		nih_return_system_error (-1);

	if (file->rotational) {
		return do_readahead_hdd (file, daemonise);
	} else {
		return do_readahead_ssd (file, daemonise);
	}
}

static int
do_readahead_hdd (PackFile *file,
		  int       daemonise)
{
	struct timespec start;
	const char *    devname;
	ext2_filsys     fs = NULL;
	nih_local int * fds = NULL;

	nih_assert (file != NULL);

	/* Adjust our CPU and I/O priority, we want to stay in the
	 * foreground and hog all bandwidth to avoid jumping around the
	 * disk.
	 */
	if (setpriority (PRIO_PROCESS, getpid (), -20))
		nih_warn ("%s: %s", _("Failed to set CPU priority"),
			  strerror (errno));

	if (syscall (__NR_ioprio_set, IOPRIO_WHO_PROCESS, getpid (),
		     IOPRIO_RT_HIGHEST) < 0)
		nih_warn ("%s: %s", _("Failed to set I/O priority"),
			  strerror (errno));

	clock_gettime (CLOCK_MONOTONIC, &start);

	/* Attempt to open the device as an ext2/3/4 filesystem,
	 * and if successful do a bit of pre-loading of inode groups
	 * to speed up opening files.
	 */
	devname = blkid_devno_to_devname (file->dev);
	if (devname
	    && (! ext2fs_open (devname, 0, 0, 0, unix_io_manager, &fs))) {
		nih_assert (fs != NULL);

		for (size_t i = 0; i < file->num_groups; i++)
			preload_inode_group (fs, file->groups[i]);

		ext2fs_close (fs);
	}

	print_time ("Preload ext2fs inodes", &start);

	/* Open all of the files */
	fds = NIH_MUST (nih_alloc (NULL, sizeof (int) * file->num_paths));
	for (size_t i = 0; i < file->num_paths; i++) {
		fds[i] = open (file->paths[i].path, O_RDONLY | O_NOATIME);
		if (fds[i] < 0)
			nih_warn ("%s: %s", file->paths[i].path,
				  strerror (errno));
	}

	print_time ("Open files", &start);

	/* Read in all of the blocks in a single pass for rotational
	 * disks, otherwise we'll have a seek time penalty.  For SSD,
	 * use a few threads to read in really fast.
	 */
	for (size_t i = 0; i < file->num_blocks; i++) {
		if ((fds[file->blocks[i].pathidx] < 0)
		    || (file->blocks[i].pathidx >= file->num_paths))
			continue;

		readahead (fds[file->blocks[i].pathidx],
			   file->blocks[i].offset,
			   file->blocks[i].length);
	}

	print_time ("Readahead", &start);

	return 0;
}

static void
preload_inode_group (ext2_filsys fs,
		     int         group)
{
	ext2_inode_scan scan = NULL;

	nih_assert (fs != NULL);

	if (! ext2fs_open_inode_scan (fs, 0, &scan)) {
		nih_assert (scan != NULL);

		if (! ext2fs_inode_scan_goto_blockgroup (scan, group)) {
			struct ext2_inode inode;
			ext2_ino_t        ino = 0;

			while ((! ext2fs_get_next_inode (scan, &ino, &inode))
			       && (ext2fs_group_of_ino (fs, ino) == group))
				;
		}

		ext2fs_close_inode_scan (scan);
	}
}


struct thread_ctx {
	PackFile *file;
	size_t    idx;
	int *     got;
};

static int
do_readahead_ssd (PackFile *file,
		  int       daemonise)
{
	struct timespec   start;
	pthread_t         thread[NUM_THREADS];
	struct thread_ctx ctx;

	nih_assert (file != NULL);

	/* Can only --daemon for SSD */
	if (daemonise) {
		pid_t pid;

		pid = fork ();
		if (pid < 0) {
			nih_return_system_error (-1);
		} else if (pid > 0) {
			_exit (0);
		}
	}

	if (syscall (__NR_ioprio_set, IOPRIO_WHO_PROCESS, getpid (),
		     IOPRIO_IDLE_LOWEST) < 0)
		nih_warn ("%s: %s", _("Failed to set I/O priority"),
			  strerror (errno));

	clock_gettime (CLOCK_MONOTONIC, &start);

	ctx.file = file;
	ctx.idx = 0;
	ctx.got = NIH_MUST (nih_alloc (NULL, sizeof (int) * file->num_paths));
	memset (ctx.got, 0, sizeof (int) * file->num_paths);

	for (int t = 0; t < NUM_THREADS; t++)
		pthread_create (&thread[t], NULL, ra_thread, &ctx);
	for (int t = 0; t < NUM_THREADS; t++)
		pthread_join (thread[t], NULL);

	print_time ("Readahead", &start);

	return 0;
}

static void *
ra_thread (void *ptr)
{
	struct thread_ctx *ctx = ptr;

	for (;;) {
		size_t i;
		size_t pathidx;
		int    fd;

		i = __sync_fetch_and_add (&ctx->idx, 1);
		if (i >= ctx->file->num_blocks)
			break;

		pathidx = ctx->file->blocks[i].pathidx;
		if (pathidx > ctx->file->num_paths)
			continue;

		if (! __sync_bool_compare_and_swap (&ctx->got[pathidx], 0, 1))
			continue;

		fd = open (ctx->file->paths[pathidx].path,
			   O_RDONLY | O_NOATIME);
		if (fd < 0) {
			nih_warn ("%s: %s", ctx->file->paths[pathidx].path,
				  strerror (errno));
			continue;
		}

		do {
			readahead (fd,
				   ctx->file->blocks[i].offset,
				   ctx->file->blocks[i].length);
		} while ((++i < ctx->file->num_blocks)
			 && (ctx->file->blocks[i].pathidx == pathidx));
	}

	return NULL;
}
