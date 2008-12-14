#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dpm.h"

DPM_CONF_DECLARE (verbose, "verbose", "false", bool,
		  "Set this to true to enable more verbose output.")	  

DPM_CONF_DECLARE (debug, "debug", "false", bool,
		  "Set this to true to enable debugging output.")	  

int
main ()
{
  dpm_conf_parse ("foo.conf");
  dpm_conf_dump ();

  dyn_begin ();
  dpm_conf_let ("verbose", "true");
  
  printf ("verbose: %d\n", dyn_get (&verbose));

  dyn_end ();

  printf ("verbose: %d\n", dyn_get (&verbose));
}
