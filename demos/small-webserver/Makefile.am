
noinst_PROGRAMS = gsksmallhttpserver

gsksmallhttpserver_SOURCES = \
microhttplistener.c \
register-server-funcs.c
noinst_HEADERS = \
microhttplistener.h

debug_suffix = @debug_suffix@
INCLUDES = -I$(top_srcdir)/include @GSK_CFLAGS@ @GSK_LIB_CFLAGS@

misc_gsk_ld_add = \
  ../../src/libgsk$(debug_suffix).la \
  ../../src/protocols/libgsk-protocol$(debug_suffix).la \
  ../../src/xml/libgsk-xml$(debug_suffix).la \
  ../../src/xml/libgsk-server$(debug_suffix).la \
  ../../src/gtk/libgsk-minigtk$(debug_suffix).la \
  @GSK_LIBS@

gsksmallhttpserver_LDADD = $(misc_gsk_ld_add)
