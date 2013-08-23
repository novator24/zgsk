noinst_LTLIBRARIES = libgsk-http.la
pkginclude_httpdir = $(includedir)/gsk-1.0/gsk/http

INCLUDES = @GLIB_CFLAGS@ @GSK_DEBUG_CFLAGS@

libgsk_http_la_SOURCES = \
gskhttpclient.c \
gskhttpcontent.c \
gskhttpheader.c \
gskhttpheader-input.c \
gskhttpheader-output.c \
gskhttprequest.c \
gskhttpresponse.c \
gskhttpserver.c \
gskprefixtree.c

noinst_HEADERS = gskprefixtree.h

pkginclude_http_HEADERS = \
gskhttpclient.h \
gskhttpcontent.h \
gskhttpheader.h \
gskhttprequest.h \
gskhttpresponse.h \
gskhttpserver.h

gskhttpheader.lo: gskhttpheader.inc

gskhttpheader.inc: $(srcdir)/gskhttpheader.h 
	$(GLIB_MKENUMS) --vhead 'static const GEnumValue @enum_name@_enum_values[] = {' \
	                --vprod '{@VALUENAME@, "@VALUENAME@", "@valuenick@"},' \
			--vtail '{0,NULL,NULL}};' $(srcdir)/gskhttpheader.h > gskhttpheader.inc
