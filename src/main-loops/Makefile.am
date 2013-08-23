INCLUDES = @GLIB_CFLAGS@ @GSK_DEBUG_CFLAGS@
noinst_LTLIBRARIES = libgsk-mainloops.la

libgsk_mainloops_la_SOURCES = \
gskmainloopepoll.c \
gskmainloopepoll.h \
gskmainloopdevpoll.c \
gskmainloopdevpoll.h \
gskmainloopkqueue.c \
gskmainloopkqueue.h \
gskmainlooppoll.c \
gskmainlooppoll.h \
gskmainlooppollbase.c \
gskmainlooppollbase.h \
gskmainloopselect.c \
gskmainloopselect.h

libgsk_mainloops_la_LIBADD = @EPOLL_LIBS@
