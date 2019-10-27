/* ureadahead
 *
 * Copyright © 2009 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
 *
 * Inspired by readahead:
 *   Copyright © 2005 Ziga Mahkovec <ziga.mahkovec@klika.si>
 *   Copyright © 2006, 2007 Red Hat, Inc.
 * and sreadahead:
 *   Copyright © 2008 Intel Corporation,
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/main.h>
#include <nih/option.h>
#include <nih/logging.h>
#include <nih/error.h>

#include "pack.h"
#include "trace.h"


/**
 * daemonise:
 *
 * Set to TRUE if we should become a daemon, rather than just running
 * in the foreground.
 **/
static int daemonise = FALSE;

/**
 * force_trace:
 *
 * Set to TRUE if we should re-trace no matter what, the existing pack
 * file will not be read.
 **/
static int force_trace = FALSE;

/**
 * timeout:
 *
 * Set to non-zero if we should stop tracing after a particular time,
 * rather than waiting for a signal.
 **/
static int timeout = 0;

/**
 * dump_pack:
 *
 * Set to TRUE to only dump the current pack file.
 **/
static int dump_pack = FALSE;

/**
 * sort_pack:
 *
 * Set to how we want the pack sorted when dumping.
 **/
static SortOption sort_pack = SORT_OPEN;


static int
sort_option (NihOption  *option,
	     const char *arg)
{
	SortOption *value;

	nih_assert (option != NULL);
	nih_assert (option->value != NULL);
	nih_assert (arg != NULL);

	value = (SortOption *)option->value;

	if (! strcmp (arg, "open")) {
		*value = SORT_OPEN;
	} else if (! strcmp (arg, "path")) {
		*value = SORT_PATH;
	} else if (! strcmp (arg, "disk")) {
		*value = SORT_DISK;
	} else if (! strcmp (arg, "size")) {
		*value = SORT_SIZE;
	} else {
		fprintf (stderr, _("%s: illegal argument: %s\n"),
			 program_name, arg);
		nih_main_suggest_help ();
		return -1;
	}

	return 0;
}


/**
 * options:
 *
 * Command-line options accepted by this tool.
 **/
static NihOption options[] = {
	{ 0, "daemon", N_("detach and run in the background"),
	  NULL, NULL, &daemonise, NULL },
	{ 0, "force-trace", N_("ignore existing pack and force retracing"),
	  NULL, NULL, &force_trace, NULL },
	{ 0, "timeout", N_("maximum time to trace [default: until terminated]"),
	  NULL, "SECONDS", &timeout, nih_option_int },
	{ 0, "dump", N_("dump the current pack file"),
	  NULL, NULL, &dump_pack, NULL },
	{ 0, "sort", N_("how to sort the pack file when dumping [default: open]"),
	  NULL, "SORT", &sort_pack, sort_option },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char **             args;
	nih_local char *    filename = NULL;
	nih_local PackFile *file = NULL;

	nih_main_init (argv[0]);

	nih_option_set_usage (_("[PATH]"));
	nih_option_set_synopsis (_("Read required files in advance"));
	nih_option_set_help (
		_("PATH should be the location of a mounted filesystem "
		  "for which files should be read.  If not given, the root "
		  "filesystem is assumed.\n"
		  "\n"
		  "If PATH is not given, and no readahead information exists "
		  "for the root filesystem (or it is old), tracing is "
		  "performed instead to generate the information for the "
		  "next boot."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

	if (! force_trace) {
		NihError *err;

		/* Lookup the filename for the pack based on the path given
		 * (if any).
		 */
		filename = pack_file_name (NULL, args[0]);
		if (! filename) {
			NihError *err;

			err = nih_error_get ();
			nih_fatal ("%s: %s: %s", args[0] ?: "/",
				   _("Unable to determine pack file name"),
				   err->message);
			nih_free (err);

			exit (2);
		}

		/* Read the current pack file */
		file = read_pack (NULL, filename, dump_pack);
		if (file) {
			if (dump_pack) {
				pack_dump (file, sort_pack);
				exit (0);
			}

			/* Read the pack */
			if (do_readahead (file, daemonise) < 0) {
				err = nih_error_get ();
				nih_error ("%s: %s", _("Error while reading"),
					   err->message);
				nih_free (err);
				exit (3);
			}

			exit (0);
		}

		/* Error reading file means we retrace if not given a PATH,
		 * otherwise we error out.
		 */
		err = nih_error_get ();
		if (args[0] || dump_pack) {
			nih_fatal ("%s: %s", filename, err->message);
		} else {
			nih_info ("%s: %s", filename, err->message);
		}
		nih_free (err);

		if (args[0] || dump_pack)
			exit (4);
	}

	/* Trace to generate new pack files */
	if (trace (daemonise, timeout) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_error ("%s: %s", _("Error while tracing"), err->message);
		nih_free (err);

		exit (5);
	}

	return 0;
}
