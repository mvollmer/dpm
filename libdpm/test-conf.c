#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dpm.h"

DPM_CONF_DECLARE (verbose, "verbose",
		  "bool", "false",
		  "Set this to true to enable more verbose output.")

DPM_CONF_DECLARE (debug, "debug",
		  "bool", "false",
		  "Set this to true to enable debugging output.")

DPM_CONF_DECLARE (architectures, "architectures",
		  "(seq string ...)", "(host)",
		  "The list of architectures.")

DPM_CONF_DECLARE (architecture, "architecture",
		  "string", "host",
		  "The default architecture.")

DPM_CONF_DECLARE (distributions, "distributions",
		  "(seq string ...)", "(stable)",
		  "The list of distributions.")

DPM_CONF_DECLARE (mirror, "mirror",
		  "string", NULL,
		  "The mirrors to use.")

int
main ()
{
  dpm_conf_parse ("foo.conf");
  dpm_conf_dump ();

  return 0;
}
