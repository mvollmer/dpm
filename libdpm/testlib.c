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

#define _GNU_SOURCE

#include "testlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <dlfcn.h>

static void
default_failure_printer (const char *file, int line, 
			 const char *fmt, va_list ap)
{
  fprintf (stderr, "%s:%d: ", file, line);
  vfprintf (stderr, fmt, ap);
  fprintf (stderr, "\n");
}

static void (*failure_printer) (const char *file, int line,
				const char *fmt, va_list ap) =
  default_failure_printer;

void
set_failure_printer (void (*printer) (const char *file, int line,
				      const char *fmt, va_list ap))
{
  failure_printer = printer;
}

void
expect (int b, char *expr, char *file, int line,
	const char *fmt, ...)
{
  if (!b)
    {
      if (fmt)
	{
	  va_list ap;
	  va_start (ap, fmt);
	  failure_printer (file, line, fmt, ap);
	  va_end (ap);
	}
      else
	failure_printer (file, line, "Expected %s", expr);
      exit (1);
    }
}

#define BUFSIZE (1024*1024)

static void
handle_fd (fd_set *fds, int *fd, char *buf, int *len)
{
  if (FD_ISSET (*fd, fds))
    {
      int n = read (*fd, buf+(*len), BUFSIZE-(*len));
      if (n < 0)
	{
	  fprintf (stderr, "Can't read: %m\n");
	  exit (1);
	}
      else if (n == 0)
	{
	  close (*fd);
	  *fd = -1;
	}
      else
	{
	  *len += n;
	  if (*len >= BUFSIZE)
	    {
	      fprintf (stderr, "Too much output from child.\n");
	      exit (1);
	    }
	}
    }
}

bool
expect_child (void (*check) (int status,
			     char *stdout_text, int stdout_len,
			     char *stderr_text, int stderr_len,
			     char *file, int line,
			     va_list args),
	      char *file, int line,
	      ...)
{
  int stdout_pipe[2], stderr_pipe[2];
  char *stdout_buf, *stderr_buf;
  int stdout_len, stderr_len;

  va_list args;
  va_start (args, line);

  if (pipe (stdout_pipe) < 0
      || pipe (stderr_pipe) < 0)
    {
      fprintf (stderr, "Can't pipe: %m\n");
      exit (1);
    }

  pid_t p = fork ();
  if (p < 0)
    {
      fprintf (stderr, "Can't fork: %m\n");
      exit (1);
    }

  if (p == 0)
    {
      close (stdout_pipe[0]);
      close (stderr_pipe[0]);
      dup2 (stdout_pipe[1], 1);
      dup2 (stderr_pipe[1], 2);
      return 1;
    }

  close (stdout_pipe[1]);
  close (stderr_pipe[1]);

  stdout_buf = malloc (BUFSIZE);
  stdout_len = 0;
  stderr_buf = malloc (BUFSIZE);
  stderr_len = 0;

  while (stdout_pipe[0] > 0 || stderr_pipe[0] > 0)
    {
      fd_set fds;
      FD_ZERO (&fds);
      if (stdout_pipe[0] > 0)
	FD_SET (stdout_pipe[0], &fds);
      if (stderr_pipe[0] > 0)
	FD_SET (stderr_pipe[0], &fds);

      if (select (FD_SETSIZE, &fds, NULL, NULL, NULL) < 0)
	{
	  fprintf (stderr, "Can't select: %m\n");
	  exit (1);
	}

      handle_fd (&fds, &stdout_pipe[0], stdout_buf, &stdout_len);
      handle_fd (&fds, &stderr_pipe[0], stderr_buf, &stderr_len);
    }

  int status;
  while (waitpid (p, &status, 0) < 0)
    {
      fprintf (stderr, "Can't waitpid: %m\n");
      exit (1);
    }

  check (status,
	 stdout_buf, stdout_len,
	 stderr_buf, stderr_len,
	 file, line,
	 args);

  free (stdout_buf);
  free (stderr_buf);

  return 0;
}

static void
check_output (char *label, char *text, int len, char *expected,
	      char *file, int line)
{
  if (len != strlen (expected)
      || strncmp (text, expected, len))
    {
      fprintf (stderr, "%s:%d: Unexpected %s:\n", file, line, label);
      fprintf (stderr, ">%.*s<\n", len, text);
      if (strlen (expected) > 0)
	{
	  fprintf (stderr, "Expected:\n");
	  fprintf (stderr, ">%s<\n", expected);
	}
      exit (1);
    }
}

void
check_status_abort (int status,
		    char *stdout_text, int stdout_len,
		    char *stderr_text, int stderr_len,
		    char *file, int line,
		    va_list args)
{
  char *expected_stderr = va_arg (args, char *);

  if (!WIFSIGNALED (status) || WTERMSIG (status) != SIGABRT)
    {
      fprintf (stderr, "%s:%d: Expected abort\n", file, line);
      exit (1);
    }

  check_output ("stdout", stdout_text, stdout_len, "", file, line);
  check_output ("stderr", stderr_text, stderr_len,
		expected_stderr, file, line);
}

void
check_status_exit (int status,
		   char *stdout_text, int stdout_len,
		   char *stderr_text, int stderr_len,
		   char *file, int line,
		   va_list args)
{
  int code = va_arg (args, int);
  char *expected_stderr = va_arg (args, char *);

  if (!WIFEXITED (status) || WEXITSTATUS (status) != code)
    {
      fprintf (stderr, "%s:%d: Expected exit(1)\n", file, line);
      exit (1);
    }

  check_output ("stdout", stdout_text, stdout_len, "", file, line);
  check_output ("stderr", stderr_text, stderr_len,
		expected_stderr, file, line);
}

int
test_main (int argc, char **argv)
{
  if (argc != 2)
    {
      fprintf (stderr, "Usage: %s TEST\n", argv[0]);
      exit (1);
    }

  char *name;
  asprintf (&name, "test_%s", argv[1]);
  void (*func)() = dlsym (NULL, name);

  if (func)
    {
      func ();
      exit (0);
    }
  else
    {
      fprintf (stderr, "Test %s is not defined.\n", argv[1]);
      exit (1);
    }
}
