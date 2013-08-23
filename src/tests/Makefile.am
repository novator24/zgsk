# Programs which are written in C, and are test programs,
# ie they exit with status 0 if some test is verified.
common_check_programs = \
	test-gskbase64 \
	test-gskbuffer \
	test-gskdate \
	test-gskhash \
	test-gskhook \
	test-concat \
	test-debugalloc \
	test-dnsrrcache \
	test-gsklistmacros \
	test-gskmodule \
	test-gsktable-file \
	test-hangup \
	test-http-content \
	test-http-header \
	test-http-serverclient \
	test-io-error \
	test-mempool \
	test-mime-multipart-decoder \
	test-mime-encdec \
	test-passfd \
	test-prefix-tree \
	test-qsortmacro \
	test-signal-handling \
	test-stream-fd-pipe \
	test-wait-source \
	test-gskstreamexternal \
	test-rbtree-macros \
	test-serverclient \
	test-store \
	test-streamfd-guess-flags \
	test-thread-pool \
	test-timer \
	test-xmlrpc \
	test-url \
	test-utils \
	test-zlib \
	test-tree

#### Once we get xml working again, add: test-xml

makecheck_scripts = \
	$(srcdir)/test-name-resolver \
	$(srcdir)/test-url-download

if HAVE_OPENSSL
ssl_check_programs = test-ssl
ssl_tests = 
else
ssl_check_programs =
ssl_tests = 
endif

makecheck_programs = \
	$(common_check_programs) \
	$(ssl_check_programs)

# Programs which are written in C and are intended to explore
# an area of GSK, however, they are not test programs
# and will usually print a usage message if run with no arguments.
diagnostic_programs = \
	test-echo \
	gsk-hash \
	test-gsklog \
	get-network-interfaces \
	get-process-info \
	dns-stress-test \
	mk-inputs--gsk-table-test \
	name-resolver \
	name-resolver-filter \
	url-download \
	test-http-server \
	test-persistent-connection \
	test-http-redirect \
	test-stdio-rotation \
	test-gsktable-helper \
	time-0 \
	test-zlib-stream \
 	$(ssl_tests)

# all programs to build.
check_PROGRAMS = \
	$(diagnostic_programs) \
	$(makecheck_programs)

noinst_HEADERS = testobject.h

# all programs and scripts to run when 'make check' is run.
TESTS = $(makecheck_programs) $(makecheck_scripts)

# Extra files which must be copied into the distribution .tar.gz file.
EXTRA_DIST = $(makecheck_scripts) \
client-key.pem client.pem server-key.pem server.pem \
make-ssl-pem-files

# --- How to build the programs. ---
INCLUDES = @GLIB_CFLAGS@ @GSK_DEBUG_CFLAGS@

# GLIB_LIBS should not be necessary, EXCEPT that
# on BSD -pthread is lost by libtool.  hack it up thusly.
LDADD = ../libgsk-1.0.la @GLIB_LIBS@

test_echo_SOURCES = test-echo.c
test_concat_SOURCES = test-concat.c
test_debugalloc_SOURCES = test-debugalloc.c
test_dnsrrcache_SOURCES = test-dnsrrcache.c
name_resolver_SOURCES = name-resolver.c
test_tree_SOURCES = test-tree.c
test_hangup_SOURCES = test-hangup.c
url_download_SOURCES = url-download.c
test_stream_fd_pipe_SOURCES = test-stream-fd-pipe.c
test_http_server_SOURCES = test-http-server.c
test_http_header_SOURCES = test-http-header.c
test_http_content_SOURCES = test-http-content.c
test_http_redirect_SOURCES = test-http-redirect.c
test_http_serverclient_SOURCES = test-http-serverclient.c
test_passfd_SOURCES = test-passfd.c
test_persistent_connection_SOURCES = test-persistent-connection.c
test_prefix_tree_SOURCES = test-prefix-tree.c
test_serverclient_SOURCES = test-serverclient.c
test_gskstreamexternal_SOURCES = test-gskstreamexternal.c
test_qsortmacro_SOURCES = test-qsortmacro.c
test_store_SOURCES = test-store.c testobject.c
test_streamfd_guess_flags_SOURCES = test-streamfd-guess-flags.c
test_url_SOURCES = test-url.c
test_utils_SOURCES = test-utils.c
test_zlib_stream_SOURCES = test-zlib-stream.c
test_stdio_rotation_SOURCES = test-stdio-rotation.c
test_gsktable_helper_SOURCES = test-gsktable-helper.c
time_0_SOURCES = time-0.c
mk_inputs__gsk_table_test_SOURCES = mk-inputs--gsk-table-test.c
test_gsktable_file_SOURCES = test-gsktable-file.c

# HACK: print list of undefined functions, useful when writing tons of new code
missing-refs:
	$(MAKE) all 2>&1 | grep 'undefined reference' | sed -e 's/.*`//;' -e "s/'.*//" | sort -u
