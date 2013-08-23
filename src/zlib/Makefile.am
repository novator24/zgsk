noinst_LTLIBRARIES = libgsk-zlib.la
pkginclude_zlibdir = $(includedir)/gsk-1.0/gsk/zlib

INCLUDES = @GLIB_CFLAGS@ @GSK_DEBUG_CFLAGS@

libgsk_zlib_la_LIBADD = -lz

libgsk_zlib_la_SOURCES = \
gskzlibdeflator.c \
gskzlibinflator.c \
gskzlib.c 

pkginclude_zlib_HEADERS = \
gskzlib.h \
gskzlibdeflator.h \
gskzlibinflator.h

