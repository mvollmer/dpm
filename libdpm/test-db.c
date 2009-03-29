#include "db.h"

int
main (int argc, char **argv)
{
  dpm_db_open ();

  if (argc > 1)
    {
      for (int i = 1; i < argc; i++)
	{
	  dpm_db_update_packages (argv[i]);
	  dpm_db_checkpoint ();
	}
    }
  else
    dpm_db_dump ();

  dpm_db_done ();

  return 0;
}
