/* ureadahead
 *
 * trace.c - boot tracing
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

#define _ATFILE_SOURCE


#include <sys/select.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <blkid.h>
#define NO_INLINE_FUNCS
#include <ext2fs.h>

#include <linux/fs.h>
#include <linux/fiemap.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/main.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "trace.h"
#include "pack.h"
#include "values.h"
#include "file.h"


/**
 * PATH_DEBUGFS:
 *
 * Path to the usual debugfs mountpoint.
 **/
#define PATH_DEBUGFS     "/sys/kernel/debug"

/**
 * PATH_DEBUGFS_TMP:
 *
 * Path to the temporary debugfs mountpoint that we mount it on if it
 * hasn't been mounted at the usual place yet.
 **/
#define PATH_DEBUGFS_TMP "/data/ureadahead/debugfs"

/**
 * INODE_GROUP_PRELOAD_THRESHOLD:
 *
 * Number of inodes in a group before we preload that inode's blocks.
 **/
#define INODE_GROUP_PRELOAD_THRESHOLD 8


/* Prototypes for static functions */
static int       read_trace        (const void *parent,
				    int dfd, const char *path,
				    PackFile **files, size_t *num_files);
static void      fix_path          (char *pathname);
static int       trace_add_path    (const void *parent, const char *pathname,
				    PackFile **files, size_t *num_files);
static int       ignore_path       (const char *pathname);
static PackFile *trace_file        (const void *parent, dev_t dev,
				    PackFile **files, size_t *num_files);
static int       trace_add_chunks  (const void *parent,
				    PackFile *file, PackPath *path,
				    int fd, off_t size);
static int       trace_add_extents (const void *parent,
				    PackFile *file, PackPath *path,
				    int fd, off_t size,
				    off_t offset, off_t length);
static int       trace_add_groups  (const void *parent, PackFile *file);
static int       trace_sort_blocks (const void *parent, PackFile *file);
static int       trace_sort_paths  (const void *parent, PackFile *file);


static void
sig_interrupt (int signum)
{
}

int
trace (int daemonise,
       int timeout)
{
	int                 dfd;
	FILE                *fp;
	int                 unmount = FALSE;
	int                 old_sys_open_enabled = 0;
	int                 old_open_exec_enabled = 0;
	int                 old_uselib_enabled = 0;
	int                 old_tracing_enabled = 0;
	int                 old_buffer_size_kb = 0;
	struct sigaction    act;
	struct sigaction    old_sigterm;
	struct sigaction    old_sigint;
	struct timeval      tv;
	nih_local PackFile *files = NULL;
	size_t              num_files = 0;
	size_t              num_cpus = 0;

	/* Mount debugfs if not already mounted */
	dfd = open (PATH_DEBUGFS "/tracing", O_RDONLY | O_NOATIME);
	if (dfd < 0) {
		if (errno != ENOENT)
			nih_return_system_error (-1);

		if (mount ("none", PATH_DEBUGFS_TMP, "debugfs", 0, NULL) < 0)
			nih_return_system_error (-1);

		dfd = open (PATH_DEBUGFS_TMP "/tracing", O_RDONLY | O_NOATIME);
		if (dfd < 0) {
			nih_error_raise_system ();
			umount (PATH_DEBUGFS_TMP);
			return -1;
		}

		unmount = TRUE;
	}

	/*
	 * Count the number of CPUs, default to 1 on error. 
	 */
	fp = fopen("/proc/cpuinfo", "r");
	if (fp) {
		int line_size=1024;
		char *processor="processor";
		char *line = malloc(line_size);
		if (line) {
			num_cpus = 0;
			while (fgets(line,line_size,fp) != NULL) {
				if (!strncmp(line,processor,strlen(processor)))
					num_cpus++;
			}
			free(line);
			nih_message("Counted %d CPUs\n",num_cpus);
		}
		fclose(fp);
	}
	if (!num_cpus)
		num_cpus = 1;

	/* Enable tracing of open() syscalls */
	if (set_value (dfd, "events/fs/do_sys_open/enable",
		       TRUE, &old_sys_open_enabled) < 0)
		goto error;
 	if (set_value (dfd, "events/fs/open_exec/enable",
		       TRUE, &old_open_exec_enabled) < 0)
		goto error;
 	if (set_value (dfd, "events/fs/uselib/enable",
		       TRUE, &old_uselib_enabled) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_debug ("Missing uselib tracing: %s", err->message);
		nih_free (err);

		old_uselib_enabled = -1;
	}
	if (set_value (dfd, "buffer_size_kb", 8192/num_cpus, &old_buffer_size_kb) < 0)
		goto error;
	if (set_value (dfd, "tracing_on",
		       TRUE, &old_tracing_enabled) < 0)
		goto error;

	if (daemonise) {
		pid_t pid;

		pid = fork ();
		if (pid < 0) {
			nih_error_raise_system ();
			goto error;
		} else if (pid > 0) {
			_exit (0);
		}
	}

	/* Sleep until we get signals */
	act.sa_handler = sig_interrupt;
	sigemptyset (&act.sa_mask);
	act.sa_flags = 0;

	sigaction (SIGTERM, &act, &old_sigterm);
	sigaction (SIGINT, &act, &old_sigint);

	if (timeout) {
		tv.tv_sec = timeout;
		tv.tv_usec = 0;

		select (0, NULL, NULL, NULL, &tv);
	} else {
		pause ();
	}

	sigaction (SIGTERM, &old_sigterm, NULL);
	sigaction (SIGINT, &old_sigint, NULL);

	/* Restore previous tracing settings */
	if (set_value (dfd, "tracing_on",
		       old_tracing_enabled, NULL) < 0)
		goto error;
	if (old_uselib_enabled >= 0)
		if (set_value (dfd, "events/fs/uselib/enable",
			       old_uselib_enabled, NULL) < 0)
			goto error;
	if (set_value (dfd, "events/fs/open_exec/enable",
		       old_open_exec_enabled, NULL) < 0)
		goto error;
	if (set_value (dfd, "events/fs/do_sys_open/enable",
		       old_sys_open_enabled, NULL) < 0)
		goto error;

	/* Be nicer */
	if (nice (15))
		;

	/* Read trace log */
	if (read_trace (NULL, dfd, "trace", &files, &num_files) < 0)
		goto error;

	/*
	 * Restore the trace buffer size (which has just been read) and free
	 * a bunch of memory.
	 */
	if (set_value (dfd, "buffer_size_kb", old_buffer_size_kb, NULL) < 0)
		goto error;

	/* Unmount the temporary debugfs mount if we mounted it */
	if (close (dfd)) {
		nih_error_raise_system ();
		goto error;
	}
	if (unmount
	    && (umount (PATH_DEBUGFS_TMP) < 0)) {
		nih_error_raise_system ();
		goto error;
	}

	/* Write out pack files */
	for (size_t i = 0; i < num_files; i++) {
		nih_local char *filename = NULL;

		filename = pack_file_name_for_device (NULL, files[i].dev);
		if (! filename) {
			NihError *err;

			err = nih_error_get ();
			nih_warn ("%s", err->message);
			nih_free (err);

			continue;
		}

		nih_info ("Writing %s", filename);

		/* We only need to apply additional sorting to the
		 * HDD-optimised packs, the SSD ones can read in random
		 * order quite happily.
		 *
		 * Also for HDD, generate the inode group preloading
		 * array.
		 */
		if (files[i].rotational) {
			trace_add_groups (files, &files[i]);

			trace_sort_blocks (files, &files[i]);
			trace_sort_paths (files, &files[i]);
		}

		write_pack (filename, &files[i]);

		if (nih_log_priority < NIH_LOG_MESSAGE)
			pack_dump (&files[i], SORT_OPEN);
	}

	return 0;
error:
	close (dfd);
	if (unmount)
		umount (PATH_DEBUGFS_TMP);

	return -1;
}


static int
read_trace (const void *parent,
	    int         dfd,
	    const char *path,
	    PackFile ** files,
	    size_t *    num_files)
{
	int   fd;
	FILE *fp;
	char *line;

	nih_assert (path != NULL);
	nih_assert (files != NULL);
	nih_assert (num_files != NULL);

	fd = openat (dfd, path, O_RDONLY);
	if (fd < 0)
		nih_return_system_error (-1);

	fp = fdopen (fd, "r");
	if (! fp) {
		nih_error_raise_system ();
		close (fd);
		return -1;
	}

	while ((line = fgets_alloc (NULL, fp)) != NULL) {
		char *ptr;
		char *end;

		ptr = strstr (line, " do_sys_open:");
		if (! ptr)
			ptr = strstr (line, " open_exec:");
		if (! ptr)
			ptr = strstr (line, " uselib:");
		if (! ptr) {
			nih_free (line);
			continue;
		}

		ptr = strchr (ptr, '"');
		if (! ptr) {
			nih_free (line);
			continue;
		}

		ptr++;

		end = strrchr (ptr, '"');
		if (! end) {
			nih_free (line);
			continue;
		}

		*end = '\0';

		fix_path (ptr);
		trace_add_path (parent, ptr, files, num_files);

		nih_free (line);
	}

	if (fclose (fp) < 0)
		nih_return_system_error (-1);

	return 0;
}

static void
fix_path (char *pathname)
{
	char *ptr;

	nih_assert (pathname != NULL);

	for (ptr = pathname; *ptr; ptr++) {
		size_t len;

		if (ptr[0] != '/')
			continue;

		len = strcspn (ptr + 1, "/");

		/* // and /./, we shorten the string and repeat the loop
		 * looking at the new /
		 */
		if ((len == 0) || ((len == 1) && ptr[1] == '.')) {
			memmove (ptr, ptr + len + 1, strlen (ptr) - len);
			ptr--;
			continue;
		}

		/* /../, we shorten back to the previous / or the start
		 * of the string and repeat the loop looking at the new /
		 */
		if ((len == 2) && (ptr[1] == '.') && (ptr[2] == '.')) {
			char *root;

			for (root = ptr - 1;
			     (root >= pathname) && (root[0] != '/');
			     root--)
				;
			if (root < pathname)
				root = pathname;

			memmove (root, ptr + len + 1, strlen (ptr) - len);
			ptr = root - 1;
			continue;
		}
	}

	while ((ptr != pathname) && (*(--ptr) == '/'))
		*ptr = '\0';
}


static int
trace_add_path (const void *parent,
		const char *pathname,
		PackFile ** files,
		size_t *    num_files)
{
	static NihHash *path_hash = NULL;
	struct stat     statbuf;
	int             fd;
	PackFile *      file;
	PackPath *      path;
	static NihHash *inode_hash = NULL;
	nih_local char *inode_key = NULL;

	nih_assert (pathname != NULL);
	nih_assert (files != NULL);
	nih_assert (num_files != NULL);

	/* We can't really deal with relative paths since we don't know
	 * the working directory that they were opened from.
	 */
	if (pathname[0] != '/') {
		nih_warn ("%s: %s", pathname, _("Ignored relative path"));
		return 0;
	}

	/* Certain paths aren't worth caching, because they're virtual or
	 * temporary filesystems and would waste pack space.
	 */
	if (ignore_path (pathname))
		return 0;

	/* Ignore paths that won't fit in the pack; we could use PATH_MAX,
	 * but with 1000 files that'd be 4M just for the
	 * pack.
	 */
	if (strlen (pathname) > PACK_PATH_MAX) {
		nih_warn ("%s: %s", pathname, _("Ignored far too long path"));
		return 0;
	}

	/* Use a hash table of paths to eliminate duplicate path names from
	 * the table since that would waste pack space (and fds).
	 */
	if (! path_hash)
		path_hash = NIH_MUST (nih_hash_string_new (NULL, 2500));

	if (nih_hash_lookup (path_hash, pathname)) {
		return 0;
	} else {
		NihListEntry *entry;

		entry = NIH_MUST (nih_list_entry_new (path_hash));
		entry->str = NIH_MUST (nih_strdup (entry, pathname));

		nih_hash_add (path_hash, &entry->entry);
	}

	/* Make sure that we have an ordinary file
	 * This avoids us opening a fifo or socket or symlink.
	 */
	if ((lstat (pathname, &statbuf) < 0)
	    || (S_ISLNK (statbuf.st_mode))
	    || (! S_ISREG (statbuf.st_mode)))
		return 0;

	/* Open and stat again to get the genuine details, in case it
	 * changes under us.
	 */
	fd = open (pathname, O_RDONLY | O_NOATIME);
	if (fd < 0) {
		nih_warn ("%s: %s: %s", pathname,
			  _("File vanished or error reading"),
			  strerror (errno));
		return -1;
	}

	if (fstat (fd, &statbuf) < 0) {
		nih_warn ("%s: %s: %s", pathname,
			  _("Error retrieving file stat"),
			  strerror (errno));
		close (fd);
		return -1;
	}

	/* Double-check that it's really still a file */
	if (! S_ISREG (statbuf.st_mode)) {
		close (fd);
		return 0;
	}

	/* Some people think it's clever to split their filesystem across
	 * multiple devices, so we need to generate a different pack file
	 * for each device.
	 *
	 * Lookup file based on the dev_t, potentially creating a new
	 * pack file in the array.
	 */
	file = trace_file (parent, statbuf.st_dev, files, num_files);

	/* Grow the PackPath array and fill in the details for the new
	 * path.
	 */
	file->paths = NIH_MUST (nih_realloc (file->paths, *files,
					     (sizeof (PackPath)
					      * (file->num_paths + 1))));

	path = &file->paths[file->num_paths++];
	memset (path, 0, sizeof (PackPath));

	path->group = -1;
	path->ino = statbuf.st_ino;

	strncpy (path->path, pathname, PACK_PATH_MAX);
	path->path[PACK_PATH_MAX] = '\0';

	/* The paths array contains each unique path opened, but these
	 * might be symbolic or hard links to the same underlying files
	 * and we don't want to read the same block more than once.
	 *
	 * Use a hash table of dev_t/ino_t pairs to make sure we only
	 * read the blocks of an actual file the first time.
	 */
	if (! inode_hash)
		inode_hash = NIH_MUST (nih_hash_string_new (NULL, 2500));

	inode_key = NIH_MUST (nih_sprintf (NULL, "%llu:%llu",
					   (unsigned long long)statbuf.st_dev,
					   (unsigned long long)statbuf.st_ino));

	if (nih_hash_lookup (inode_hash, inode_key)) {
		close (fd);
		return 0;
	} else {
		NihListEntry *entry;

		entry = NIH_MUST (nih_list_entry_new (inode_hash));
		entry->str = inode_key;
		nih_ref (entry->str, entry);

		nih_hash_add (inode_hash, &entry->entry);
	}

	/* There's also no point reading zero byte files, since they
	 * won't have any blocks (and we can't mmap zero bytes anyway).
	 */
	if (! statbuf.st_size) {
		close (fd);
		return 0;
	}

	/* Now read the in-memory chunks of this file and add those to
	 * the pack file too.
	 */
	trace_add_chunks (*files, file, path, fd, statbuf.st_size);
	close (fd);

	return 0;
}

static int
ignore_path (const char *pathname)
{
	nih_assert (pathname != NULL);

	if (! strncmp (pathname, "/system/", 8))
		return FALSE;
	if (! strncmp (pathname, "/data/", 6))
		return FALSE;

	return TRUE;
}


static PackFile *
trace_file (const void *parent,
	    dev_t       dev,
	    PackFile ** files,
	    size_t *    num_files)
{
	nih_local char *filename = NULL;
	int             rotational;
	PackFile *      file;

	nih_assert (files != NULL);
	nih_assert (num_files != NULL);

	/* Return any existing file structure for this device */
	for (size_t i = 0; i < *num_files; i++)
		if ((*files)[i].dev == dev)
			return &(*files)[i];

	/* Query sysfs to see whether this disk is rotational; this
	 * obviously won't work for virtual devices and the like, so
	 * default to TRUE for now.
	 */
	filename = NIH_MUST (nih_sprintf (NULL, "/sys/dev/block/%d:0/queue/rotational",
					  major (dev)));

	if (get_value (AT_FDCWD, filename, &rotational) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_warn (_("Unable to obtain rotationalness for device %u:%u: %s"),
			  major (dev), minor (dev), err->message);
		nih_free (err);

		rotational = TRUE;
	}

	/* Grow the PackFile array and fill in the details for the new
	 * file.
	 */
	*files = NIH_MUST (nih_realloc (*files, parent,
					(sizeof (PackFile) * (*num_files + 1))));

	file = &(*files)[(*num_files)++];
	memset (file, 0, sizeof (PackFile));

	file->dev = dev;
	file->rotational = rotational;
	file->num_paths = 0;
	file->paths = NULL;
	file->num_blocks = 0;
	file->blocks = NULL;

	return file;
}


static int
trace_add_chunks (const void *parent,
		  PackFile *  file,
		  PackPath *  path,
		  int         fd,
		  off_t       size)
{
	static int               page_size = -1;
	void *                   buf;
	off_t                    num_pages;
	nih_local unsigned char *vec = NULL;

	nih_assert (file != NULL);
	nih_assert (path != NULL);
	nih_assert (fd >= 0);
	nih_assert (size > 0);

	if (page_size < 0)
		page_size = sysconf (_SC_PAGESIZE);

	/* Map the file into memory */
	buf = mmap (NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (buf == MAP_FAILED) {
		nih_warn ("%s: %s: %s", path->path,
			  _("Error mapping into memory"),
			  strerror (errno));
		return -1;
	}

	/* Grab the core memory map of the file */
	num_pages = (size - 1) / page_size + 1;
	vec = NIH_MUST (nih_alloc (NULL, num_pages));
	memset (vec, 0, num_pages);

	if (mincore (buf, size, vec) < 0) {
		nih_warn ("%s: %s: %s", path->path,
			  _("Error retrieving page cache info"),
			  strerror (errno));
		munmap (buf, size);
		return -1;
	}

	/* Clean up */
	if (munmap (buf, size) < 0) {
		nih_warn ("%s: %s: %s", path->path,
			  _("Error unmapping from memory"),
			  strerror (errno));
		return -1;
	}


	/* Now we can figure out which contiguous bits of the file are
	 * in core memory.
	 */
	for (off_t i = 0; i < num_pages; i++) {
		off_t offset;
		off_t length;

		if (! vec[i])
			continue;

		offset = i * page_size;
		length = page_size;

		while (((i + 1) < num_pages) && vec[i + 1]) {
			length += page_size;
			i++;
		}

		/* The rotational crowd need this split down further into
		 * on-disk extents, the non-rotational folks can just use
		 * the chunks data.
		 */
		if (file->rotational) {
			trace_add_extents (parent, file, path, fd, size,
					   offset, length);
		} else {
			PackBlock *block;

			file->blocks = NIH_MUST (nih_realloc (file->blocks, parent,
							      (sizeof (PackBlock)
							       * (file->num_blocks + 1))));

			block = &file->blocks[file->num_blocks++];
			memset (block, 0, sizeof (PackBlock));

			block->pathidx = file->num_paths - 1;
			block->offset = offset;
			block->length = length;
			block->physical = -1;
		}
	}

	return 0;
}

struct fiemap *
get_fiemap (const void *parent,
	    int         fd,
	    off_t       offset,
	    off_t       length)
{
	struct fiemap *fiemap;

	nih_assert (fd >= 0);

	fiemap = NIH_MUST (nih_new (parent, struct fiemap));
	memset (fiemap, 0, sizeof (struct fiemap));

	fiemap->fm_start = offset;
	fiemap->fm_length = length;
	fiemap->fm_flags = 0;

	do {
		/* Query the current number of extents */
		fiemap->fm_mapped_extents = 0;
		fiemap->fm_extent_count = 0;

		if (ioctl (fd, FS_IOC_FIEMAP, fiemap) < 0) {
			nih_error_raise_system ();
			nih_free (fiemap);
			return NULL;
		}

		/* Always allow room for one extra over what we were told,
		 * so we know if they changed under us.
		 */
		fiemap = NIH_MUST (nih_realloc (fiemap, parent,
						(sizeof (struct fiemap)
						 + (sizeof (struct fiemap_extent)
						    * (fiemap->fm_mapped_extents + 1)))));
		fiemap->fm_extent_count = fiemap->fm_mapped_extents + 1;
		fiemap->fm_mapped_extents = 0;

		memset (fiemap->fm_extents, 0, (sizeof (struct fiemap_extent)
						* fiemap->fm_extent_count));

		if (ioctl (fd, FS_IOC_FIEMAP, fiemap) < 0) {
			nih_error_raise_system ();
			nih_free (fiemap);
			return NULL;
		}
	} while (fiemap->fm_mapped_extents
		 && (fiemap->fm_mapped_extents >= fiemap->fm_extent_count));

	return fiemap;
}

static int
trace_add_extents (const void *parent,
		   PackFile *  file,
		   PackPath *  path,
		   int         fd,
		   off_t       size,
		   off_t       offset,
		   off_t       length)
{
	nih_local struct fiemap *fiemap = NULL;

	nih_assert (file != NULL);
	nih_assert (path != NULL);
	nih_assert (fd >= 0);
	nih_assert (size > 0);

	/* Get the extents map for this chunk, then iterate the extents
	 * and put those in the pack instead of the chunks.
	 */
	fiemap = get_fiemap (NULL, fd, offset, length);
	if (! fiemap) {
		NihError *err;

		err = nih_error_get ();
		nih_warn ("%s: %s: %s", path->path,
			  _("Error retrieving chunk extents"),
			  err->message);
		nih_free (err);

		return -1;
	}

	for (__u32 j = 0; j < fiemap->fm_mapped_extents; j++) {
		PackBlock *block;
		off_t      start;
		off_t      end;

		if (fiemap->fm_extents[j].fe_flags & FIEMAP_EXTENT_UNKNOWN)
			continue;

		/* Work out the intersection of the chunk and extent */
		start = nih_max (fiemap->fm_start,
				 fiemap->fm_extents[j].fe_logical);
		end = nih_min ((fiemap->fm_start + fiemap->fm_length),
			       (fiemap->fm_extents[j].fe_logical
				+ fiemap->fm_extents[j].fe_length));

		/* Grow the blocks array to add the extent */
		file->blocks = NIH_MUST (nih_realloc (file->blocks, parent,
						      (sizeof (PackBlock)
						       * (file->num_blocks + 1))));

		block = &file->blocks[file->num_blocks++];
		memset (block, 0, sizeof (PackBlock));

		block->pathidx = file->num_paths - 1;
		block->offset = start;
		block->length = end - start;
		block->physical = (fiemap->fm_extents[j].fe_physical
				   + (start - fiemap->fm_extents[j].fe_logical));
	}

	return 0;
}

static int
trace_add_groups (const void *parent,
		  PackFile *  file)
{
	const char *devname;
	ext2_filsys fs = NULL;

	nih_assert (file != NULL);

	devname = blkid_devno_to_devname (file->dev);
	if (devname
	    && (! ext2fs_open (devname, 0, 0, 0, unix_io_manager, &fs))) {
		nih_assert (fs != NULL);
		size_t            num_groups = 0;
		nih_local size_t *num_inodes = NULL;
		size_t            mean = 0;
		size_t            hits = 0;

		nih_assert (fs != NULL);

		/* Calculate the number of inode groups on this filesystem */
		num_groups = ((fs->super->s_blocks_count - 1)
			      / fs->super->s_blocks_per_group) + 1;

		/* Fill in the pack path's group member, and count the
		 * number of inodes in each group.
		 */
		num_inodes = NIH_MUST (nih_alloc (NULL, (sizeof (size_t)
							 * num_groups)));
		memset (num_inodes, 0, sizeof (size_t) * num_groups);

		for (size_t i = 0; i < file->num_paths; i++) {
			file->paths[i].group = ext2fs_group_of_ino (fs, file->paths[i].ino);
			num_inodes[file->paths[i].group]++;
		}

		/* Iterate the groups and add any group that exceeds the
		 * inode preload threshold.
		 */
		for (size_t i = 0; i < num_groups; i++) {
			mean += num_inodes[i];
			if (num_inodes[i] > INODE_GROUP_PRELOAD_THRESHOLD) {
				file->groups = NIH_MUST (nih_realloc (file->groups, parent,
								      (sizeof (int)
								       * (file->num_groups + 1))));
				file->groups[file->num_groups++] = i;
				hits++;
			}
		}

		mean /= num_groups;

		nih_debug ("%zu inode groups, mean %zu inodes per group, %zu hits",
			   num_groups, mean, hits);

		ext2fs_close (fs);
	}

	return 0;
}


static int
block_compar (const void *a,
	      const void *b)
{
	const PackBlock *block_a = a;
	const PackBlock *block_b = b;

	nih_assert (block_a != NULL);
	nih_assert (block_b != NULL);

	if (block_a->physical < block_b->physical) {
		return -1;
	} else if (block_a->physical > block_b->physical) {
		return 1;
	} else {
		return 0;
	}
}

static int
trace_sort_blocks (const void *parent,
		   PackFile *  file)
{
	nih_assert (file != NULL);

	/* Sort the blocks array by physical location, since these are
	 * read in a separate pass to opening files, there's no reason
	 * to consider which path each block is in - and thus resulting
	 * in a linear disk read.
	 */
	qsort (file->blocks, file->num_blocks, sizeof (PackBlock),
	       block_compar);

	return 0;
}

static int
path_compar (const void *a,
	     const void *b)
{
	const PackPath * const *path_a = a;
	const PackPath * const *path_b = b;

	nih_assert (path_a != NULL);
	nih_assert (path_b != NULL);

	if ((*path_a)->group < (*path_b)->group) {
		return -1;
	} else if ((*path_a)->group > (*path_b)->group) {
		return 1;
	} else if ((*path_a)->ino < (*path_b)->ino) {
		return -1;
	} else if ((*path_b)->ino > (*path_b)->ino) {
		return 1;
	} else {
		return strcmp ((*path_a)->path, (*path_b)->path);
	}
}

static int
trace_sort_paths (const void *parent,
		  PackFile *  file)
{
	nih_local PackPath **paths = NULL;
	nih_local size_t *   new_idx = NULL;
	PackPath *           new_paths;

	nih_assert (file != NULL);

	/* Sort the paths array by ext2fs inode group, ino_t then path.
	 *
	 * Mucking around with things like the physical locations of
	 * first on-disk blocks of the dentry and stuff didn't work out
	 * so well, sorting by path was better, but this seems the best.
	 * (it looks good on blktrace too)
	 */
	paths = NIH_MUST (nih_alloc (NULL, (sizeof (PackPath *)
					    * file->num_paths)));

	for (size_t i = 0; i < file->num_paths; i++)
		paths[i] = &file->paths[i];

	qsort (paths, file->num_paths, sizeof (PackPath *),
	       path_compar);

	/* Calculate the new indexes of each path element in the old
	 * array, and then update the block array's path indexes to
	 * match.
	 */
	new_idx = NIH_MUST (nih_alloc (NULL,
				       (sizeof (size_t) * file->num_paths)));
	for (size_t i = 0; i < file->num_paths; i++)
		new_idx[paths[i] - file->paths] = i;

	for (size_t i = 0; i < file->num_blocks; i++)
		file->blocks[i].pathidx = new_idx[file->blocks[i].pathidx];

	/* Finally generate a new paths array with the new order and
	 * attach it to the file.
	 */
	new_paths = NIH_MUST (nih_alloc (parent,
					 (sizeof (PackPath) * file->num_paths)));
	for (size_t i = 0; i < file->num_paths; i++)
		memcpy (&new_paths[new_idx[i]], &file->paths[i],
			sizeof (PackPath));

	nih_unref (file->paths, parent);
	file->paths = new_paths;

	return 0;
}
