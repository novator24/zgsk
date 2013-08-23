gtkpkgincludedir=$(pkgincludedir)/gtk

gtkpkginclude_HEADERS = \
gtkarg.h \
gtkdebug.h \
gtkenums.h \
gtkmarshal.h \
gtkobject.h \
gtksignal.h \
gtktypebuiltins.h \
gtktypeutils.h \
makeenums.h 

sources = \
gtkarg.c \
gtkargcollector.c \
gtkextra.c \
gtkmarshal.c \
gtkobject.c \
gtksignal.c \
gtktypeutils.c 

EXTRA_DIST = \
gtktypebuiltins_evals.c \
gtktypebuiltins_ids.c \
gtktypebuiltins_vars.c 

INCLUDES = @GSK_CFLAGS@ @GSK_LIB_CFLAGS@
if GSK_NO_CHECKS_LIBRARY
lib_LTLIBRARIES = libgsk-minigtk-ndebug.la
libgsk_minigtk_ndebug_la_SOURCES = $(sources)
else
if GSK_DEBUG_LIBRARY
lib_LTLIBRARIES = libgsk-minigtk-debug.la
libgsk_minigtk_debug_la_SOURCES = $(sources)
else
lib_LTLIBRARIES = libgsk-minigtk.la
libgsk_minigtk_la_SOURCES = $(sources)
endif
endif
