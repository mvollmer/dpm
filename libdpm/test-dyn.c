#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dyn.h"
#include "util.h"

dyn_var var;

dyn_condition error_cond = {
 .name = "error",
};

void
error (const char *fmt, ...)
{
  char *message;
  va_list ap;
  va_start (ap, fmt);
  message = dpm_vsprintf (fmt, ap);
  va_end (ap);
  dyn_val val = dyn_from_string (message);
  free (message);
  dyn_throw (&error_cond, val);
}

void
process (void *data)
{
  dyn_let (&var, dyn_from_string ("ho"));
  error ("nothing to do for %s", dyn_to_string (dyn_get (&var)));
}

int
main ()
{
  int n = 0;

  while (n++ < 3)
    {
      dyn_val ball;

      dyn_begin ();
      dyn_set (&var, dyn_from_string ("hi"));

      if ((ball = dyn_catch (&error_cond, process, 0)))
	printf ("caught: %s\n", dyn_to_string (ball));

      printf ("main %s\n", dyn_to_string (dyn_get (&var)));

      dyn_end ();
    }

  dyn_set (&var, NULL);

  return 0;
}
