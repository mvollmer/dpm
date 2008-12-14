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

#ifndef DPM_UTIL_H
#define DPM_UTIL_H

#include <stddef.h>
#include <stdarg.h>

void *dpm_xmalloc (size_t size);
void *dpm_xremalloc (void *old, size_t size);
void *dpm_xstrdup (const char *str);
void *dpm_xstrndup (const char *str, int n);

char *dpm_sprintf (const char *fmt, ...);
char *dpm_vsprintf (const char *fmt, va_list ap);

char *dpm_catch_error (void (*func) (void *data), void *data);
void dpm_error (const char *fmt, ...);

void dpm_let_error_context (char *(*func) (const char *message, int level,
					   void *data),
			    void *data);

#endif /* !DPM_UTIL_H */
