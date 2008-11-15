#include "dyn.h"

dpm_dyn_var var;

void
process (void *data)
{
  dpm_dyn_set (&var, "ho");
  dpm_dyn_error ("nothing to do for %s", dpm_dyn_get (&var));
}

int
main ()
{
  int n = 0;

  while (n++ < 3)
    {
      char *message;

      dpm_dyn_begin ();
      dpm_dyn_set (&var, "hi");

      if (message = dpm_dyn_catch (process, 0))
	{
	  printf ("caught: %s\n", message);
	  free (message);
	}

      printf ("main %s\n", dpm_dyn_get (&var));

      dpm_dyn_end ();
    }
}
