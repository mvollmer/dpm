#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dyn.h"

dyn_var var[1];

void
process (void *data)
{
  dyn_let (var, dyn_from_string ("ho"));
  dyn_error ("nothing to do for %s", dyn_to_string (dyn_get (var)));
}

int
main ()
{
  dyn_begin ();
  dyn_write (dyn_stdout, "Hi, %V\n", dyn_seq (dyn_from_string ("foo"),
					      dyn_from_string ("bar"),
					      DYN_EOS));
  dyn_write (dyn_stdout, "Hi, %V\n", dyn_format ("%s", "foo"));
  dyn_write (dyn_stdout, "Hi, %V\n", dyn_pair (dyn_from_string ("foo"),
					       dyn_from_string ("bar")));
  dyn_write (dyn_stdout, "Hi, %s\n", "foo");
  dyn_output_flush (dyn_stdout);
  dyn_write (dyn_stdout, "foo: %V\n", dyn_read (dyn_open_file ("foo.dyn")));
  dyn_print ("%V\n", dyn_read_string ("(foo bar baz)"));
  dyn_end ();

  int n = 0;

  while (n++ < 3)
    {
      dyn_val ball;

      dyn_begin ();
      dyn_set (var, dyn_from_string ("hi"));

      if ((ball = dyn_catch_error (process, 0)))
	dyn_write (dyn_stdout, "caught: %s\n", dyn_to_string (ball));

      dyn_write (dyn_stdout, "main %s\n", dyn_to_string (dyn_get (var)));

      dyn_output_flush (dyn_stdout);

      dyn_end ();
    }

  dyn_set (var, NULL);

  return 0;
}
