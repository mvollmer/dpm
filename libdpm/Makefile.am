lib_LTLIBRARIES = libdpm.la

libdpm_la_SOURCES = dpm.h \
		    util.h \
		    util.c \
                    stream.c \
                    parse.c
libdpm_la_LIBADD = -lz -lbz2

noinst_PROGRAMS = test

test_SOURCES = test.c
test_LDADD = libdpm.la
