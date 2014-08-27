/*
 *
  Copyright (C) 2009-2014 Olof Hagsand and Benny Holmgren

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

 *
 */

#ifndef _CONFIG_HANDLE_H_
#define _CONFIG_HANDLE_H_

/*
 * Prototypes 
 * not exported.
 */
/* backend handles */
clicon_handle backend_handle_init(void);

int backend_handle_exit(clicon_handle h);

dbdep_t *backend_dbdep(clicon_handle h); 

int backend_dbdep_set(clicon_handle h, dbdep_t *dbdep);

struct client_entry *backend_client_add(clicon_handle h, struct sockaddr *addr);

struct client_entry *backend_client_list(clicon_handle h);

#endif  /* _CONFIG_HANDLE_H_ */
