/* ureadahead
 *
 * values.c - dealing with proc/sysfs values
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


#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "values.h"


int
get_value (int         dfd,
	   const char *path,
	   int *       value)
{
	int     fd;
	char    buf[80];
	ssize_t len;

	nih_assert (path != NULL);
	nih_assert (value != NULL);

	fd = openat (dfd, path, O_RDONLY);
	if (fd < 0)
		nih_return_system_error (-1);

	len = read (fd, buf, sizeof(buf) - 1);
	if (len < 0) {
		nih_error_raise_system ();
		close (fd);
		return -1;
	}

	buf[len] = '\0';
	*value = len ? atoi (buf) : 0;

	if (close (fd) < 0)
		nih_return_system_error (-1);

	return 0;
}

int
set_value (int         dfd,
	   const char *path,
	   int         value,
	   int *       oldvalue)
{
	int     fd;
	char    buf[80];
	ssize_t len;

	nih_assert (path != NULL);

	fd = openat (dfd, path, O_RDWR);
	if (fd < 0)
		nih_return_system_error (-1);

	if (oldvalue) {
		len = read (fd, buf, sizeof(buf) - 1);
		if (len < 0) {
			nih_error_raise_system ();
			close (fd);
			return -1;
		}

		buf[len] = '\0';
		*oldvalue = atoi (buf);

		nih_assert (lseek (fd, 0, SEEK_SET) == 0);
	}

	snprintf (buf, sizeof buf, "%d", value);

	len = write (fd, buf, strlen (buf));
	if (len < 0) {
		nih_error_raise_system ();
		close (fd);
		return -1;
	}

	nih_assert (len > 0);

	if (close (fd) < 0)
		nih_return_system_error (-1);

	return 0;
}
