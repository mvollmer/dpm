CFLAGS = -Wall -Werror -O2 -g

lib_LTLIBRARIES = libdpm.la

libdpm_la_SOURCES = dyn.h     \
                    dyn.c     \
		    store.h   \
                    store.c   \
                    parse.h   \
                    parse.c   \
                    conf.h    \
                    conf.c
libdpm_la_LIBADD = -lz -lbz2

pkginclude_HEADERS = dpm.h dyn.h parse.h conf.h

bin_PROGRAMS = dpm-store-tool
noinst_PROGRAMS = test test-store test-dyn test-conf

test_SOURCES = test.c
test_LDADD = libdpm.la

test_store_SOURCES = test-store.c
test_store_LDADD = libdpm.la

test_dyn_SOURCES = test-dyn.c
test_dyn_LDADD = libdpm.la

test_conf_SOURCES = test-conf.c
test_conf_LDADD = libdpm.la

dpm_store_tool_SOURCES = store-tool.c
dpm_store_tool_LDADD = libdpm.la
