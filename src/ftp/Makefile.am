noinst_LTLIBRARIES = libgsk-ftp.la
pkginclude_ftpdir = $(includedir)/gsk-1.0/gsk/ftp

INCLUDES = @GLIB_CFLAGS@ @GSK_DEBUG_CFLAGS@

libgsk_ftp_la_SOURCES = \
gskftpcommon.c

pkginclude_ftp_HEADERS = \
gskftpcommon.h

gskftpcommon.lo: gskftpcommon.inc

gskftpcommon.inc:
	$(GLIB_MKENUMS) --vhead 'static GEnumValue @enum_name@_enum_values = {' \
	                --vprod '{@VALUENAME@, "@VALUENAME@", "@valuenick@"},' \
			--vtail '{0,NULL,NULL}};' $(srcdir)/gskftpcommon.h > gskftpcommon.inc
