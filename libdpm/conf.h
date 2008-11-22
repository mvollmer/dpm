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

#ifndef DPM_CONF_H
#define DPM_CONF_H

/* Dpm is configured via a set of key/value pairs.

   There usually is a hierachical structure imposed on key names by
   separating parts of the key name with ".", but that is mostly a
   feature of the configuration file parser.

   The value of a key is a vector of strings, similar to command line
   parameters.  There are a number of convenience functions to
   interpret the strings as various data types, such as booleans or
   numbers.

   Usually, the strings undergo variable substitution: substrings of
   the form "$key" or "${key}" are replaced with the strings of 'key'.
   The "$key" form must appear alone as one string in the vector, and
   the vector of strings of 'key' is spliced into the first vector in
   its stead.  The "${key}" form can also appear in the middle of a
   string: the substring is replaced with a new string, which is
   constructed from the strings of 'key' by concatenating them with
   spaces inbetween.  These substitutions are done at the time of
   access, so that you always get the current values.  (But some
   caching is done to avoid unnecessary computations.)

   The key/value pairs are maintained as an ordered list.  You can add
   to the beginning of that list, optionally under control of a
   dynamic extent.

   When getting the value of a key, the first entry in the ordered
   list with that key is used.  You can also enumerate all values for
   a given key.  This is used to form lists of values.

   Instead of hard coding default values in the code for keys that are
   not found in the configuration, you should provide the defaults by
   setting a initial list of key/value pairs.
 */

#endif /* !DPM_CONF_H */
