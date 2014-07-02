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

 *
 * Database specification
 * Syntax:
 * <line> ::= <key> <var>*
 * <var>  ::= $[!]<name>[:<type>]
 * Example: system.hostname a:string !b:number
 * Comment sign is '#'
 * The resulting parse-tree is in a linked list of db_spec:s
 * Each db_spec contains a key and a variable-headm which in turn contains
 * a list of variables (see osr_var.h).
 *
 * Translation between database specs
 *     db_spec                      parse_tree                    parse_tree
 *  +-------------+ dbspec_key2cli +-------------+ dbspec2cli    +-------------+
 *  |  dbspec     | -------------> | dbclispec   | ------------> | cli         |
 *  |  A[].B !$a  | dbspec_cli2key | A <!a>{ B;} |               | syntax      |
 *  +-------------+ <------------  +-------------+               +-------------+
 *        ^                               ^
 *        |db_spec_parse_file             | dbclispec_parse
 *        |                               |
 *      <file>                          <file>
 * NOTE: PT needed also in commit/validation in config_commit.
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
#include "clicon_hash.h"
#include "clicon_spec.h"
#include "clicon_dbspec_parsetree.h"
#include "clicon_lvalue.h"
#include "clicon_lvmap.h"
#include "clicon_chunk.h"
#include "clicon_yang.h"
#include "clicon_options.h"
#include "clicon_dbutil.h"
#include "clicon_dbspec.h"

/*! Parse a string containing a CLICON dbspec into a parse-tree
 * 
 * Syntax parsing. A string is input and a syntax-tree is returned (or error). 
 * A variable record is also returned containing a list of (global) variable values.
 * (cloned from cligen)
 */
static int
clicon_dbspec_parse_str(clicon_handle h,
			char *str,
			const char *name, /* just for errs */
			dbspec_tree *pt,
			cvec *vr
    )
{
    int                retval = -1;
    int                i;
    struct clicon_dbspec_yacc_arg ya = {0,};
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
	if (dbspec_scan_init(&ya) < 0)
	    goto done;
	if (dbspec_parse_init(&ya, co_top) < 0)
	    goto done;
	if (clicon_dbspecparse(&ya) != 0) {
	    dbspec_parse_exit(&ya);
	    dbspec_scan_exit(&ya);
	    goto done;
	}
	if (dbspec_parse_exit(&ya) < 0)
	    goto done;		
	if (dbspec_scan_exit(&ya) < 0)
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


/*! Parse a file containing a CLICON dbspec into a parse-tree
 *
 * Similar to clicon_dbspec_str(), just read a file first
 * (cloned from cligen)
 * The database symbols are inserted in alphabetical order.
 */
static int
clicon_dbspec_parse_file(clicon_handle h,
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
    if (clicon_dbspec_parse_str(h, buf, name, pt, globals) < 0)
	goto done;
    retval = 0;
  done:
    if (buf)
	free(buf);
    return retval;
}

/*! Parse dbspec using cligen spec format
 *
 * The database symbols are inserted in alphabetical order.
 */
int
dbclispec_parse(clicon_handle h, const char *filename, dbspec_tree *pt)
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
    if (clicon_dbspec_parse_file(h, f, filename, pt, cvec) < 0)
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


static struct dbspec_obj *
key2spec_co1(dbspec_tree *pt, char **vec, int nvec)
{
    char            *key;
//    char            *index;
    dbspec_obj          *co;
    int              i;

    if (nvec <= 0)
	return NULL;
    key = vec[0];
    for (i=0; i<pt->dt_len; i++){
	if ((co = pt->dt_vec[i]) == NULL)
	    continue;
	if (co->do_type != CO_COMMAND)
	    continue;
	if (strcmp(key, co->do_command) != 0)
	    continue;
	if (dbspec_indexvar_get(co)){
	    nvec--;
	    if (nvec <= 0)
		return NULL;
//	    index = vec[0]; /* assume index is number */
	}
	if (nvec == 1)
	    return co;
	return key2spec_co1(&co->do_pt, vec+1, nvec-1);
    }
    return NULL;
}

/*
 * key2spec_co
 * given a specific key, find a matching specification (dbspec cli-object style).
 * e.g. a.0 matches the db_spec corresponding to a[].
 * Input args:
 *  pt - db specification parse-tree
 *  key - key to find in dbspec
 */
struct dbspec_obj *
key2spec_co(dbspec_tree *pt, char *key)
{
    char           **vec;
    int              nvec;

    if ((vec = clicon_strsplit(key, ".", &nvec, __FUNCTION__)) == NULL){
	clicon_err(OE_DB, errno, "%s: strsplit", __FUNCTION__); 
	return NULL;
    }
    return key2spec_co1(pt, vec, nvec);
    /* XXX: unchunk */
}


/*---------------------------------------------------------------------------
 * key2cli code
 *--------------------------------------------------------------------------*/
/*
 * cogen_new
 * Translate db specification from a key to a cli format
 */
static dbspec_obj *
cogen_new(clicon_handle h, dbspec_obj *cop, char *key)
{
    dbspec_obj             *co;

    if ((co = co_new2(key, cop)) == NULL) { 
	clicon_err(OE_UNIX, errno, "Allocating cligen object"); 
	goto err;
    }
    if (dbspec_userdata_add(h, co) < 0)
	goto err;
    if ((co = co_insert2(&cop->do_pt, co)) == NULL)  {
	clicon_err(OE_UNIX, errno, "Inserting cligen object"); 
	goto err;
    }
//    co->co_nonterminal++;	
/* successively build string of key var* for insertion into cli_set() for example */
    return co;
  err:
    if (co)
	co_free2(co, 1);
    return NULL;
}

/*
 * covar_new
 * Translate db specification from a key to a cli format
 * add new variable co under cop
 */
static dbspec_obj *
covar_new(clicon_handle h, dbspec_obj *cop, cg_var *v)
{
    dbspec_obj             *cov = NULL;

    if ((cov = co_new2(cv_name_get(v), cop)) == NULL) { 
	clicon_err(OE_UNIX, errno, "Allocating cligen object"); 
	goto err;
    }
    cov->do_type = CO_VARIABLE;
    cov->do_vtype = cv_type_get(v);
    if (dbspec_userdata_add(h, cov) < 0)
	goto err;

    if (!cv_flag(v, V_UNSET))
	dbspec_default_set(cov, cv_dup(v)); /* Could actually get only .u part */

    if ((cov = co_insert2(&cop->do_pt, cov)) == NULL)  {
	clicon_err(OE_UNIX, errno, "Inserting cligen object"); 
	goto err;
    }
    unchunk_group(__FUNCTION__);  
    return cov;
  err:
    if (cov)
	co_free2(cov, 1);
    unchunk_group(__FUNCTION__);  
    return NULL;
}



/* 
 * dbspec_key2cli
 * Translate db specification from a key to a cli format
 * Input: 
 *    db_spec:   Database specification.
 *    pt         parse-tree
 */
int
dbspec_key2cli(clicon_handle h, struct db_spec *db_spec, dbspec_tree *pt)
{
    int              retval = -1;
    struct db_spec  *ds;
    char           **vec;
    char            *key;
    int              nvec;
    int              i;
    cvec            *subvh;
    cg_var          *v = NULL;
    dbspec_obj          *co;
    dbspec_obj          *cov;
    dbspec_obj           co0; /* tmp top object: NOT malloced */
    dbspec_obj          *cop; /* parent */
    int              isvec;
    char            *str;

    memset(&co0, 0, sizeof(co0));
    /* Parse through all spec lines */
    for (ds=db_spec; ds; ds=ds->ds_next){
	clicon_debug(2, "%s: spec line: %s\n", __FUNCTION__, ds->ds_key);
	subvh = db_spec2cvec(ds);
	v = NULL; 		
	co = NULL;
	if ((vec = clicon_strsplit(ds->ds_key, ".", &nvec, __FUNCTION__)) == NULL){
	    clicon_err(OE_DB, errno, "%s: strsplit", __FUNCTION__); 
	    goto catch;
	}
	cop = &co0;
	v = NULL; /* unique variable */
	/* Parse through all keys in a spec-line, eg "a.b.c" */
	for (i=0; i<nvec; i++){ 
	    key = vec[i];
	    clicon_debug(2, "%s: \tkey: %s\n", __FUNCTION__, vec[i]);
	    isvec = 0;
	    if (key_isvector(key)){
		isvec++;
		key[strlen(key)-2] = '\0';
	    }
	    /* check if key already exists in existing parse-tree */
	    if ((co = co_find_one2(cop->do_pt, key)) == NULL){
		if ((co = cogen_new(h, cop, key)) == NULL){
		    goto catch;
		}
	    } /* co is key */
	    if (co->do_type != CO_COMMAND){
		clicon_err(OE_DB, 0, "%s: child '%s' in '%s' has mixed types\n",
			   __FUNCTION__, key, ds->ds_key); 
		goto catch;
	    }
	    if (isvec){ 
		/* Find unique key and add that as sub (if it does not already exist) */
		while ((v = cvec_each(subvh, v))) 
		    if (cv_flag(v, V_UNIQUE))
			break;
		if (v == NULL){
		    clicon_err(OE_DB, 0, "Spec has no matching unique variable\n"); 
		    goto catch;
		}
		/* A unique variable is created */
		if ((cov = co_find_one2(co->do_pt, cv_name_get(v))) == NULL)
		    if ((cov = covar_new(h, co, v)) == NULL)
			goto catch;
//		co_insert2(&cov->do_pt, NULL); /* empty child */
		if ((str = db_spec2str(ds)) == NULL)
		    goto catch;
		/* The unique variable is added as indexvar in co */
		if (dbspec_key_set(cov, str) < 0)
		    goto catch;
// XXX ??	dbspec_default_set(co, cv_dup(v));
	    }
	    cop = co;
	} /* Parse through all keys in a spec-line, eg "a.b.c[]" */

	/* Remaining unique variables are added at the end */
	while ((v = cvec_each(subvh, v))) {
	    if (!cv_flag(v, V_UNIQUE))
		continue;
	    /* A variable is created as '<name>' */
	    if ((cov = co_find_one2(co->do_pt, cv_name_get(v))) == NULL)
		if ((cov = covar_new(h, co, v)) == NULL)
		    goto catch;
	    if ((str = db_spec2str(ds)) == NULL)
		goto catch;
	    /* The unique variable is added as indexvar in co */
	    if (dbspec_key_set(cov, str) < 0)
		goto catch;

	    co = cov;
	}

	/* Go through all non-unique variables and append to syntax */
	v = NULL;
	while ((v = cvec_each(subvh, v))) {
	    if (cv_flag(v, V_UNIQUE))
		continue;
	    if ((cov = covar_new(h, co, v)) == NULL)
		goto catch;
	    co_insert2(&cov->do_pt, NULL); /* empty child */
		if ((str = db_spec2str(ds)) == NULL)
		    goto catch;
		/* The unique variable is added as indexvar in co */
		if (dbspec_key_set(cov, str) < 0)
		    goto catch;

	} /* while single var not unique */
    } /* for (ds): lines in database */

    /*
     * Remove the fake top level object and remove references to it.
     */
    *pt = co0.do_pt;
    for (i=0; i<co0.do_max; i++){
	co=co0.do_next[i];
	if (co)
	    co_up_set2(co, NULL);
    }
    retval = 0;
  catch:
    /*
     * Remove the fake top level object and remove references to it.
     */
    for (i=0; i<co0.do_max; i++){
	co=co0.do_next[i];
	if (co)
	    co_up_set2(co, NULL);
    }
    unchunk_group(__FUNCTION__);  
    return retval;
}

/*---------------------------------------------------------------------------
 * cli2key code
 *--------------------------------------------------------------------------*/
/*
 * Create a dbspeckey
 */
static int
cli2db_genkey(cvec *keys, cvec *vars, struct db_spec **dsp)
{
    char           *key = NULL;
    cg_var         *cv = NULL;
    struct db_spec *ds = NULL;

    while ((cv = cvec_each(keys, cv)) != NULL) 
	key = chunk_sprintf(__FUNCTION__, "%s%s%s%s",
			    key?key:"",
			    key?".":"",
			    cv_name_get(cv),
			    cv_flag(cv, V_UNIQUE)?"[]":"");
    if (key==NULL){
	*dsp = NULL;
	return 0;
    }
    if ((ds = db_spec_new()) == NULL) /* XXX */
	goto err;
    if ((ds->ds_key = strdup(key)) == NULL){
	clicon_err(OE_DB, errno, "%s: strdup", __FUNCTION__); 
	goto err;
    }
    if ((ds->ds_vec = cvec_dup(vars)) == NULL){
	clicon_err(OE_DB, errno, "%s: cvec_dup", __FUNCTION__); 
	goto err;
    }
    unchunk_group(__FUNCTION__);
    *dsp = ds;
    return 0;
  err:
    if (ds)
	db_spec_free(ds);
    unchunk_group(__FUNCTION__);
    return -1;
}

/* Forward */
static int
dbspec_cli2key_co(dbspec_obj          *co0,
		  cvec            *keys,   
		  cvec            *vars,   
		  struct db_spec **ds_list);

/*
 * dbspec_cli2key_co_addvar
 *
 * Add a new variable to the list of variables in vars, 
 * a key consists of 'key vars'.
 * Create a new key and append to ds_list
 */
static int
dbspec_cli2key_co_addvar(dbspec_obj          *co,
			 int              unique,
			 cvec            *keys0,  
		         cvec            *vars,
			 struct db_spec **ds_list)
{
    int             retval = -1;
    cg_var         *cv;
    struct db_spec *ds;
    char           *str;

    if ((cv = cvec_add(vars, co->do_vtype)) == NULL){
	clicon_err(OE_DB, errno, "%s: cvec_add", __FUNCTION__); 
	goto done;
    }
    if (dbspec_default_get(co)){ /* default value */
	if (cv_cp(cv, dbspec_default_get(co)) < 0){
	    clicon_err(OE_DB, errno, "%s: cv_cp", __FUNCTION__); 
	    goto done;
	}
    }
    else
	cv_flag_set(cv, V_UNSET);
    if (unique)
	cv_flag_set(cv, V_UNIQUE);
    if (cv_name_set(cv, co->do_command) == NULL) {
	clicon_err(OE_UNIX, errno, "cv_name_set"); 
	goto done;
    }
    if (cli2db_genkey(keys0, vars, &ds) < 0)
	goto done;
    if (ds==NULL){
	clicon_err(OE_DB, errno, "No ds created for %s", co->do_command); 
	goto done;
    }
    if (dbspec_vector_get(co))
	ds->ds_vector = 1;
    if ((str = db_spec2str(ds)) == NULL)
	goto done;
    if (dbspec_key_set(co, str) < 0)
	goto done;
    db_spec_tailadd(ds_list, ds); /* ds may be freed */

    retval = 0;
  done:  
    return retval;
}

/*
 * dbspec_cli2key_co_cmd
 */
static int
dbspec_cli2key_co_cmd(dbspec_obj          *co,
		      cvec            *keys0,   /* inherited keys */
		      cvec            *vars0,   /* inherited vars */
		      struct db_spec **ds_list)
{
    cg_var         *cv;
    int             i;
    dbspec_tree     *pt;
    dbspec_tree     *ptv;
    char           *indexvar = dbspec_indexvar_get(co);
    char           *str;
    dbspec_obj         *co1;
    dbspec_obj         *cov;
    cvec           *keys = NULL;
    cvec           *vars = NULL;
    int             retval = -1;
    struct db_spec *ds = NULL;

    pt = &co->do_pt;
    /* Append a key to dbspec for every command. Check if list or not */
    if ((cv = cvec_add(keys0, CGV_STRING)) == NULL){
	clicon_err(OE_DB, errno, "%s: cvec_add", __FUNCTION__); 
	goto done;
    }
    if (cv_name_set(cv, co->do_command) == NULL) {
	clicon_err(OE_UNIX, errno, "cv_name_set"); 
	goto done;
    }

    if (indexvar){
	/* This cmd has index variable, mark it as such (print as x[]) */
	cv_flag_set(cv, V_UNIQUE); 	
	/* This cmd has index variable. Loop thru children and find it */
	cov = NULL;
	for (i=0; i<pt->dt_len; i++){
	    cov = pt->dt_vec[i];
	    if (cov && 
		cov->do_type == CO_VARIABLE && 
		strcmp(indexvar, cov->do_command)==0){
		break;
	    }
	}
	if (i==pt->dt_len){
	    clicon_err(OE_DB, errno, "%s: %s has no indexvariable %s\n", 
		       __FUNCTION__, co->do_command, indexvar); 
	    goto done;
	}
	assert(cov);
	/* Sanity check: indexvar should not have children */
	ptv = &cov->do_pt;
	if (ptv->dt_len)
	    if (ptv->dt_len > 1 || ptv->dt_vec[0] != NULL){
		clicon_err(OE_DB, errno, "Variable %s has children, should be leaf", 
			   cov->do_command); 
		goto done;
	    }
	/* Add the index-variable to the list of variables in vars */
	if (dbspec_cli2key_co_addvar(cov, 1, keys0, vars0, ds_list) < 0)
	    goto done;
    }
    /* Create new dbspec_key structure from existing key list. Create key and copy
       variables. */
    if (cli2db_genkey(keys0, vars0, &ds) < 0)
	goto done;
    if (ds == NULL){
	clicon_err(OE_DB, errno, "No ds created for %s", co->do_command); 
	goto done;
    }
    /* Get the key and store the it back in the parse-tree spec for cli generation 
       XXX: isnt this the same as the one in ds?
     */
    if ((str = db_spec2str(ds)) == NULL)
	goto done;
    if (dbspec_key_set(co, str) < 0)  /* XXX: why is this freed? */
	goto done;
    db_spec_free1(ds);
    for (i=0; i<pt->dt_len; i++){
	if ((co1 = pt->dt_vec[i]) == NULL){
	    if (cli2db_genkey(keys0, vars0, &ds) < 0)
		goto done;
	    db_spec_tailadd(ds_list, ds); /* ds computed above XXX but ds is freed? */
	    continue;
	}
	/* index variables already handled */
	if (indexvar &&
	    co1->do_type == CO_VARIABLE && 
	    strcmp(indexvar, co1->do_command)== 0)
	    continue;
	if ((keys = cvec_dup(keys0)) == NULL){
	    clicon_err(OE_DB, errno, "%s: chunk", __FUNCTION__); 
	    goto done;
	}
	if ((vars = cvec_dup(vars0)) == NULL){
	    clicon_err(OE_DB, errno, "%s: cvec_dup", __FUNCTION__); 
	    goto done;
	}
	if (dbspec_cli2key_co(co1, keys, vars, ds_list) < 0)
	    goto done;
	cvec_free(vars);
	cvec_free(keys);
    }
    retval = 0;
  done:  /* note : no unchunk here: recursion */
    return retval;
}

/*
 * dbspec_cli2key_co_var
 */
static int
dbspec_cli2key_co_var(dbspec_obj          *co,
		      cvec            *keys0,   /* inherited keys (encoded as vars) */
		      cvec            *vars0,   /* inherited vars */
		      struct db_spec **ds_list)
{
    int             retval = -1;
    dbspec_tree     *pt;

    pt = &co->do_pt; /* Canonical form: <var> (leaf) should have no children. */
    if (pt->dt_len > 1 || pt->dt_vec[0] != NULL){
	clicon_err(OE_DB, errno, "Variable %s has children, should be leaf", 
		   co->do_command); 
	goto done;
    }
    /* Assume index variable already handled */
    if (dbspec_cli2key_co_addvar(co, 0, keys0, vars0, ds_list) < 0)
	goto done;

    retval = 0;
  done:  
    return retval;
}

/*
 * dbspec_cli2key_co
 * Translate cg_obj co0 to key dbspec.
 * There are four kinds, two for commands, two for variables:
 *  list      g[i]{<x>,a} ==> g[] $!i <x>; g[].a...
 *  container g{<x>,a>}   ==> g $x; g.a
 *  leaf      <x>         ==> $x
 *  leaf-ist  <x[]>       ==> $x[] NOTYET
 */
static int
dbspec_cli2key_co(dbspec_obj          *co,
		  cvec            *keys0,   /* inherited keys (encoded as vars) */
		  cvec            *vars0,   /* inherited vars */
		  struct db_spec **ds_list)

{
    int             retval = -1;

    assert(co->do_type == CO_COMMAND ||co->do_type == CO_VARIABLE);
    switch (co->do_type){
    case CO_COMMAND: /* Add command as key */
	if (dbspec_cli2key_co_cmd(co, keys0, vars0, ds_list) < 0)
	    goto done;
	break;
    case CO_VARIABLE: /* Add variable as value */
	if (dbspec_cli2key_co_var(co, keys0, vars0, ds_list) < 0)
	    goto done;
	break;
    default:
	break;
    }
    retval = 0;
  done:  /* note : no unchunk here: recursion */
    return retval;
}

/*
 * dbspec_cli2key
 * Not completely trivial. See clicon-datamode.pdf.
 * some cases:
 * a <a>         --> a $a
 * a <!a>        --> a[] $a
 * a b <a>       --> a.b $a
 * a b <!a>      --> a.b[] $!a
 * a <!a> b <b>  --> a[]   $!a   # NOTE!
                     a[].b $!a $b
 * And of course:
 * a { b; c; }  --> a.b
 *                  a.c
 */
struct db_spec *
dbspec_cli2key(dbspec_tree *pt)
{
    int             i;
    dbspec_obj     *co;
    struct db_spec *ds_list = NULL; 
    cvec           *keys;
    cvec           *vars;

    for (i=0; i<pt->dt_len; i++){
	if ((co = pt->dt_vec[i]) == NULL)
	    continue;
	if ((vars = cvec_new(0)) == NULL){
	    clicon_err(OE_DB, errno, "%s: cvec_new", __FUNCTION__); 
	    goto err;
	}
	if ((keys = cvec_new(0)) == NULL){
	    clicon_err(OE_DB, errno, "%s: cvec_new", __FUNCTION__); 
	    goto err;
	}
	if (dbspec_cli2key_co(co, keys, vars, &ds_list) < 0)
	    goto err;
	cvec_free(vars);
	cvec_free(keys);
    }
    if (ds_list == NULL){
	clicon_err(OE_DB, errno, "%s: Empty database-spec", __FUNCTION__); 
	goto err;
    }
    return ds_list;
  err:
    if (ds_list)
	db_spec_free(ds_list);
    return NULL;
}

/*
 * Access macros for cligen_objects userdata
 */
char *
dbspec_indexvar_get(dbspec_obj *co)
{
    struct dbspec_userdata *du = (struct dbspec_userdata*)co->do_userdata;

    assert(du);
    return du->du_indexvar;
}

int 
dbspec_indexvar_set(dbspec_obj *co, char *val)
{
    struct dbspec_userdata *du = (struct dbspec_userdata*)co->do_userdata;

    assert(du);
    du->du_indexvar = val;
    return 0;
}

/*! get dbspec key of a parse-tree node, used when generating cli
 */
char *
dbspec_key_get(dbspec_obj *co)
{
    struct dbspec_userdata *du = (struct dbspec_userdata*)co->do_userdata;

    assert(du);
    return du->du_dbspec;
}

int 
dbspec_key_set(dbspec_obj *co, char *val)
{
    struct dbspec_userdata *du = (struct dbspec_userdata*)co->do_userdata;

    assert(du);
    du->du_dbspec = val;
    return 0;
}

int 
dbspec_optional_get(dbspec_obj *co)
{
    struct dbspec_userdata *du = (struct dbspec_userdata*)co->do_userdata;

    assert(du);
    return du->du_optional;
}

int 
dbspec_optional_set(dbspec_obj *co, int val)
{
    struct dbspec_userdata *du = (struct dbspec_userdata*)co->do_userdata;

    assert(du);
    du->du_optional = val;
    return 0;
}

struct cg_var *
dbspec_default_get(dbspec_obj *co)
{
    struct dbspec_userdata *du = (struct dbspec_userdata*)co->do_userdata;

    assert(du);
    return du->du_default;
}

int 
dbspec_default_set(dbspec_obj *co, struct cg_var *val)
{
    struct dbspec_userdata *du = (struct dbspec_userdata*)co->do_userdata;

    assert(du);
    du->du_default = val;
    return 0;
}

char 
dbspec_vector_get(dbspec_obj *co)
{
    struct dbspec_userdata *du = (struct dbspec_userdata*)co->do_userdata;

    assert(du);
    return du->du_vector;
}

int 
dbspec_vector_set(dbspec_obj *co, char val)
{
    struct dbspec_userdata *du = (struct dbspec_userdata*)co->do_userdata;

    assert(du);
    du->du_vector = val;
    return 0;
}

/* 
 * Add malloced piece of code to attach to cligen objects used as db-specs.
 * So if(when) we translate cg_obj to dbspec_obj (or something). These are the fields
 * we should add.
 */
int
dbspec_userdata_add(clicon_handle h, dbspec_obj *co)
{
    struct dbspec_userdata *du;

    if ((co->do_userdata  = malloc(sizeof (struct dbspec_userdata))) == NULL){
	clicon_err(OE_UNIX, errno, "malloc");
	return -1;
    }
    du = (struct dbspec_userdata *)co->do_userdata;
    memset(du, 0, sizeof (struct dbspec_userdata));
    du->du_optional = clicon_cli_genmodel_optional(h);
    return 0;
}

int
dbspec_userdata_delete(dbspec_obj *co, void *arg)
{
    struct dbspec_userdata *du = (struct dbspec_userdata *)co->do_userdata;

    if (du == NULL)
	return 0;
    if (du->du_indexvar)
	free(du->du_indexvar);
    if (du->du_dbspec)
	free(du->du_dbspec);
    if (du->du_default)
	cv_free(du->du_default);
    free(du);
    co->do_userdata = NULL;
    return 0;
}

static int
dbspec2dtd_var(FILE *f, dbspec_obj *co)
{
    struct cg_var *cv;

    fprintf(f, "\t%s CDATA ", co->do_command);
    if ((cv = dbspec_default_get(co)) != NULL){
	cv_name_set(cv, co->do_command);
	if (cv_type_get(cv) != CGV_STRING && cv_type_get(cv) != CGV_REST)
	    fprintf(f, "\"");
	cv_print(f, cv);
	if (cv_type_get(cv) != CGV_STRING && cv_type_get(cv) != CGV_REST)
	    fprintf(f, "\"");
    }
    else{
	if (dbspec_optional_get(co))
	    fprintf(f, "#IMPLIED");
	else
	    fprintf(f, "#REQUIRED");
    }
    fprintf(f, " \n");
    return 0;
}

static int
dbspec2dtd_cmd(FILE *f, dbspec_obj *co)
{
    dbspec_tree     *pt;
    dbspec_obj         *coc;
    int             retval = -1;
    int             i;
    int             j;

    pt = &co->do_pt;
    fprintf(f, "<!ELEMENT %s (", co->do_command);
    j = 0;
    for (i=0; i<pt->dt_len; i++){
	if ((coc = pt->dt_vec[i]) == NULL)
	    continue;
	if (coc->do_type != CO_COMMAND)
	    continue;
	if (j++ != 0)
	    fprintf(f, "|");
	fprintf(f, "%s", coc->do_command);
    }
    if (j==0)
	fprintf(f, "#PCDATA)>\n");
    else
	fprintf(f, ")*>\n");
    fprintf(f, "<!ATTLIST %s\n", co->do_command);
    for (i=0; i<pt->dt_len; i++){
	if ((coc = pt->dt_vec[i]) == NULL)
	    continue;
	if (coc->do_type != CO_VARIABLE)
	    continue;
	if (dbspec2dtd_var(f, coc) < 0)
	    goto done;
    }
    fprintf(f, ">\n");
    if (dbspec2dtd(f, pt) < 0)
	goto done;
    retval = 0;
  done:
    return retval;
}

/*! Translate from a clicon database specification to a DTD on a stream
 *
 * @param f    Output stream
 * @param pt   Database specification as CLIGEN parse-tree
 */
int
dbspec2dtd(FILE *f, dbspec_tree *pt)
{
    int        i;
    dbspec_obj    *co;
    int        retval = -1;

    for (i=0; i<pt->dt_len; i++){
	if ((co = pt->dt_vec[i]) == NULL)
	    continue;
	if (co->do_type == CO_COMMAND)
	    if (dbspec2dtd_cmd(f, co) < 0)
		goto done;
    }   
    retval = 0;
  done:
    return retval;
}
