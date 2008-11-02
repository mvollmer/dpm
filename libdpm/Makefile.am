lib_LTLIBRARIES = libdpm.la

libdpm_la_SOURCES = dpm.h \
                    parse.c
libdpm_la_LIBADD = -lz

noinst_PROGRAMS = test zpipe

test_SOURCES = test.c
test_LDADD = libdpm.la

zpipe_LDADD = -lz
