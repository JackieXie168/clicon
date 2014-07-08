/*
 *  CVS Version: $Id: cli_generate.c,v 1.15 2013/09/18 19:20:50 olof Exp $
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
 *     db_spec                      parse_tree                    parse_tree
 *  +-------------+ dbspec_key2cli +-------------+ dbspec2cli    +-------------+
 *  |  dbspec     | -------------> | dbclispec   | ------------> | cli         |
 *  |  A[].B !$a  | dbspec_cli2key | A <!a>{ B;} |               | syntax      |
 *  +-------------+ <------------  +-------------+               +-------------+
 *        ^                               ^
 *        |db_spec_parse_file             | dbclispec_parse
 *        |                               |
 *      <file>                          <file>
 */
#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/param.h>


/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clicon/clicon.h>

#include "clicon_cli_api.h"
#include "cli_plugin.h"
#include "cli_generate.h"

#ifdef USE_DBSPEC_PT


/*
 * dbspec2cli_co
 * Translate from a database specification (cli form) into a cligen syntax.
 * co sub-fn of dbspec2cli.
 * Take a cli spec tree and transform it into a cligen syntax tree.
 * More specifically, for every non-index variable, make a 'merge' rule.
 */


/*
 * Find variable in pt with a certain name 
 * very similar to co_find_one(), only Variable-check differs.
 */
static dbspec_obj *
find_index_variable(dbspec_tree *pt, char *name)
{
    int i;
    dbspec_obj *co = NULL;

    for (i=0; i<pt->dt_len; i++){
	co = pt->dt_vec[i];
	if (co && 
	    co->do_type == CO_VARIABLE && 
	    strcmp(name, co->do_command)==0)
	    break; /* found */
	co = NULL; /* only inde variable should be cov */
    }
    return co;
}


int
print_symbol(xf_t *xf, dbspec_obj *co)
{
    xprintf(xf, " %s", co->do_command);
    if (co->do_help)
	xprintf(xf, "(\"%s\")", co->do_help);
    return 0;
}

/* forward */
static int
dbspec2cli_co(clicon_handle h, xf_t *xf, dbspec_obj *co, enum genmodel_type gt);

static int
dbspec2cli_co_cmd(clicon_handle h, xf_t *xf, dbspec_obj *co, enum genmodel_type gt)
{
    int         retval = -1;
    dbspec_tree *pt;
    dbspec_obj    *cov;
    dbspec_obj    *co1;
    int         i;
    int         subs; /* yes, there are children */
    char       *ivar;
    
    pt = &co->do_pt;
    print_symbol(xf, co);
    cov = NULL;
    if ((ivar = dbspec_indexvar_get(co)) != NULL){
	/* Find the index variable */
	if ((cov = find_index_variable(pt, ivar)) == NULL){
	    clicon_err(OE_DB, errno, "%s: %s has no indexvariable %s\n", 
		       __FUNCTION__, co->do_command, ivar); 
	    goto done;
	}
	if (gt == GT_ALL) /* Add extra keyword for index variable */
	    print_symbol(xf, cov);
	if (dbspec2cli_co(h, xf, cov, gt) < 0)
	    goto done;
    }
    else{
	if (dbspec_key_get(co))
	    xprintf(xf, ",cli_set(\"%s\")", dbspec_key_get(co));
	xprintf(xf, ";");	    
#if 0 /* obsolete? */
	struct cg_callback *cc;
	for (cc = co->co_callbacks; cc; cc=cc->cc_next){
	    fprintf(stderr, "fn(");
	    if (cc->cc_arg)
		cv_print(stderr, cc->cc_arg);
	    fprintf(stderr, ")");
	}
	fprintf(stderr, "\n");
#endif
    }
    subs = 0; /* Loop thru all other children (skip indexvariable) */
    for (i=0; i<pt->dt_len; i++){ 
	if ((co1 = pt->dt_vec[i]) == NULL)
	    continue;
	if (cov && co1 == cov) /* skip index variable */
	    continue;
	/* Avoid empty brackets: {} by ensuring there are subs */
	if (!subs){
	    xprintf(xf, "{");
	    subs++;
	}
	/* Add extra keyword regular variables */
	if (co1->do_type == CO_VARIABLE &&
	    (gt == GT_ALL || gt == GT_VARS))
	    print_symbol(xf, co1);
	if (dbspec2cli_co(h, xf, co1, gt) < 0)
	    goto done;
    }
    if (subs)
	xprintf(xf, "}");
    retval = 0;
  done:
    return retval;
}

static int 
mycov_print(xf_t *xf, dbspec_obj *co, int brief)
{
    if (co->do_choice){
	xprintf(xf, "<type:string choice:%s>", co->do_choice);
    }
    else{
	if (brief)
	    xprintf(xf, "<%s>", co->do_command);   
	else{
	    xprintf(xf, "<%s:%s", co->do_command, cv_type2str(co->do_vtype));
	    if (co->do_range){
		xprintf(xf, " range[%" PRId64 ":%" PRId64 "]", 
			co->do_range_low, co->do_range_high);
	    }
	    if (co->do_regex){
		xprintf(xf, " regexp:\"%s\"", co->do_regex);
	    }
	    xprintf(xf, ">");
	}
    }

    return 0;
}


static int
dbspec2cli_co_var(clicon_handle h, xf_t *xf, dbspec_obj *co, enum genmodel_type gt)
{
    char *s0, *key;
    int retval = -1;

    /* In some cases we should add keyword before variables */	
    xprintf(xf, " (");
    mycov_print(xf, co, 0);
    if (co->do_help)
	xprintf(xf, "(\"%s\")", co->do_help);
    if (clicon_cli_genmodel_completion(h)){
	char *ds = dbspec_key_get(co);
	assert(ds); 
	if ((s0 = strdup(ds)) == NULL){
	    clicon_err(OE_DB, errno, "%s: strdup\n", __FUNCTION__); 
	    goto done;
	}
	key = strtok(s0, " ");
	if (key){
	    xprintf(xf, "|");
	    xprintf(xf, "<%s:%s expand_dbvar_auto(\"candidate %s\")>",
		    co->do_command, 
		    cv_type2str(co->do_vtype),
		    dbspec_key_get(co)
		);
	    free(s0); /* includes token */
	    if (co->do_help)
		xprintf(xf, "(\"%s\")", co->do_help);
	}
    }
    xprintf(xf, ")");
    if (dbspec_key_get(co)){
	xprintf(xf, ",cli_set(\"%s\")", dbspec_key_get(co));
	xprintf(xf, ";");
    }
    retval = 0;
  done:
    return retval;
}

/*
 * Miss callbacks here
 */
static int
dbspec2cli_co(clicon_handle h, xf_t *xf, dbspec_obj *co, enum genmodel_type gt)
{
    int         retval = -1;

    switch (co->do_type){
    case CO_COMMAND: 
	if (dbspec2cli_co_cmd(h, xf, co, gt) < 0)
	    goto done;
	break;
    case CO_VARIABLE: 
	if (dbspec2cli_co_var(h, xf, co, gt) < 0)
	    goto done;
	break;
    default:
	break;
    }
    retval = 0;
  done:
    return retval;
}

/*
 * dbspec2cli
 * Translate from a database specification (cli form) into a cligen syntax.
 * First copy the parse-tree, then modify it.
 * gt - how to generate CLI: 
 *      VARS: generate keywords for regular vars only not index
 *      ALL:  generate keywords for all variables including index
  */
int
dbspec2cli(clicon_handle h, dbspec_tree *pt, parse_tree *ptnew, enum genmodel_type gt)
{
    int             i;
    dbspec_obj     *co;
    int             retval = -1;
    xf_t           *xf;
    cvec           *globals;       /* global variables from syntax */

    if ((xf = xf_alloc()) == NULL){
	clicon_err(OE_XML, errno, "%s: xf_alloc", __FUNCTION__);
	goto done;
    }
    
    /* Go through parse-tree and print a CLIgen tree. */
    for (i=0; i<pt->dt_len; i++)
	if ((co = pt->dt_vec[i]) != NULL)
	    if (dbspec2cli_co(h, xf, co, gt) < 0)
		goto done;
    /* Parse the buffer using cligen parser. */
    if ((globals = cvec_new(0)) == NULL)
	goto done;
    clicon_debug(1, "xbuf: %s", xf_buf(xf));
    /* load cli syntax */
    if (cligen_parse_str(cli_cligen(h), xf_buf(xf), "dbspec2cli", ptnew, globals) < 0)
	goto done;
    /* Dont check any variables for now (eg name="myspec") */
    cvec_free(globals);
    /* resolve expand function names */
    if (cligen_expand_str2fn(*ptnew, expand_str2fn, NULL) < 0)     
	goto done;
    retval = 0;
  done:
    xf_free(xf);
    return retval;
}
#endif /* USE_DBSPEC_PT */

/*=====================================================================
 * YANG generate CLI
 *=====================================================================*/
#if 0 /* examples/ntp */
 ntp("Network Time Protocol"),cli_set("ntp");{ 
     logging("Configure NTP message logging"),cli_set("ntp.logging");{ 
	 status (<status:bool>),cli_set("ntp.logging $status:bool");
     } 
     server("Configure NTP Server") (<ipv4addr:ipv4addr>("IPv4 address of peer")),cli_set("ntp.server[] $!ipv4addr:ipv4addr");
 }
#endif
#if 0 /* examples/datamodel */

WITH COMPLETION:
 a (<x:number>|<x:number expand_dbvar_auto("candidate a[] $!x")>),cli_set("a[] $!x");{
     b,cli_set("a[].b $!x");{ 
	 y (<y:string>|<y:string expand_dbvar_auto("candidate a[].b $!x $y")>),cli_set("a[].b $!x $y");
     } 
     z (<z:string>|<z:string expand_dbvar_auto("candidate a[] $!x $z")>),cli_set("a[] $!x $z");
 }

#endif

#include <src/clicon_yang_parse.tab.h> /* XXX for constants */

static int yang2cli_stmt(clicon_handle h, yang_stmt    *ys, 
			 xf_t         *xf,    
			 enum genmodel_type gt,
			 int           level);

/*
 * Check for completion (of already existent values), ranges (eg range[min:max]) and
 * patterns, (eg regexp:"[0.9]*").
 */
static int
yang2cli_var(yang_stmt    *ys, 
	     xf_t         *xf,    
	     enum cv_type  cvtype,
	     int completion)
{
    yang_stmt    *yt;        /* type */
    yang_stmt    *yr = NULL; /* range */
    yang_stmt    *yp = NULL; /* pattern */
    int           retval = -1;

    if ((yt = yang_find((yang_node*)ys, K_TYPE, NULL)) != NULL){        /* type */
	yr =  yang_find((yang_node*)yt, K_RANGE, NULL);
	yp =  yang_find((yang_node*)yt, K_PATTERN, NULL);
    }
    xprintf(xf, "(<%s:%s", ys->ys_argument, cv_type2str(cvtype));
    if (yr != NULL)
	xprintf(xf, " range[%" PRId64 ":%" PRId64 "]", 
		yr->ys_range_min, yr->ys_range_max);	
    if (yp != NULL)
	xprintf(xf, " regexp:\"%s\"", yp->ys_argument);
    if (completion){
	xprintf(xf, ">|<%s:%s expand_dbvar_auto(\"candidate %s\")",
		ys->ys_argument, 
		cv_type2str(cvtype),
		yang_dbkey_get(ys));
	/* XXX: maybe yr, yp? */
    }
    xprintf(xf, ">)");
    retval = 0;
//  done:
    return retval;
}

static int
yang2cli_leaf(clicon_handle h, 
	      yang_stmt    *ys, 
	      xf_t         *xf,
	      enum genmodel_type gt,
	      int           level)
{
    yang_stmt    *yd;  /* description */
    int           retval = -1;
    char         *keyspec;
    enum cv_type  cvtype;
    int           completion;

    completion = clicon_cli_genmodel_completion(h);
    cvtype = cv_type_get(ys->ys_cv);
    yd = yang_find((yang_node*)ys, K_DESCRIPTION, NULL); /* description */
    xprintf(xf, "%*s", level*3, "");
    if (gt == GT_ALL || gt == GT_VARS){
	xprintf(xf, "%s", ys->ys_argument);
	if (yd != NULL)
	    xprintf(xf, "(\"%s\")", yd->ys_argument);
	xprintf(xf, " ");
	yang2cli_var(ys, xf, cvtype, completion);
    }
    else
	yang2cli_var(ys, xf, cvtype, completion);

    if (yd != NULL)
	xprintf(xf, "(\"%s\")", yd->ys_argument);
    if ((keyspec = yang_dbkey_get(ys)) != NULL)
	xprintf(xf, ",cli_set(\"%s\")", keyspec);
   xprintf(xf, ";\n");

    retval = 0;
//  done:
    return retval;
}


static int
yang2cli_container(clicon_handle h, 
		   yang_stmt    *ys, 
		   xf_t         *xf,
		   enum genmodel_type gt,
		   int           level)
{
    yang_stmt    *yc;
    yang_stmt    *yd;
    char         *keyspec;
    int           i;
    int           retval = -1;

    xprintf(xf, "%*s%s", level*3, "", ys->ys_argument);
    if ((yd = yang_find((yang_node*)ys, K_DESCRIPTION, NULL)) != NULL)
	xprintf(xf, "(\"%s\")", yd->ys_argument);
    if ((keyspec = yang_dbkey_get(ys)) != NULL)
	xprintf(xf, ",cli_set(\"%s\");", keyspec);
   xprintf(xf, "{\n");
    for (i=0; i<ys->ys_len; i++)
	if ((yc = ys->ys_stmt[i]) != NULL)
	    if (yang2cli_stmt(h, yc, xf, gt, level+1) < 0)
		goto done;
    xprintf(xf, "%*s}\n", level*3, "");
    retval = 0;
  done:
    return retval;
}

static int
yang2cli_list(clicon_handle h, 
	      yang_stmt    *ys, 
	      xf_t         *xf,
	      enum genmodel_type gt,
	      int           level)
{
    yang_stmt    *yc;
    yang_stmt    *yd;
    yang_stmt    *ykey;
    yang_stmt    *yleaf;
    int           i;
    int           retval = -1;

    xprintf(xf, "%*s%s", level*3, "", ys->ys_argument);
    if ((yd = yang_find((yang_node*)ys, K_DESCRIPTION, NULL)) != NULL)
	xprintf(xf, "(\"%s\")", yd->ys_argument);
    /* Look for key variable */
    if ((ykey = yang_find((yang_node*)ys, K_KEY, NULL)) == NULL){
	clicon_err(OE_XML, errno, "List statement \"%s\" has no key", ys->ys_argument);
	goto done;
    }
    if ((yleaf = yang_find((yang_node*)ys, K_LEAF, ykey->ys_argument)) == NULL){
	clicon_err(OE_XML, errno, "List statement \"%s\" has no key leaf \"%s\"", 
		   ys->ys_argument, ykey->ys_argument);
	goto done;
    }
    /* Print key variable now, and skip it in loop below */
    if (yang2cli_leaf(h, yleaf, xf, gt, level+1) < 0)
	goto done;
    xprintf(xf, "{\n");
    for (i=0; i<ys->ys_len; i++)
	if ((yc = ys->ys_stmt[i]) != NULL){
	    if (yc == yleaf) /* skip key leaf since done above */
		continue;
	    if (yang2cli_stmt(h, yc, xf, gt, level+1) < 0)
		goto done;
	}
    xprintf(xf, "%*s}\n", level*3, "");
    retval = 0;
  done:
    return retval;
}



/*! Translate yang-stmt to CLIgen syntax.
 */
static int
yang2cli_stmt(clicon_handle h, 
	      yang_stmt    *ys, 
	      xf_t         *xf,
	      enum genmodel_type gt,
	      int           level /* indentation level for pretty-print */
    )
{
    yang_stmt    *yc;
    int           retval = -1;
    int           i;

//    fprintf(stderr, "%s: %s %s\n", __FUNCTION__, 
//	    yang_key2str(ys->ys_keyword), ys->ys_argument);
    switch (ys->ys_keyword){
    case K_CONTAINER:
	if (yang2cli_container(h, ys, xf, gt, level) < 0)
	    goto done;
	break;
    case K_LIST:
	if (yang2cli_list(h, ys, xf, gt, level) < 0)
	    goto done;
	break;
    case K_LEAF_LIST:
    case K_LEAF:
	if (yang2cli_leaf(h, ys, xf, gt, level) < 0)
	    goto done;
	break;
    default:
    for (i=0; i<ys->ys_len; i++)
	if ((yc = ys->ys_stmt[i]) != NULL)
	    if (yang2cli_stmt(h, yc, xf, gt, level+1) < 0)
		goto done;
	break;
    }

    retval = 0;
  done:
    return retval;

}

/*! Translate from a yang specification into a CLIgen syntax.
 *
 * Print a CLIgen syntax to xf string, then parse it.
 * @param gt - how to generate CLI: 
 *             VARS: generate keywords for regular vars only not index
 *             ALL:  generate keywords for all variables including index
 */
int
yang2cli(clicon_handle h, 
	 yang_spec *yspec, 
	 parse_tree *ptnew, 
	 enum genmodel_type gt)
{
    xf_t           *xf;
    int             i;
    int             retval = -1;
    yang_stmt      *ys = NULL;
    cvec           *globals;       /* global variables from syntax */

    if ((xf = xf_alloc()) == NULL){
	clicon_err(OE_XML, errno, "%s: xf_alloc", __FUNCTION__);
	goto done;
    }
    /* Traverse YANG specification: loop through statements */
    for (i=0; i<yspec->yp_len; i++)
	if ((ys = yspec->yp_stmt[i]) != NULL){
	    if (yang2cli_stmt(h, ys, xf, gt, 0) < 0)
		goto done;
	}
    if (debug)
	fprintf(stderr, "%s: buf\n%s\n", __FUNCTION__, xf_buf(xf));
    /* Parse the buffer using cligen parser. XXX why this?*/
    if ((globals = cvec_new(0)) == NULL)
	goto done;
    /* load cli syntax */
    if (cligen_parse_str(cli_cligen(h), xf_buf(xf), 
			 "yang2cli", ptnew, globals) < 0)
	goto done;
    cvec_free(globals);

    retval = 0;
  done:
    xf_free(xf);
    return retval;
}
