if HAVE_OPENSSL
ssl_dirs = ssl
ssl_libs = ssl/libgsk-ssl.la
else
ssl_dirs = 
ssl_libs =
endif

composite_headers = \
gsk.h \
gskhttp.h \
gskzlib.h \
gskdns.h

SUBDIRS = main-loops common dns zlib $(ssl_dirs) hash mime http xmlrpc url store control . programs tests
INCLUDES = @GLIB_CFLAGS@ @GSK_DEBUG_CFLAGS@
gskincludedir = $(includedir)/gsk-1.0/gsk
gskinclude_HEADERS = \
$(composite_headers) \
gskbuffer.h \
gskbufferstream.h \
gskdaemonize.h \
gskdebug.h \
gskdebugalloc.h \
gskmainloop.h \
gskmemorybarrier.h \
gskerrno.h \
gskerror.h \
gskfork.h \
gskghelpers.h \
gskhook.h \
gskindexer.h \
gskinit.h \
gskio.h \
gskipv4.h \
gsklistmacros.h \
gsklog.h \
gsklogringbuffer.h \
gskmacros.h \
gskmain.h \
gskmemory.h \
gskmempool.h \
gskmodule.h \
gsknameresolver.h \
gsknetworkinterface.h \
gskpacket.h \
gskpacketqueue.h \
gskpacketqueuefd.h \
gskpassfd.h \
gskpersistentconnection.h \
gskprocessinfo.h \
gskqsortmacro.h \
gskrequest.h \
gskrbtreemacros.h \
gsksocketaddress.h \
gsksocketaddresssymbolic.h \
gsksimplefilter.h \
gskstdio.h \
gskstreamqueue.h \
gskstreamconcat.h \
gskstream.h \
gskstreamclient.h \
gskstreamconnection.h \
gskstreamexternal.h \
gskstreamfd.h \
gskstreamlistener.h \
gskstreamlistenersocket.h \
gskstreamtransferrequest.h \
gskstreamwatchdog.h \
gskstringmatcher.h \
gsktable.h \
gskthreadpool.h \
gsktree.h \
gsktypes.h \
gskutils.h

lib_LTLIBRARIES = libgsk-1.0.la
libgsk_1_0_la_SOURCES = \
inlines.c \
gskbuffer.c \
gskbufferstream.c \
gskdaemonize.c \
gskdebug.c \
gskdebugalloc.c \
gskerrno.c \
gskerror.c \
gskfork.c \
gskghelpers.c \
gskhook.c \
gskindexer.c \
gskinit.c \
gskinlines.c \
gskio.c \
gskipv4.c \
gsklog.c \
gsklogringbuffer.c \
gskmain.c \
gskmainloop.c \
gskmemory.c \
gskmempool.c \
gskmodule.c \
gsknameresolver.c \
gsknetworkinterface.c \
gskpacket.c \
gskpacketqueue.c \
gskpacketqueuefd.c \
gskpassfd.c \
gskpersistentconnection.c \
gskprocessinfo.c \
gskrequest.c \
gsksimplefilter.c \
gsksocketaddress.c \
gsksocketaddresssymbolic.c \
gsksocketaddress-connect.c \
gskstdio.c \
gskstream.c \
gskstreamqueue.c \
gskstreamconcat.c \
gskstreamconnection.c \
gskstreamclient.c \
gskstreamexternal.c \
gskstreamfd.c \
gskstreamlistener.c \
gskstreamlistenersocket.c \
gskstreamtransferrequest.c \
gskstreamwatchdog.c \
gsktable-flat.c \
gsktable-options.c \
gsktable.c \
gskthreadpool.c \
gsktree.c \
gsktypes.c \
gskutils.c

noinst_HEADERS = \
gsktable-file.h \
gsktable-helpers.h \
gsktable-implement-run-merge-task.inc.c \
debug.h \
cycle.h

gskerror.lo: gskerror.inc

libgsk_1_0_la_LDFLAGS = @GLIB_LIBS@
libgsk_1_0_la_LIBADD = \
main-loops/libgsk-mainloops.la \
dns/libgsk-dns.la \
common/libgsk-common.la \
http/libgsk-http.la \
mime/libgsk-mime.la \
$(ssl_libs) \
url/libgsk-url.la \
xmlrpc/libgsk-xmlrpc.la \
zlib/libgsk-zlib.la \
hash/libgsk-hash.la \
control/libgsk-control.la \
store/libgsk-store.la

BUILT_SOURCES = gskmemorybarrier.h gsktable-implementations-generated.c

gsktable-implementations-generated.c: $(srcdir)/gen-gsk-table-includes.pl
	perl $(srcdir)/gen-gsk-table-includes.pl > gsktable-implementations-generated.c

EXTRA_DIST = gen-gsk-table-includes.pl

gskerror.inc: $(srcdir)/gskerror.h
	glib-mkenums  --vprod '{@VALUENAME@, "@VALUENAME@", "@valuenick@"},' $(srcdir)/gskerror.h > gskerror.inc

# HACK
missing-refs:
	$(MAKE) all 2>&1 | grep 'undefined reference' | sed -e 's/.*`//;' -e "s/'.*//" | sort -u
