#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dpm.h"

DPM_CONF_DECLARE (verbose, "verbose", NULL, bool,
		  "Set this to true to enable more verbose output.")	  

DPM_CONF_DECLARE (debug, "debug", NULL, bool,
		  "Set this to true to enable debugging output.")	  

DPM_CONF_DECLARE (architecture, "architecture", NULL, string,
		  "The default architecture.")	  

DPM_CONF_DECLARE (source, "source", NULL, any,
		  "The source.")

void
doit (void *data)
{
  dpm_conf_parse ("foo.conf");
  dpm_conf_dump ();

#if 1
  dyn_begin ();
  dpm_conf_let ("verbose", dyn_from_string ("true"));
  dpm_print ("verbose: %V\n", dyn_get (&verbose));
  dyn_end ();

  dpm_print ("verbose: %V\n", dyn_get (&verbose));
#endif
}

int
main ()
{
  const char *error;

  error = dpm_catch_error (doit, NULL);
  if (error)
    printf ("%s\n", error);
  
  // Reset all variables.  No objects should remain alive.
  dyn_set (&verbose, NULL);
  dyn_set (&debug, NULL);
  dyn_set (&architecture, NULL);
  dyn_set (&source, NULL);

  return 0;
}
