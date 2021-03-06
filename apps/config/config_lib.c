/*
 *
  Copyright (C) 2009-2015 Olof Hagsand and Benny Holmgren

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

#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <grp.h>


/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clicon/clicon.h>

#include "config_lib.h"


/* 
 * config_snapshot
 * dump old running_db to IOS snapshot file #0
 * and move all other checkpoints
 * one step up
 */
int
config_snapshot(dbspec_key *dbspec, char *dbname, char *dir)
{
    char filename0[MAXPATHLEN];
    char filename1[MAXPATHLEN];
    struct stat st;
    int i;

    if (stat(dir, &st) < 0){
	clicon_err(OE_CFG, errno, "%s: stat(%s): %s\n", 
		__FUNCTION__, dir, strerror(errno));
	return -1;
    }
    if (!S_ISDIR(st.st_mode)){
	clicon_err(OE_CFG, 0, "%s: %s: not directory\n", 
		__FUNCTION__, dir);
	return -1;
    }
    for (i=SNAPSHOTS_NR-1; i>0; i--){
	snprintf(filename0, MAXPATHLEN, "%s/%d", 
		 dir,
		 i-1);
	snprintf(filename1, MAXPATHLEN, "%s/%d", 
		 dir,
		 i);
	if (stat(filename0, &st) == 0)
	    if (rename(filename0, filename1) < 0){
		clicon_err(OE_CFG, errno, "%s: rename(%s, %s): %s\n", 
			__FUNCTION__, filename0, filename1, strerror(errno));
		return -1;
	    }
    }
    /* Make the most current snapshot */
    snprintf(filename0, MAXPATHLEN, "%s/0", dir);
    return save_db_to_xml(filename0, dbspec, dbname, 0);
}

