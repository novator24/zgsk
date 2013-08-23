noinst_LTLIBRARIES = libgsk-mime.la
pkginclude_mimedir = $(includedir)/gsk-1.0/gsk/mime

INCLUDES = @GLIB_CFLAGS@ @GSK_DEBUG_CFLAGS@

libgsk_mime_la_SOURCES = \
gskmimeencodings.c \
gskmimeencodings-base64.c \
gskmimeencodings-identity.c \
gskmimeencodings-quotedprintable.c \
gskmimemultipartdecoder.c \
gskmimemultipartencoder.c \
gskmimemultipartpiece.c

pkginclude_mime_HEADERS = \
gskmimeencodings.h \
gskmimemultipartdecoder.h \
gskmimemultipartencoder.h \
gskmimemultipartpiece.h
