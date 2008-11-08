lib_LTLIBRARIES = libdpm.la

libdpm_la_SOURCES = util.h \
		    util.c \
		    store.h \
                    store.c \
                    stream.h \
                    stream.c \
                    parse.h \
                    parse.c
libdpm_la_LIBADD = -lz -lbz2

noinst_PROGRAMS = test test-store store-tool

test_SOURCES = test.c
test_LDADD = libdpm.la

test_store_SOURCES = test-store.c
test_store_LDADD = libdpm.la

store_tool_SOURCES = store-tool.c
store_tool_LDADD = libdpm.la
