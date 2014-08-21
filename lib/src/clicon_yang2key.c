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
 * Translate between YANG and key specification
 * Key Syntax:
 * <line> ::= <key> <var>*
 * <var>  ::= $[!]<name>[:<type>]
 * Example: system.hostname a:string !b:number
 * Comment sign is '#'
 * The resulting parse-tree is in a linked list of db_spec:s
 * Each db_spec contains a key and a variable-headm which in turn contains
 * a list of variables (see osr_var.h).
 *
 * Translation between database specs
 *     dbspec_key                   yang_spec                     CLIgen parse_tree
 *  +-------------+    yang2key    +-------------+   yang2cli    +-------------+
 *  |  keyspec    | -------------> |             | ------------> | cli         |
 *  |  A[].B !$a  |    key2yang    | list{key A;}|               | syntax      |
 *  +-------------+ <------------  +-------------+               +-------------+
 *        ^                             ^
 *        |db_spec_parse_file           |yang_parse
 *        |                             |
 *      <file>                        <file>
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
#include "clicon_dbspec_key.h"
#include "clicon_yang.h"
#include "clicon_yang_type.h"
#include "clicon_lvalue.h"
#include "clicon_lvmap.h"
#include "clicon_chunk.h"
#include "clicon_options.h"
#include "clicon_dbutil.h"
#include "clicon_yang2key.h"

static int yang2key_stmt(yang_stmt *ys, cvec *keys, cvec *vars, dbspec_key **ds_list);

/*! Create a dbspeckey by concatenating previous keys eg a.b.c
 */
static int
cli2db_genkey(cvec *keys, cvec *vars, dbspec_key **dsp)
{
    char           *key = NULL;
    cg_var         *cv = NULL;
    dbspec_key *ds = NULL;

    while ((cv = cvec_each(keys, cv)) != NULL) 
	if ((key = chunk_sprintf(__FUNCTION__, "%s%s%s%s",
			    key?key:"",
			    key?".":"",
			    cv_name_get(cv),
				 cv_flag(cv, V_UNIQUE)?"[]":"")) == NULL){
	    clicon_err(OE_DB, errno, "%s: chunk_sprintf", __FUNCTION__); 
	    goto err;
	}
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


/*! Translate yang container to dbspec key
 */
static int
yang2key_container(yang_stmt       *ys, 
		   cvec            *keys0,   /* inherited keys */
		   cvec            *vars0,   /* inherited vars */
		   dbspec_key **ds_list)
{
    int             retval = -1;
    cg_var         *cv;
    char           *str = NULL;
    dbspec_key *ds = NULL;

    /* 1. Append a key to dbspec for every container. */
    if ((cv = cvec_add_name(keys0, CGV_STRING, ys->ys_argument)) == NULL){
	clicon_err(OE_DB, errno, "%s: cvec_add", __FUNCTION__); 
	goto done;
    }
    /* 2. Add symbol to dbspec structure (used in callback) */
    if (cli2db_genkey(keys0, vars0, &ds) < 0)
	goto done;
    /* Add symbol but only if there are variables */
    
#if 0
    if (cvec_len(vars0) &&
	db_spec_tailadd(ds_list, ds) < 0) /* ds is consumed and may be freed in this call */
	goto done;
#else
    if (db_spec_tailadd(ds_list, ds) < 0) /* ds is consumed and may be freed in this call */
	goto done;
#endif
    /* XXX: Many problems with this code:
       1. ds may leak or may be freed maturely?
       2. I dont think it should be here, the ds is added by leaf function anyhow,...
     */
    if ((str = db_spec2str(ds)) == NULL) /* XXX: isnt this the same as the one in ds? */
	goto done;
    /* Get the key and store the it back in the parse-tree spec for cli generation */
    if (yang_dbkey_set(ys, str) < 0)
	goto done;

    retval = 0;
  done:
    if (str)
	free(str);
    return retval;
}

/*! Translate yang leaf to dbspec key
 */
static int
yang2key_leaf(yang_stmt       *ys, 
	      cvec            *keys0,   /* inherited keys */
	      cvec            *vars0,   /* inherited vars */
	      int              fromlist,/* Called from yang list-stmt fn */
	      dbspec_key **ds_list)
{
    int             retval = -1;
    cg_var         *cv;
    dbspec_key *ds = NULL;
    char           *keyspec = NULL;
    yang_node      *yparent; 
    yang_stmt      *ykey = NULL; 

    /* If called from generic loop in yang2key_stmt, 
       check if this leaf is a key leaf by finding parent and its key node 
       If it is, we have already added it, see yang2key_list()
     */
    if (!fromlist &&
	(yparent = ys->ys_parent) != NULL && yparent->yn_keyword == Y_LIST){
	if ((ykey = yang_find(yparent, Y_KEY, ys->ys_argument)) != NULL){
	    retval = 0; /* We are good */
	    goto done;
	}
    }
    
    /* get the (already generated) cligen variable */
    if ((cv = cvec_add_cv(vars0, ys->ys_cv)) == NULL){
	clicon_err(OE_DB, errno, "%s: cvec_add_cv", __FUNCTION__); 
	goto done;
    }
    /* 2. Add symbol to dbspec structure (used in callback) */
    if (cli2db_genkey(keys0, vars0, &ds) < 0)
	goto done;
    if (ds == NULL){
	clicon_err(OE_DB, 0, "No db key, leaf directly under root"); 
	goto done;
    }
    /* This adds the variables on the form $x:type */
    if ((keyspec = db_spec2str(ds)) == NULL) 
	goto done;
    if (db_spec_tailadd(ds_list, ds) < 0) 
	goto done;
    ds = NULL; /* ds is consumed and may be freed in the call above */
    /* Get the key and store the it back in the parse-tree spec for cli generation */
    if (yang_dbkey_set(ys, keyspec) < 0)
	goto done;
    retval = 0;
  done:
    if (keyspec)
	free(keyspec);
    return retval;
}

/*! Translate yang list to dbspec key
 */
static int
yang2key_list(yang_stmt       *ys, 
	      cvec            *keys0,   /* inherited keys */
	      cvec            *vars0,   /* inherited vars */
	      dbspec_key **ds_list)
{
    int             retval = -1;
    cg_var         *cv;
    char           *str = NULL;
    dbspec_key *ds = NULL;
    yang_stmt      *ykey;
    yang_stmt      *yleaf;

    /* 1. Append a key to dbspec for every container. */
    if ((cv = cvec_add_name(keys0, CGV_STRING, ys->ys_argument)) == NULL){
	clicon_err(OE_DB, errno, "%s: cvec_add", __FUNCTION__); 
	goto done;
    }
    /* A list has a key(index) variable, mark it as CLICON list (print as x[]) */
    cv_flag_set(cv, V_UNIQUE); 	

    if ((ykey = yang_find((yang_node*)ys, Y_KEY, NULL)) == NULL){
	clicon_err(OE_XML, errno, "List statement \"%s\" has no key", ys->ys_argument);
	goto done;
    }
    if ((yleaf = yang_find((yang_node*)ys, Y_LEAF, ykey->ys_argument)) == NULL){
	clicon_err(OE_XML, errno, "List statement \"%s\" has no key leaf \"%s\"", 
		   ys->ys_argument, ykey->ys_argument);
	goto done;
    }
    /* Call leaf directly, then ensure it is not called again in yang2key_stmt() */
    if (yang2key_leaf(yleaf, keys0, vars0, 1, ds_list) < 0)
	goto done;

    /* 2. Add symbol to dbspec structure (used in callback) */
    if (cli2db_genkey(keys0, vars0, &ds) < 0)
	goto done;
    if ((str = db_spec2str(ds)) == NULL) /* XXX: isnt this the same as the one in ds? */
	goto done;
    if (db_spec_tailadd(ds_list, ds) < 0) 
	goto done;
    ds = NULL; /* ds is consumed and may be freed in the call above */

    /* Get the key and store the it back in the parse-tree spec for cli generation */
    if (yang_dbkey_set(ys, str) < 0)
	goto done;
    retval = 0;
  done:
    if (str)
	free(str);
    return retval;
}


/*! Translate yang leaf-list to dbspec key
 * Similar to leaf but set the ds_vector flag to allow multiple values.
 * Also append '[]' to variable name?
 */
static int
yang2key_leaf_list(yang_stmt       *ys, 
		   cvec            *keys0,   /* inherited keys */
		   cvec            *vars0,   /* inherited vars */
		   dbspec_key **ds_list)
{
    int             retval = -1;
    cg_var         *cv;
    dbspec_key *ds = NULL;
    char           *keyspec = NULL;
    char           *keyspec2;

    /* get the (already generated) cligen variable */
    if ((cv = cvec_add_cv(vars0, ys->ys_cv)) == NULL){
	clicon_err(OE_DB, errno, "%s: cvec_add_cv", __FUNCTION__); 
	goto done;
    }
    /* 2. Add symbol to dbspec structure (used in callback) */
    if (cli2db_genkey(keys0, vars0, &ds) < 0)
	goto done;
    if (ds == NULL){
	clicon_err(OE_DB, 0, "No db key, leaf directly under root"); 
	goto done;
    }
    ds->ds_vector = 1; 
    /* This adds the variables on the form $x:type */
    if ((keyspec = db_spec2str(ds)) == NULL) 
	goto done;
    /* XXX: %s[] */
    if ((keyspec2 = chunk_sprintf(__FUNCTION__, "%s", keyspec)) == NULL)
	goto done;
    if (db_spec_tailadd(ds_list, ds) < 0) 
	goto done;
    ds = NULL; /* ds is consumed and may be freed in the call above */
    /* Get the key and store the it back in the spec for cli generation */
    if (yang_dbkey_set(ys, keyspec2) < 0)
	goto done;
    retval = 0;
  done:
    if (keyspec)
	free(keyspec);
    unchunk_group(__FUNCTION__);
    return retval;
}

/*! Translate generic yang_stmt to key dbspec.
 */
static int
yang2key_stmt(yang_stmt       *ys, 
	      cvec            *keys0,   /* inherited keys */
	      cvec            *vars0,   /* inherited vars */
	      dbspec_key **ds_list)
{
    yang_stmt *yc;
    int        retval = -1;
    int        i;
    cvec      *keys = NULL;
    cvec      *vars = NULL;

    if (debug)
	fprintf(stderr, "%s: %s %s\n", __FUNCTION__, 
		yang_key2str(ys->ys_keyword), ys->ys_argument);
    switch (ys->ys_keyword){
    case Y_CONTAINER:
	if (yang2key_container(ys, keys0, vars0, ds_list) < 0)
	    goto done;
	break;
    case Y_LEAF:
	if (yang2key_leaf(ys, keys0, vars0, 0, ds_list) < 0)
	    goto done;
	break;
    case Y_LIST:
	if (yang2key_list(ys, keys0, vars0, ds_list) < 0)
	    goto done;
	break;
    case Y_LEAF_LIST:
	if (yang2key_leaf_list(ys, keys0, vars0, ds_list) < 0)
	    goto done;
	break;
    default:
	break;
    }

    for (i=0; i<ys->ys_len; i++)
	if ((yc = ys->ys_stmt[i]) != NULL){
	    if ((keys = cvec_dup(keys0)) == NULL){
		clicon_err(OE_DB, errno, "%s: chunk", __FUNCTION__); 
		goto done;
	    }
	    if ((vars = cvec_dup(vars0)) == NULL){
		clicon_err(OE_DB, errno, "%s: cvec_dup", __FUNCTION__); 
		goto done;
	    }
	    if (yang2key_stmt(yc, keys, vars, ds_list) < 0)
		goto done;
	    cvec_free(vars);
	    cvec_free(keys);
	}

    retval = 0;
  done:
    return retval;
}

/*! Translate yang spec to a key-based dbspec used for qdb. 
 *
 * Translate yang spec to a dbspec. 
 * There are four kinds, two for commands, two for variables:
 *  list      -->   g[i] $!i <x>; g[].a...
 *  container -->   g $x; g.a
 *  leaf      -->   $x
 *  leaf-list -->   $x[a]
 */
dbspec_key *
yang2key(yang_spec *yspec)
{
    yang_stmt      *ys = NULL;
    dbspec_key *ds_list = NULL; 
    cvec           *keys;
    cvec           *vars;
    int i;

    /* Traverse YANG specification: loop through statements */
    for (i=0; i<yspec->yp_len; i++)
	if ((ys = yspec->yp_stmt[i]) != NULL){
	    if ((vars = cvec_new(0)) == NULL){
		clicon_err(OE_DB, errno, "%s: cvec_new", __FUNCTION__); 
		goto err;
	    }
	    if ((keys = cvec_new(0)) == NULL){
		clicon_err(OE_DB, errno, "%s: cvec_new", __FUNCTION__); 
		goto err;
	    }
	    if (yang2key_stmt(ys, keys, vars, &ds_list) < 0)
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

/*===================================================================================
 * key2yang
 *==================================================================================*/

/*! Translate from key-based dbspec to yang spec
 *
 * See also yang2key
 */
yang_spec *
key2yang(dbspec_key *db_spec)
{
    dbspec_key  *ds;
    cvec            *subvh;
    cg_var          *v = NULL;
    char           **vec;
    int              nvec;
    int              i;
    char            *key;
    int              isvec;
    yang_spec       *yspec = NULL;
    yang_stmt       *ym; /* module */
    yang_node       *yp; /* parent */
    yang_stmt       *ys;
    yang_stmt       *yl;
    yang_stmt       *yt;
    char            *str;

    if ((yspec = yspec_new()) == NULL)
	goto err;
    if ((ym = ys_new(Y_MODULE)) == NULL)
	goto err;
    ym->ys_argument = strdup("my module");
    if (yn_insert((yang_node*)yspec, ym) < 0)
	goto err; 
    /* Parse through all spec lines */
    for (ds=db_spec; ds; ds=ds->ds_next){
	clicon_debug(1, "%s: spec line: %s", __FUNCTION__, ds->ds_key);
	subvh = db_spec2cvec(ds);
	if ((vec = clicon_strsplit(ds->ds_key, ".", &nvec, __FUNCTION__)) == NULL){
	    clicon_err(OE_DB, errno, "%s: strsplit", __FUNCTION__); 
	    goto err;
	}
	ys = NULL;
	yp = (yang_node*)ym; /* parent is module, starting point */

	/* Parse through all keys in a spec-line, eg "a.b.c" */
	for (i=0; i<nvec; i++){ 
	    key = vec[i];
	    clicon_debug(1, "%s: \tkey: %s", __FUNCTION__, vec[i]);
	    isvec = 0;
	    if (key_isvector(key)){
		isvec++;
		key[strlen(key)-2] = '\0';
	    }
	    if (!isvec){
		/* Create container node */
		if ((ys = yang_find_specnode(yp, key)) == NULL){
		    if ((ys = ys_new(Y_CONTAINER)) == NULL)
			goto err;
		    ys->ys_argument = strdup(key);
		    if (yn_insert(yp, ys) < 0)
			goto err;
		}
	    }
	    else {
		/* Find unique key and add that as sub (if it does not already exist) */
		v = NULL; /* unique variable */
		while ((v = cvec_each(subvh, v))) 
		    if (cv_flag(v, V_UNIQUE))
			break;
		if (v == NULL){
		    clicon_err(OE_DB, 0, "Spec has no matching unique variable"); 
		    goto err;
		}
		/* Create list node */
		if ((ys = yang_find_specnode(yp, key)) == NULL){
		    if ((ys = ys_new(Y_LIST)) == NULL)
			goto err;
		    ys->ys_argument = strdup(key);
		    if (yn_insert(yp, ys) < 0)
			goto err;
		    /* Create key */
		    if ((yl = ys_new(Y_KEY)) == NULL) 
			goto err;
		    yl->ys_argument = strdup(cv_name_get(v));
		    if (yn_insert((yang_node*)ys, yl) < 0)
			goto err; 
		    /* Create leaf */
		    if ((yl = ys_new(Y_LEAF)) == NULL) 
			goto err; 
		    yl->ys_argument = strdup(cv_name_get(v));
		    if (yn_insert((yang_node*)ys, yl) < 0)
			goto err;
		    if ((str = db_spec2str(ds)) == NULL)
			goto err;
		    if (yang_dbkey_set(yl, str) < 0) /* XXX ys? */
			goto err;
		    if ((yl->ys_cv = cv_dup(v)) == NULL){
			clicon_err(OE_DB, errno, "%s: cv_dup", __FUNCTION__); 
			goto err;
		    }
		    if ((yt = ys_new(Y_TYPE)) == NULL) 
			goto err; 
		    yt->ys_argument = strdup(cv2yang_type(cv_type_get(v)));
		    if (yn_insert((yang_node*)yl, yt) < 0)
			goto err;
		}
		yp = (yang_node*)ys;
	    } /* isvec */
	} /* for i */
#ifdef notyet
	/* Remaining unique variables are added at the end */
	while ((v = cvec_each(subvh, v))) 
	    if (!cv_flag(v, V_UNIQUE))
		continue;
#endif
	/* Go through all non-unique variables and append to syntax */
	v = NULL;
	while ((v = cvec_each(subvh, v))) {
	    if (cv_flag(v, V_UNIQUE))
		continue; /* LEAF_LIST */
	    if ((yl = ys_new(ds->ds_vector?Y_LEAF_LIST:Y_LEAF)) == NULL) 
		goto err; 
	    yl->ys_argument = strdup(cv_name_get(v));
	    if (yn_insert((yang_node*)ys, yl) < 0)
		goto err;
	    /* The unique variable is added as indexvar in co */
	    if ((str = db_spec2str(ds)) == NULL)
		goto err;
	    if (yang_dbkey_set(yl, str) < 0) /* XXX ys? */
		goto err;
	    if ((yl->ys_cv = cv_dup(v)) == NULL){
		clicon_err(OE_DB, errno, "%s: cv_dup", __FUNCTION__); 
		goto err;
	    }
	    if ((yt = ys_new(Y_TYPE)) == NULL) 
		goto err; 
	    yt->ys_argument = strdup(cv2yang_type(cv_type_get(v)));
	    if (yn_insert((yang_node*)yl, yt) < 0)
		goto err;
	}
    } /* for ds */
//  ok:
    unchunk_group(__FUNCTION__);  
    return yspec;
  err:
    if (yspec)
	yspec_free(yspec);
    unchunk_group(__FUNCTION__);  
    return NULL;
}
