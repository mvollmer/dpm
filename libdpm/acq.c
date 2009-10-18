/*
 * Copyright (C) 2008, 2009 Marius Vollmer <marius.vollmer@gmail.com>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */

#include <time.h>
#include <sys/stat.h>

#include "dyn.h"
#include "conf.h"
#include "acq.h"

DPM_CONF_DECLARE (cachedir, "cachedir",
		  "string", "cache",
		  "The location of the local cache.")

dyn_val
dpm_acq_local_name (const char *filename)
{
  if (filename
      && strlen (filename) > 7
      && strncmp (filename, "http://", 7) == 0)
    return dyn_format ("%s/%s", dpm_conf_string (cachedir), filename + 7);
  else if (filename
      && strlen (filename) > 8
      && strncmp (filename, "https://", 8) == 0)
    return dyn_format ("%s/%s", dpm_conf_string (cachedir), filename + 8);
  else
    return NULL;
}

time_t
dpm_acq_modification_time (const char *file)
{
  dyn_val local = dpm_acq_local_name (file);
  struct stat buf;
  if (stat (dyn_to_string (local), &buf) == 0)
    return buf.st_mtime;
  else
    return 0;
}

static int
dpm_system (const char *fmt, ...)
{
  dyn_block
    {
      va_list ap;
      va_start (ap, fmt);
      dyn_val cmd = dyn_formatv (fmt, ap);
      va_end (ap);

      // fprintf (stderr, "%s\n", cmd);
      return system (cmd);
    }
}

dpm_acq_code
dpm_acquire (const char *file)
{
  const char *dir = dpm_conf_string (cachedir);

  time_t mtime_before = dpm_acq_modification_time (file);

  if (dpm_system ("mkdir -p '%s'", dir) != 0)
    dyn_error ("Can't create cache directory %s", dir);

  if (dpm_system ("cd '%s' && wget --no-check-certificate -nv -m '%s'", dir, file) != 0)
    return DPM_ACQ_NOT_FOUND; // XXX - could be other errors, of course.

  time_t mtime_after = dpm_acq_modification_time (file);

  if (mtime_after == 0)
    return DPM_ACQ_NOT_FOUND;
  else if (mtime_before == mtime_after)
    return DPM_ACQ_UNCHANGED;
  else
    return DPM_ACQ_CHANGED;
}

dyn_input
dpm_acq_open (const char *file)
{
  dpm_acq_code code = dpm_acquire (file);
  if (code != DPM_ACQ_NOT_FOUND)
    return dpm_acq_open_local (file);
  else
    return NULL;
}

dyn_input
dpm_acq_open_local (const char *file)
{
  return dyn_open_file (dpm_acq_local_name (file));
}
