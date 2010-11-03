/*
 * Copyright (C) 2010 Marius Vollmer <marius.vollmer@gmail.com>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */

#ifndef TESTLIB_H
#define TESTLIB_H

#include <stdarg.h>
#include <stdbool.h>

#define DEFTEST(x) void test_##x ()
#define EXPECT(expr) expect((expr), #expr, __FILE__, __LINE__)
#define EXPECT_CHILD(check, args...)			\
  if(expect_child (check, __FILE__, __LINE__, ##args))	\
    for (; true; exit(0))

#define EXPECT_ABORT(t)      EXPECT_CHILD (check_status_abort, t)
#define EXPECT_EXIT          EXPECT_CHILD (check_status_exit, 1, "")
#define EXPECT_STDERR(c, t)  EXPECT_CHILD (check_status_exit, c, t)

void expect (int b, char *msg, char *file, int line);

bool expect_child (void (*check) (int status,
				  char *stdout_text, int stdout_len,
				  char *stderr_text, int stderr_len,
				  char *file, int line,
				  va_list args),
		   char *file, int line,
		   ...);

void check_status_abort (int status,
			 char *stdout_text, int stdout_len,
			 char *stderr_text, int stderr_len,
			 char *file, int line,
			 va_list args);

void check_status_exit (int status,
			char *stdout_text, int stdout_len,
			char *stderr_text, int stderr_len,
			char *file, int line,
			va_list args);

int test_main (int argc, char **argv);

#endif
