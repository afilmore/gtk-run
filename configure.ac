dnl Process this file with autoconf to produce a configure script.

AC_INIT(gtkrun, 0.1)

AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.11])

dnl AM_SILENT_RULES([yes])

AC_PROG_CC

dnl LT_INIT

pkg_modules="$pkg_modules \
             gtk+-3.0 \
             gio-unix-2.0 \
             gthread-2.0 \
             libmenu-cache"

PKG_CHECK_MODULES(GTKRUN, [$pkg_modules])
AC_SUBST(GTKRUN_CFLAGS)
AC_SUBST(GTKRUN_LIBS)

AC_OUTPUT([
    Makefile
    src/Makefile
])

