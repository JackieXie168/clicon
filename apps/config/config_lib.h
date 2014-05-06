/*
 * CVS Version: $Id: config_lib.h,v 1.5 2013/08/01 09:15:46 olof Exp $
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
 * Support functions for confif daemon
 */

#ifndef _CONFIG_LIB_H_
#define _CONFIG_LIB_H_

/* Nr of snapshots. Can be made into a dynamic option */
#define SNAPSHOTS_NR 30

/*
 * Prototypes
 */ 
int config_snapshot(struct db_spec *dbspec, char *dbname, char *dir);
int group_name2gid(char *name, gid_t *gid);

#endif  /* _CONFIG_LIB_H_ */
