#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dpm.h"

DPM_CONF_DECLARE (verbose, "verbose", bool,
		  "Set this to true to enable more verbose output.")

DPM_CONF_DECLARE (debug, "debug", bool,
		  "Set this to true to enable debugging output.")

DPM_CONF_DECLARE (architectures, "architectures", (seq string ...),
		  "The list of architectures.")

DPM_CONF_DECLARE (architecture, "architecture", string,
		  "The default architecture.")

DPM_CONF_DECLARE (distributions, "distributions", (seq string ...),
		  "The list of distributions.")

DPM_CONF_DECLARE (mirror, "mirror", string,
		  "The mirror to use.")

int
main ()
{
  dpm_conf_parse ("foo.conf");
  dpm_conf_dump ();

  return 0;
}
