/*
 *  CVS Version: $Id: clicon_db.h,v 1.5 2013/08/01 09:15:46 olof Exp $
 *
  Copyright (C) 2009-2014 Olof Hagsand and Benny Holmgren
  Olof Hagsand
 *
  This file is part of CLICON.

  CLICON is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  CLICON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with CLICON; see the file COPYING.  If not, see
  <http://www.gnu.org/licenses/>.

 */

#ifndef _CLICON_DB_H_
#define _CLICON_DB_H_

struct db_pair {    
    char *dp_key;  /* database key */
    char *dp_matched; /* Matched component of key */
    char *dp_val;  /* pointer to vector of lvalues */
    int dp_vlen;   /* length of vector of lvalues */
};

/*
 * Prototypes
 */ 
int db_init(char *file);

int db_set(char *file, char *key, void *data, size_t datalen);

int db_get(char *file, char *key, void *data, size_t *datalen);

int db_get_alloc(char *file, char *key, void **data, size_t *datalen);

int db_del(char *file, char *key);

int db_regexp(char *file, char *regexp, const char *label, 
	      struct db_pair **pairs, int noval);

char *db_sanitize(char *rx, const char *label);

#endif  /* _CLICON_DB_H_ */
