
debug_suffix = @debug_suffix@

LDADD = \
  ../../src/protocols/libgsk-protocol$(debug_suffix).la \
  ../../src/libgsk$(debug_suffix).la \
  ../../src/gtk/libgsk-minigtk$(debug_suffix).la \
  @GSK_LIBS@
INCLUDES = -I$(top_srcdir)/include @GSK_CFLAGS@ @GSK_LIB_CFLAGS@
noinst_PROGRAMS = textnode-test gsknamed
textnode_test_SOURCES = textnode-test.c textnode.c
noinst_HEADERS = gsknamedconfig.h gsknamedresolver.h gsksimpleacl.h gsksyslog.h textnode.h
gsknamed_SOURCES = textnode.c gsknamedconfig.c gsknamedresolver.c gsknamed.c gsksimpleacl.c gsksyslog.c
