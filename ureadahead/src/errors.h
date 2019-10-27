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

#ifndef UREADAHEAD_ERRORS_H
#define UREADAHEAD_ERRORS_H

#include <nih/macros.h>
#include <nih/errors.h>


/* Allocated error numbers */
enum {
	UREADAHEAD_ERROR_START = NIH_ERROR_APPLICATION_START,

	PACK_DATA_ERROR,
	PACK_TOO_OLD
};

/* Error strings for defined messages */
#define PACK_DATA_ERROR_STR N_("Pack data error")
#define PACK_TOO_OLD_STR    N_("Pack too old")

#endif /* UREADAHEAD_ERRORS_H */

