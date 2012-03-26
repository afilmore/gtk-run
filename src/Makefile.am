## Process this file with automake to produce Makefile.in

bin_PROGRAMS = gtkrun

AM_CPPFLAGS = \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
	$(GTKRUN_CFLAGS)

AM_CFLAGS =\
	 -Wall\
	 -g

gtkrun_SOURCES = \
	gtk-run.c

gtkrun_LDFLAGS = \
	-Wl,--export-dynamic

gtkrun_LDADD = \
	$(GTKRUN_LIBS)


