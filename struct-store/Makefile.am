INCLUDES = -I ../libdpm

lib_LTLIBRARIES = libstructstore.la

libstructstore_la_SOURCES = struct-store.h \
                            struct-store.c

bin_PROGRAMS = st-tool
noinst_PROGRAMS = test

test_SOURCES = test.c
test_LDADD = libstructstore.la ../libdpm/libdpm.la

st_tool_SOURCES = st-tool.c
st_tool_LDADD = libstructstore.la
