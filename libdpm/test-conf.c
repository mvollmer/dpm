#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dpm.h"

#define L dyn_list
#define S dyn_from_string
#define E DYN_EOL

DPM_CONF_DECLARE (verbose, "verbose", bool,
		  "Set this to true to enable more verbose output.")

DPM_CONF_DECLARE (debug, "debug", bool,
		  "Set this to true to enable debugging output.")  

DPM_CONF_DECLARE (architecture, "architecture", string,
		  "The default architecture.")

DPM_CONF_DECLARE (source, "source", (seq string string ...),
		  "The source.")

void
doit (void *data)
{
  dpm_conf_parse ("foo.conf");
  dpm_conf_dump ();

  dyn_begin ();
  dpm_conf_let (verbose, dyn_from_string ("true"));
  dyn_print ("verbose: %V %d\n",
	     dpm_conf_get (verbose), dpm_conf_true (verbose));
  dyn_end ();

  dyn_print ("verbose: %V %d\n",
	     dpm_conf_get (verbose), dpm_conf_true (verbose));
}

int
main ()
{
  const char *error;

  dyn_begin ();

  error = dyn_catch_error (doit, NULL);
  if (error)
    printf ("%s\n", error);
  
#if 0
  // Reset all variables.  No objects should remain alive.
  dpm_conf_set (verbose, NULL);
  dpm_conf_set (debug, NULL);
  dpm_conf_set (architecture, NULL);
  dpm_conf_set (source, NULL);
#endif

  dyn_end ();

  return 0;
}
