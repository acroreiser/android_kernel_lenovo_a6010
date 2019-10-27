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

#ifndef UREADAHEAD_TRACE_H
#define UREADAHEAD_TRACE_H

#include <sys/types.h>

#include <nih/macros.h>
#include <nih/list.h>


NIH_BEGIN_EXTERN

int trace (int daemonise, int timeout);

NIH_END_EXTERN

#endif /* UREADAHEAD_TRACE_H */
