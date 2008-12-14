#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dyn.h"
#include "util.h"

dyn_var var;

void
my_free (void *value)
{
  fprintf (stderr, "freeing %p\n", value);
  free (value);
}

dyn_condition error_cond = {
 .name = "error",
 .free = my_free
};

void
error (const char *fmt, ...)
{
  char *message;
  va_list ap;
  va_start (ap, fmt);
  message = dpm_vsprintf (fmt, ap);
  va_end (ap);
  dyn_throw (&error_cond, message);
}

void
process (void *data)
{
  dyn_let (&var, "ho");
  error ("nothing to do for %s", dyn_get (&var));
}

int
main ()
{
  int n = 0;

  while (n++ < 3)
    {
      void *ball;

      dyn_begin ();
      dyn_set (&var, "hi");

      if (ball = dyn_catch (&error_cond, process, 0))
	printf ("caught: %s\n", ball);

      printf ("main %s\n", dyn_get (&var));

      dyn_end ();
    }
}
