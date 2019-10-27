/* ureadahead
 *
 * file.c - file utility functions
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


#include <stdio.h>
#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/logging.h>

#include "file.h"


/**
 * fgets_alloc:
 * @stream: stdio stream to read from.
 *
 * Reads from stream up to EOF or a newline, without any line-length
 * limitations.
 *
 * Returns: static string containing the entire line WITHOUT the
 * terminating newline, or NULL if end of file is reached and nothing
 * was read.
 **/
char *
fgets_alloc (const void *parent,
	     FILE *      stream)
{
        char * buf = NULL;
        size_t buf_sz = 0;
	size_t buf_len = 0;

	nih_assert (stream != NULL);

	for (;;) {
		char *ret;
		char *pos;

		if (buf_sz <= (buf_len + 1)) {
			buf_sz += 4096;
			buf = NIH_MUST (nih_realloc (buf, parent, buf_sz));
		}

		ret = fgets (buf + buf_len, buf_sz - buf_len, stream);
		if ((! ret) && (! buf_len)) {
			nih_free (buf);
			return NULL;
		} else if (! ret) {
			return buf;
		}

		buf_len += strlen (ret);
		pos = strchr (ret, '\n');
		if (pos) {
			*pos = '\0';
			break;
		}
	}

	return buf;
}
