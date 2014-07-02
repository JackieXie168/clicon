/*
 *  CVS Version: $Id: clicon_spec.c,v 1.29 2013/09/19 16:03:40 olof Exp $
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

  */

#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#define __USE_GNU /* strverscmp */
#include <string.h>
#include <arpa/inet.h>
#include <regex.h>
#include <syslog.h>
#include <assert.h>
#include <netinet/in.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include "clicon_log.h"
#include "clicon_err.h"
#include "clicon_string.h"
#include "clicon_queue.h"
#include "clicon_hash.h"
#include "clicon_handle.h"
#include "clicon_spec.h"
#include "clicon_hash.h"
#include "clicon_lvalue.h"
#include "clicon_lvmap.h"
#include "clicon_chunk.h"
#include "clicon_options.h"
#include "clicon_dbutil.h"
#include "clicon_dbspec.h"
#include "clicon_yang_parse.h"

/*! Parse a string containing a YANG spec into a parse-tree
 * 
 * Syntax parsing. A string is input and a syntax-tree is returned (or error). 
 * A variable record is also returned containing a list of (global) variable values.
 * (cloned from cligen)
 */
static int
clicon_yang_parse_str(clicon_handle h,
		 char *str,
		 const char *name, /* just for errs */
		 dbspec_tree *pt,
		 cvec *vr
    )
{
    int                retval = -1;
    int                i;
    struct clicon_yang_yacc_arg ya = {0,};
    dbspec_obj            *co;
    dbspec_obj             co0; /* tmp top object: NOT malloced */
    dbspec_obj            *co_top = &co0;

    memset(&co0, 0, sizeof(co0));
    ya.ya_handle       = h; 
    ya.ya_name         = (char*)name;
    ya.ya_linenum      = 1;
    ya.ya_parse_string = str;
    ya.ya_stack        = NULL;
    co_top->do_pt      = *pt;
    if (vr)
	ya.ya_globals       = vr; 
    else
	if ((ya.ya_globals = cvec_new(0)) == NULL){
	    fprintf(stderr, "%s: malloc: %s\n", __FUNCTION__, strerror(errno)); 
	    goto done;
	}

    if (strlen(str)){ /* Not empty */
	if (yang_scan_init(&ya) < 0)
	    goto done;
	if (yang_parse_init(&ya, co_top) < 0)
	    goto done;
	if (clicon_yang_parseparse(&ya) != 0) {
	    yang_parse_exit(&ya);
	    yang_scan_exit(&ya);
	    goto done;
	}
	if (yang_parse_exit(&ya) < 0)
	    goto done;		
	if (yang_scan_exit(&ya) < 0)
	    goto done;		
    }
    if (vr)
	vr= ya.ya_globals;
    else
	cvec_free(ya.ya_globals);
    /*
     * Remove the fake top level object and remove references to it.
     */
    *pt = co_top->do_pt;
    for (i=0; i<co_top->do_max; i++){
	co=co_top->do_next[i];
	if (co)
	    co_up_set2(co, NULL);
    }
    retval = 0;
  done:
    return retval;

}



/*! Parse a file containing a YANG into a parse-tree
 *
 * Similar to clicon_yang_str(), just read a file first
 * (cloned from cligen)
 * The database symbols are inserted in alphabetical order.
 */
static int
clicon_yang_parse_file(clicon_handle h,
			 FILE *f,
			 const char *name, /* just for errs */
			 dbspec_tree *pt,
			 cvec *globals)
{
    char         *buf;
    int           i;
    int           c;
    int           len;
    int           retval = -1;

    len = 1024; /* any number is fine */
    if ((buf = malloc(len)) == NULL){
	perror("pt_file malloc");
	return -1;
    }
    memset(buf, 0, len);

    i = 0; /* position in buf */
    while (1){ /* read the whole file */
	if ((c =  fgetc(f)) == EOF)
	    break;
	if (len==i){
	    if ((buf = realloc(buf, 2*len)) == NULL){
		fprintf(stderr, "%s: realloc: %s\n", __FUNCTION__, strerror(errno));
		goto done;
	    }	    
	    memset(buf+len, 0, len);
	    len *= 2;
	}
	buf[i++] = (char)(c&0xff);
    } /* read a line */
    if (clicon_yang_parse_str(h, buf, name, pt, globals) < 0)
	goto done;
    retval = 0;
  done:
    if (buf)
	free(buf);
    return retval;
}

/*! Parse dbspec using yang format
 *
 * The database symbols are inserted in alphabetical order.
 */
int
yang_parse(clicon_handle h, const char *filename, dbspec_tree *pt)
{
    FILE       *f;
    cvec       *cvec = NULL;   /* global variables from syntax */
    int         retval = -1;
    char       *name;

    if ((f = fopen(filename, "r")) == NULL){
	clicon_err(OE_UNIX, errno, "fopen(%s)", filename);	
	goto done;
    }
    if ((cvec = cvec_new(0)) == NULL){ /* global variables from syntax */
	clicon_err(OE_UNIX, errno, "cvec_new()");	
	goto done;
    }   
    if (clicon_yang_parse_file(h, f, filename, pt, cvec) < 0)
	goto done;
    /* pick up name="myname"; from spec */
    if ((name = cvec_find_str(cvec, "name")) != NULL)
	clicon_dbspec_name_set(h, name);
    retval = 0;
  done:
    if (cvec)
	cvec_free(cvec);
    if (f)
	fclose(f);
    return retval;
}
