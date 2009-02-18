#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dpm.h"

#define L dyn_list
#define S dyn_from_string
#define E DYN_EOL

DPM_CONF_DECLARE (verbose, "verbose",
		  (if (value false) () (value true)),
		  "Set this to true to enable more verbose output.")

DPM_CONF_DECLARE (debug, "debug",
		  (if (value false) () (value true)),
		  "Set this to true to enable debugging output.")	  

DPM_CONF_DECLARE (architecture, "architecture",
		  string,
		  "The default architecture.")	  

DPM_CONF_DECLARE (source, "source",
		  (list string string (defaulted string main)),
		  "The source.")

void
doit (void *data)
{
  dpm_conf_parse ("foo.conf");
  dpm_conf_dump ();

#if 1
  dyn_begin ();
  dpm_conf_let ("verbose", dyn_from_string ("true"));
  dyn_print ("verbose: %V\n", dyn_get (&verbose));
  dyn_end ();

  dyn_print ("verbose: %V\n", dyn_get (&verbose));
#endif
}

int
main ()
{
  const char *error;

  dyn_begin ();

  error = dyn_catch_error (doit, NULL);
  if (error)
    printf ("%s\n", error);
  
  // Reset all variables.  No objects should remain alive.
  dyn_set (&verbose, NULL);
  dyn_set (&debug, NULL);
  dyn_set (&architecture, NULL);
  dyn_set (&source, NULL);

  dyn_end ();

  return 0;
}
