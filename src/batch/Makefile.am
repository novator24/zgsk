
INCLUDES = @GLIB_CFLAGS@ @GSK_DEBUG_CFLAGS@

lib_LTLIBRARIES = libgskb.la

libgskb_la_SOURCES = \
	gskb-format-codegen.c \
	gskb-format.c \
	gskb-inline-impls.c \
	gskb-str-table.c \
	gskb-uint-table.c
libgskb_la_LIBADD = ../libgsk-1.0.la
bin_PROGRAMS = gskb-format-codegen

TESTS = test-codegen
check_PROGRAMS = test-codegen

# generated code
BUILT_SOURCES = test-codegen-generated.h test-codegen-generated.c \
		parser-symbol-table.inc \
		gskb-namespace-gskb-generated.inc

test-codegen-generated.h test-codegen-generated.c: \
	gskb-format-codegen test-codegen.formats
	./gskb-format-codegen \
		-i test-codegen.formats \
		-o test-codegen-generated

test_codegen_SOURCES = \
	test-codegen-generated.c \
	test-codegen.c
test_codegen_LDADD = libgskb.la
gskb_format_codegen_SOURCES = \
	gskb-format-codegen-main.c
gskb_format_codegen_LDADD = libgskb.la

noinst_PROGRAMS = make-gskb-namespace-gskb-generated \
	          make-parser-symbol-table
make_gskb_namespace_gskb_generated_SOURCES = \
	make-gskb-namespace-gskb-generated.c
make_parser_symbol_table_SOURCES = \
	make-parser-symbol-table.c
make_parser_symbol_table_LDADD = ../libgsk-1.0.la
make_gskb_namespace_gskb_generated_LDADD = ../libgsk-1.0.la
parser-symbol-table.inc: make-parser-symbol-table
	./make-parser-symbol-table > parser-symbol-table.inc
gskb-namespace-gskb-generated.inc: make-gskb-namespace-gskb-generated
	./make-gskb-namespace-gskb-generated > gskb-namespace-gskb-generated.inc

gskb-parser-lemon.c gskb-parser-lemon.h: gskb-parser-lemon.lemon lemon/lemon
	lemon/lemon gskb-parser-lemon.lemon
