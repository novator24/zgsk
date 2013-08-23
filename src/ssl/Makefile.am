noinst_LTLIBRARIES = libgsk-ssl.la
pkginclude_ssldir = $(includedir)/gsk-1.0/gsk/ssl

INCLUDES = @GLIB_CFLAGS@ @GSK_DEBUG_CFLAGS@ @OPENSSL_CFLAGS@

libgsk_ssl_la_LIBADD = @OPENSSL_LIBS@

libgsk_ssl_la_SOURCES = \
gskopensslbiostream.c \
gskstreamssl.c \
gskstreamlistenerssl.c

pkginclude_ssl_HEADERS = \
gskopensslbiostream.h \
gskstreamssl.h \
gskstreamlistenerssl.h
