lib_LTLIBRARIES = libdpm.la

libdpm_la_SOURCES = dpm.h \
                    parse.c
libdpm_la_LIBADD = -lz

noinst_PROGRAMS = test

test_SOURCES = test.c
test_LDADD = libdpm.la
