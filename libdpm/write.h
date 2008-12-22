/*
 * Copyright (C) 2008 Marius Vollmer <marius.vollmer@gmail.com>
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

#ifndef DPM_WRITE_H
#define DPM_WRITE_H

#include <stdio.h>
#include <stdarg.h>

#include "store.h"

/* Convenient writing of struct-store values and other things.

   These functions use the familiar printf-style formatting codes, but
   they use a much smaller set:

   %s -- nul-terminated string, char *
   %S -- nul-terminated string, char *, quoted
   %d -- decimal signed integer, int
   %v -- store value, dispatches
   %V -- store value, quoted, dispatches
   %c -- confval
   %C -- confval, quoted
 */

void dpm_write (FILE *, const char *fmt, ...);
void dpm_writev (FILE *, const char *fmt, va_list ap);

void dpm_print (const char *fmt, ...);

void dpm_register_tag_writer (int tag,
			      void (*func) (FILE *, ss_val, int quoted));

#endif /* !DPM_WRITE_H */
