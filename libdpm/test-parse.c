#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dyn.h"
#include "parse.h"

void
relation (dyn_input in,
	  const char *name, int name_len,
	  const char *op, int op_len,
	  const char *version, int version_len,
	  void *data)
{
  printf ("'%.*s' '%.*s' '%.*s'\n", name_len, name, op_len, op, version_len, version);
}

int
main (int argc, char **argv)
{
  dyn_input in = dyn_open_string (argv[1], -1);
  
  while (dpm_parse_relation (in, relation, NULL))
    printf ("--\n");
}
