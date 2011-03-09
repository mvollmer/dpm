CFLAGS = -Wall -O2 -g

AM_CFLAGS = -std=c99

lib_LTLIBRARIES = libdpm.la

libdpm_la_SOURCES = dyn.h     \
                    dyn.c     \
		    store.h   \
                    store.c   \
                    parse.h   \
                    parse.c   \
                    db.h      \
                    db.c      \
		    ws.h      \
		    ws.c      \
                    alg.h     \
                    alg.c     \
                    pol.h     \
                    pol.c

libdpm_la_LIBADD = -lz # -lbz2

pkginclude_HEADERS = dpm.h dyn.h store.h parse.h db.h ws.h alg.h

bin_PROGRAMS = dpm-tool

dpm_tool_SOURCES = tool.c
dpm_tool_LDADD = libdpm.la

# Tests and their coverage

check_PROGRAMS = test
EXTRA_PROGRAMS = test-coverage

test_SOURCES = test.c testlib.h testlib.c
test_LDADD = libdpm.la -ldl
test_LDFLAGS = -Wl,--export-dynamic

test_coverage_SOURCES = $(test_SOURCES) $(libdpm_la_SOURCES)
test_coverage_CFLAGS = -fprofile-arcs -ftest-coverage -std=c99
test_coverage_LDADD = $(libdpm_la_LIBADD) -ldl
test_coverage_LDFLAGS = -Wl,--export-dynamic

TESTS = $(shell grep ^DEFTEST $(srcdir)/test.c | sed 's/DEFTEST *(\(.*\))/\1/g')
TESTS_ENVIRONMENT = $(srcdir)/run-test
.PHONY: $(TESTS)

coverage: test-coverage
	rm -f *.gcda
	$(MAKE) check TESTPROG=test-coverage
	lcov -c -d . -o test-coverage.trace
	genhtml -o coverage test-coverage.trace

coverage-clean:
	rm -rf *.gcno *.gcda test-coverage.trace coverage test_coverage-*.o test-coverage

.PHONY: coverage

EXTRA_DIST = run-test 				\
	     test-data/numbers.txt 		\
	     test-data/numbers.gz 		\
	     test-data/numbers.bz2 		\
	     test-data/sgb-words.txt   		\
	     test-data/contiguous-usa.dat       \
             test-data/lines.txt                \
             test-data/control.txt              \
             test-data/pkg.deb                  \
             test-data/src.tar

DISTCLEANFILES = test-data/output.txt \
	 	 test-data/store.db   \
                 test-data/test.db
