## Process this file with automake to produce Makefile.in

SUBDIRS = po intl src

EXTRA_DIST = config.rpath

ACLOCAL_AMFLAGS = --install -I m4


initdir = $(sysconfdir)/init
dist_init_DATA = \
	conf/ureadahead.conf \
	conf/ureadahead-other.conf


dist_man_MANS = \
	man/ureadahead.8

EXTRA_DIST += 0001-trace-add-trace-events-for-open-exec-an.patch

