CFLAGS = -Wall -O2 -g

AM_CFLAGS = -std=c99

lib_LTLIBRARIES = libdpm.la

libdpm_la_SOURCES = dyn.h     \
                    dyn.c     \
		    store.h   \
                    store.c   \
                    parse.h   \
                    parse.c   \
                    conf.h    \
                    conf.c    \
		    acq.h     \
		    acq.c     \
                    db.h      \
                    db.c      \
		    alg.h     \
		    alg.c     \
		    inst.h    \
	            inst.c

libdpm_la_LIBADD = -lz -lbz2

pkginclude_HEADERS = dpm.h dyn.h parse.h conf.h

bin_PROGRAMS = dpm-store-tool

dpm_store_tool_SOURCES = store-tool.c
dpm_store_tool_LDADD = libdpm.la

# Tests and their coverage

check_PROGRAMS = test
EXTRA_PROGRAMS = test-coverage

test_LDADD = libdpm.la -ldl
test_LDFLAGS = -Wl,--export-dynamic

test_coverage_SOURCES = test.c $(libdpm_la_SOURCES)
test_coverage_CFLAGS = -fprofile-arcs -ftest-coverage -std=c99
test_coverage_LDADD = $(libdpm_la_LIBADD) -ldl
test_coverage_LDFLAGS = -Wl,--export-dynamic

TESTS = $(shell grep ^DEFTEST test.c | sed 's/DEFTEST *(\(.*\))/\1/g')
TESTS_ENVIRONMENT = ./run-test
.PHONY: $(TESTS)

coverage: test-coverage
	rm -f *.gcda
	$(MAKE) check TESTPROG=test-coverage
	lcov -c -d . -o test-coverage.trace
	genhtml -o coverage test-coverage.trace

coverage-clean:
	rm -rf *.gcno *.gcda test-coverage.trace coverage test_coverage-*.o test-coverage

.PHONY: coverage

# Ad-hoc development aids

noinst_PROGRAMS = dpm

dpm_SOURCES = dpm.c
dpm_LDADD = libdpm.la

