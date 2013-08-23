
INCLUDES = -I$(top_srcdir)/include @GSK_CFLAGS@ @GSK_LIB_CFLAGS@
LDADD = \
  ../../src/protocols/libgsk-protocol@debug_suffix@.la \
  ../../src/libgsk@debug_suffix@.la \
  ../../src/gtk/libgsk-minigtk@debug_suffix@.la \
  @GSK_LIBS@
noinst_PROGRAMS = gskgeturl
gskgeturl_SOURCES = geturl.c
