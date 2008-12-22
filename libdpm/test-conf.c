#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dpm.h"

DPM_CONF_DECLARE (verbose, "verbose", "false", bool,
		  "Set this to true to enable more verbose output.")	  

DPM_CONF_DECLARE (debug, "debug", "false", bool,
		  "Set this to true to enable debugging output.")	  

DPM_CONF_DECLARE (architecture, "architecture", NULL, string,
		  "The default architecture.")	  

DPM_CONF_DECLARE (source, "source", NULL, string_array,
		  "The source.")	  

void
doit (void *data)
{
  dpm_conf_parse ("foo.conf");
  dpm_conf_dump ();

  dyn_begin ();
  dpm_conf_let ("verbose", "true");
  
  printf ("verbose: %d\n", dyn_get (&verbose));

  dyn_end ();

  printf ("verbose: %d\n", dyn_get (&verbose));
}

int
main ()
{
  char *error;

  dyn_begin ();
  if (error = dpm_catch_error (doit, NULL))
    printf ("%s\n", error);
  dyn_end ();
}
