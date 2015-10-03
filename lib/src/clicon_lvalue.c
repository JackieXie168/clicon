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
 * Lvalues
 */

#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <syslog.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include "clicon_log.h"
#include "clicon_err.h"
#include "clicon_queue.h"
#include "clicon_hash.h"
#include "clicon_db.h"
#include "clicon_lvmap.h"
#include "clicon_string.h"
#include "clicon_chunk.h"
#include "clicon_handle.h"
#include "clicon_dbspec_key.h"
#include "clicon_lvalue.h"
#include "clicon_dbutil.h"
#include "clicon_lvalue.h"
#include "clicon_dbvars.h"

/*
 * key_isplain
 * Return 1 is plain
 * 0 otherwise
 */
int
key_isplain(char *key)
{
    if (key_isanyvector(key) || key_isregex(key))
	return 0;
    
    return 1;
}

/* 
 * key_isvector
 * Return 1 of key end swith '[]'. 
 * 0 otherwise.
 */
int 
key_isvector(char *key)
{
    int len;
    
    len = strlen(key);
    if (len>1 && key[len-2]=='[' && key[len-1]==']')
	return 1;
    return 0;
}

/* 
 * key_isregex
 * Return 1 of key starts with '^'. 
 * 0 otherwise.
 */
int 
key_isregex(char *key)
{
    int len;
    
    len = strlen(key);
    return (len && key[0] == '^');
}

/* 
 * key_iscomment
 * Return 1 of key starts with '#'. 
 * 0 otherwise.
 */
int 
key_iscomment(char *key)
{
    int len;
    
    len = strlen(key);
    return (len && key[0] == '#');
}

/* 
 * key_isvector_n
 * Return 1 of key ends with '.n'. 
 * 0 otherwise.
 */
int 
key_isvector_n(char *key)
{
    return (strcmp(key+strlen(key)-2, ".n")==0);
}

/* 
 * key_iskeycontent
 * Return 1 of key ends with '.n.<>'. 
 * 0 otherwise.
 */
int 
key_iskeycontent(char *key)
{
    return (strstr(key, ".n.")!=NULL);
}


/*
 * lv_arg_2_cv_type
 * Translate from an lvalue argument to a cligen variable type
 * See lvalue.txt
 */
enum cv_type
lv_arg_2_cv_type(char lv_arg)
{
    enum cv_type cv_type = CGV_ERR;

    switch (lv_arg) {
    case 'n':	/* 32-bit int */
	cv_type = CGV_INT32;
	break;
    case 'l':	/* 64-bit long */
	cv_type = CGV_INT64;
	break;
    case 'b':	/* boolean */
	cv_type = CGV_BOOL;
	break;
    case 's':	/* String */
	cv_type = CGV_STRING;
	break;
    case 'i':	/* Interface */
	cv_type = CGV_INTERFACE;
	break;
    case 'a':	/* IPv4 address */
	cv_type = CGV_IPV4ADDR;
	break;
    case 'p':	/* IPv4 prefix */
	cv_type = CGV_IPV4PFX;
	break;
    case 'A':	/* IPv6 address */
	cv_type = CGV_IPV6ADDR;
	break;
    case 'P':	/* IPv6 prefix */
	cv_type = CGV_IPV6PFX;
	break;
    case 'm':   /* Mac address */
	cv_type = CGV_MACADDR;
	break;
    case 'u':   /* URL */
	cv_type = CGV_URL;
	break;
    case 'U':   /* UUID */
	cv_type = CGV_UUID;
	break;
    case 't':   /* ISO 8601 time */
	cv_type = CGV_TIME;
	break;
    case 'r':   /* rest */
	cv_type = CGV_REST;
	break;
    }
    return cv_type;
}


static struct lvalue *
lv_next(struct lvalue *lv)
{
    return (struct lvalue *)(lv->lv_val + lv->lv_len);
}

/*
 * Return a pointer to the n:th lvalue in val.
 */
struct lvalue *
lv_element(char *val, int vlen, int n)
{
    int i;
    struct lvalue *lv;
    
    i = 0;
    for (lv=(struct lvalue *)val, i=0; 
	 (void*)lv-(void*)val < vlen; 
	 lv=lv_next(lv), i++){
	if (i==n)
	    return lv;
    }
    return NULL;
}

/*
 * Dump a print of the lvalues of val on f
 */
int
lv_dump(FILE *f, char *lvec, int vlen)
{
    int            i;
    struct lvalue *lv;
    cg_var        *cv; 
    int            retval = -1;
    char          *str;

    for (lv=(struct lvalue *)lvec; 
	 (void*)lv-(void*)lvec < vlen; 
	 lv=lv_next(lv)){
	fprintf(f, "\ttype: %s len: %3d data: ", 
		cv_type2str(lv->lv_type),
		lv->lv_len);
	if ((cv = cv_new(CGV_STRING)) == NULL){
	    clicon_err(OE_UNIX, errno, "Allocating cligen object"); 
	    goto done;
	}
	if ((str = lv2str(lv)) != NULL) {
	  fprintf(f, "%s%s%s\n",
		  cv_inline(cv_type_get(cv)) ? "" : "\"",
		  str,
		  cv_inline(cv_type_get(cv)) ? "" : "\"");
	  free(str);
	} else {
	  for (i=0; i<lv->lv_len; i++)
	    fprintf(f, "%02x", lv->lv_val[i]&0xff);
	  fprintf(f, "\n");
	}
	cv_free(cv);
    }
    retval = 0;
  done:
    return retval;
}

#ifdef notyet
/*
 * lvec_cgv_append
 * Translate a cligen_variable to an lvalue, and append the lvalue to 
 * an existing lvec. Return the new lvec with updated length.
 */
static int
lvec_cgv_append(cg_var *cgv, char **lvec, int *vlen)
{
    int len;
    int hlen;
    struct lvalue *lv;

    len = cv_len(cgv);
    hlen = (void*)lv->lv_val - (void*)lv;
    if ((*lvec = realloc(*lvec, (*vlen)+hlen+len)) == NULL){
	clicon_err(OE_UNIX, errno, "realloc");
	return -1;
    }
    lv = (struct lvalue *)(*lvec + *vlen);
    lv->lv_len = len;
    lv->lv_type = cv_type_get(cgv);
    /* Special case: string is a pointer */
    if (cv_inline(lv->lv_type))
	memcpy(lv->lv_val, &cgv->u, len);
    else
	/* XXX URLs not supported */
	memcpy(lv->lv_val, cv_string_get(cgv), len); 
    *vlen += hlen + len;
    return 0;
}
#endif /* notyet */

/*
 * keyindexes are a way to set dynamic arrays of keys.
 */
int
keyindex_max_get(char *dbname, char *key, int *index, int regex)
{
    char keyn[strlen(key)+10];
    size_t len = sizeof(int);
    int npairs;
    struct db_pair *pairs;

    snprintf(keyn, strlen(key)+9, "%s%s.n", key, (regex ? "\\" : ""));
    if (regex) {
      
      npairs = db_regexp(dbname, keyn, __FUNCTION__, &pairs, 1);
      switch (npairs) {
      case -1:
	*index = -1;	/* Report error */
	break;
      case 0:
	*index = 0;	/* No match. Set index to 0 */
	break;
      case 1:
	/* Copy index */
	memcpy (index, pairs[0].dp_val, sizeof (*index));
	break;
      default:
	clicon_err(OE_DB, 0,
		"keyindex_max_get: Multiple key matches: %s", key);
	*index = -1;
	break;
	
      }
      unchunk_group (__FUNCTION__);

    } else {
	if (db_get(dbname, keyn, index, &len) < 0){
	    if (clicon_suberrno == 3){ // XXX broken file (if empty)
		clicon_err_reset();
		len = 0;
	    }
	    else
		return -1;
	}
      if (len == 0)
	*index = 0;
    }
    return 0;
}

static int
keyindex_max_set(char *dbname, char *basekey, int index)
{
    char key[strlen(basekey)+4];
    size_t len = sizeof(int);
    int retval;

    snprintf(key, strlen(basekey)+3, "%s.n", basekey);
    retval = db_set(dbname, key, &index, len);
    if (retval < 0)
	return -1;
    return 0;
}

/*
 * lv_matchvar
 * Compare two variable lists (eg database key variables) and check if they are
 * 'equal' or not. If 'cmpall' flag is 0 'equal' means that they have the same
 * unique index, while if cmpall flag is 1, 'equal' means that all variables are
 * the same and have equal values.
 *
 * XXX: There is something seriously wring with this function. 
 * v1 and v2 are not used in the loop but are set two times.
 * A try to rewrite the function (below) made something else break.
 *
 *
 * Return values:
 * 0: No match
 * 1: Match
 */
#if 1
/*
 */
int
lv_matchvar (cvec *vec1, cvec *vec2, int cmpall)
{
  cvec *v1, *v2, *tmp;
  cg_var *cv1, *cv2;
  int laps = 2; /* Number of laps in loop. One for each cmp direction */

  v1 = vec1;
  v2 = vec2;
  while(laps-- > 0) {

    cv1 = cv2 = NULL;
    while ((cv1 = cvec_each (vec1, cv1))) {
      /* Only compare "unique" variables. */
      if (!cmpall && !cv_flag(cv1, V_UNIQUE))
	continue;
      
      cv2 = cvec_find (vec2, cv_name_get(cv1));
      if (cv2 == NULL)	 /* Not found: mismatch! */
	return 0;
      
      if (cv_flag(cv2, V_WILDCARD))
	continue;
      
      if (cv_type_get(cv1) != cv_type_get(cv2)) /* Type mismatch */
	return 0;
      
      if (cv_cmp(cv1, cv2) != 0) /* mismatch */
	return 0;
    }

    /* Now swap vh1 with vh2 and run again */
    tmp = v1;
    v1 = v2;
    v2 = tmp;
  }

  return 1;
}
#else
int
lv_matchvar (cvec *vec1, cvec *vec2, int cmpall)
{
    cvec *vha, *vhb;
    cg_var *cv1, *cv2;
    int laps = 2; /* Number of laps in loop. One for each cmp direction */

    vha = vec1;
    vhb = vec2;
    while(laps-- > 0) {

	cv1 = cv2 = NULL;
	while ((cv1 = cvec_each (vha, cv1))) {
	    /* Only compare "unique" variables. */
	    if (!cmpall && !cv_flag(cv1, V_UNIQUE))
		continue;
      
	    cv2 = cvec_find (vhb, cv_name_get(v1));
	    if (cv2 == NULL)	 /* Not found: mismatch! */
		return 0;
      
	    if (cv_flag(cv2, V_WILDCARD))
		continue;
      
	    if (cv_type_get(cv1) != cv_type_get(v2)) /* Type mismatch */
		return 0;
      
	    /* Data mismatch */
	    if (cv_cmp(cv1, cv2) != 0)
		return 0;
	}

	/* Now swap vha with vhb and run again */
	vha = vec2;
	vhb = vec1;
    }

    return 1;
}
#endif

/*
 * Get next _SEQ value for a vector.
 */
int
lv_next_seq(char *dbname, char *basekey, char *varname, int increment)
{
  int i;
  int seq = 0;
  int val;
  int retval = -1;
  char *key;
  int npairs;
  struct db_pair *pairs;
  cg_var *cv;
  cvec *vr = NULL;

  if ((key = chunk_sprintf(__FUNCTION__, "%s\\.[0-9]+$", basekey)) == NULL){
      clicon_err(OE_UNIX, errno, "chunk");
      goto catch;
  }
  /* Get all keys/values for vector */
  npairs = db_regexp(dbname, key, __FUNCTION__, &pairs, 0);
  if (npairs < 0)
    goto catch;

  /* Loop through list and check variable */ 
  for (i = 0; i < npairs; i++) {

    if ((vr = lvec2cvec (pairs[i].dp_val, pairs[i].dp_vlen)) == NULL)
      goto catch;
      
    if ((cv = cvec_find (vr, varname[0]=='!'?varname+1:varname)) && cv_type_get(cv) == CGV_INT32) {
      val = cv_int_get(cv);
      if (val > seq)
	seq = val;
    }
    cvec_free (vr);
    vr = NULL;
  }

  retval = seq - (seq % increment) + increment;

  /* Fall through */
 catch:
  if (vr)
    cvec_free(vr);
  unchunk_group (__FUNCTION__);
  return retval;
}



#ifdef DB_KEYCONTENT
/*
 * Find a vector index for a 'basekey'. If a matching entry is found 
 * in 'dbname' based on 'setvars', that index will be returned.
 * Returns index of key to be written, or -1 if error.
 *
 * We use several keys here. Lets take an example with 'A[] $!a $b' to illustrate
 * If we set A[] $!a=42 b=apa
 * We read A.n to get the max index
 * We read A.n.42 to get index (i) if it exists. (set matched)
 * If not, we read A.n to get the max index, use that, increment and write back to A.n
 * We return i.
 * Note the key A.i is not written, but the caller must write such a key, otherwise
 * the database will be corrupt.
 * @param[in]  setvars vector of unique indexes
 * @param[out] match   index of key that matches
 */
int
db_lv_vec_find(dbspec_key *dbspec, /* spec list */
	       char       *dbname, 
	       char       *basekey,
	       cvec       *setvars, 
	       int        *match)
{
    int             i;
    int             retval = -1;
    char           *key = NULL;
    size_t          lvlen;
    char           *str;
    dbspec_key *ds;
    
    if (match)
	*match = -1;

    /* XXX: Create dummy index key just for dbspec_match */
    if((key = chunk_sprintf(__FUNCTION__, "%s.0", basekey)) == NULL) {
	clicon_err(OE_UNIX, errno, "chunk");
	goto quit;
    }
    if ((ds = key2spec_key(dbspec, key)) == NULL){
	clicon_err(OE_DB, 0, "%s: %s not found in database spec\n",
		   __FUNCTION__, key);
	goto quit;
    }
    /* vu is last unique variable */
    if ((str = dbspec_unique_str(ds, setvars)) == NULL)
	goto quit;
    /* keycontent key: A.n.<value of unique vars> */
    if((key = chunk_sprintf(__FUNCTION__, "%s.n.%s", basekey, str)) == NULL) {
	clicon_err(OE_UNIX, errno, "chunk");
	free(str);
	goto quit;
    }
    free(str);
    /* Read value of key.n.<value> */
    lvlen = sizeof(i);
    if (db_get(dbname, key, (void*)&i, &lvlen) < 0)
	goto quit;
    if (lvlen){  /* entry exists, use value */
	assert(lvlen == sizeof(int));
	if (match)
	    *match = i;
    }
    else { /* no entry, create */
	if (keyindex_max_get(dbname, basekey, &i, 0) < 0)
	    goto quit;
	/* create A.n.<value> = <i> entry */
	if (db_set(dbname, key, &i, sizeof(i)) <0)
	    goto quit;
	if (keyindex_max_set(dbname, basekey, i+1) < 0)
	    goto quit;
    }
    retval = i;
quit:
    unchunk_group(__FUNCTION__);
    return retval;
}
#else /* DB_KEYCONTENT */
/*
 * Find a vector index for a 'basekey'. If a matching entry is found 
 * in 'dbname' based on 'setvars', that index will be returned.
 */
int
db_lv_vec_find(dbspec_key *dbspec, /* spec list */
	       char *dbname, 
	       char *basekey,
	       cvec *setvars, 
	       int *match)
{

    int i;
    int maxi;
    int hole = -1;
    int retval = -1;
    char *key = NULL;
    size_t len;
    size_t lvlen;
    char *lvec = NULL;  /* Value vector consisting of lvalues */
    cvec *dbvars = NULL;
    
    if (match)
	*match = -1;
    
    /* Find max index */
    if (keyindex_max_get(dbname, basekey, &maxi, 0) < 0)
	goto quit;
    for (i = 0; i < maxi; i++) {  	/* Replace if equal */
        if((key = chunk_sprintf(__FUNCTION__, "%s.%u", basekey, i)) == NULL) {
	    clicon_err(OE_UNIX, errno, "chunk");
	    goto quit;
	}
	if (db_get_alloc(dbname, key, (void**)&lvec, &len) < 0)
	    goto quit;
	unchunk(key);
	if (len == 0){
	    if (hole < 0)
		hole = i;
	    continue;
	}
	lvlen = len;
	dbvars = lvec2cvec (lvec, lvlen);
	free (lvec); 
	lvec=NULL;
	if (dbvars == NULL)
	    goto quit;
	
	/* Check if Value matches */
	if (lv_matchvar (dbvars, setvars, 0))
	    break;
	cvec_free(dbvars);
	dbvars = NULL;
    }
    if (i >= maxi){ /* No match found */
	if (hole < 0){ /* No holes */
	    i = maxi;
	    if (keyindex_max_set(dbname, basekey, i+1) < 0)
		goto quit;
	}
	else
	    i = hole; /* Use a hole in the vector */
    }
    else
	if (match)
	    *match = i;
    retval = i;
quit:
    if (retval < 0) {
	if (dbvars != NULL) {
	    cvec_free (dbvars);
	    dbvars = NULL;
	}
    }
    unchunk_group(__FUNCTION__);
    return retval;
}
#endif /* DB_KEYCONTENT */


/*
 * db_lv_set
 * Set key in database. 
 * IN:
 *   spec    Specification of key below
 *   dbname  Database
 *   key     Key
 *   vec     variable, value vector
 *   op      DELETE, SET, MERGE
 */
int 
db_lv_set(dbspec_key *spec, 
	  char           *dbname,  
	  char           *key,  
	  cvec           *vec, 
	  lv_op_t         op)
{
    int              retval = -1;
    cvec            *old = NULL;
    cvec            *def;
    cg_var          *v = NULL;

    /* Merge with existing variable list. Note, some calling functions
     do this before calling this function, eg db_lv_lvec_set() */
    if (op  == LV_MERGE) { /* Add vars to vh from db */
	if ((old = dbkey2cvec(dbname, key)) != NULL){
	    if (spec->ds_vector){ /* If vector append new unique cv:s */
		/* Since vector, assume same variables eg x, y below */
		/* The values in a leaf-list MUST be unique.*/
		/*
		 * vec:[x=42;y=99] old:[x=42;y=100] => vec:[x=42;y=99;x=42;y=100]
		 * vec:[x=42;y=99] old:[x=42;y=100] => vec:[x=42;y=99;y=100] ??
		 * vec:[x=42;y=99] old:[x=42;y=99]  => vec:[x=42;y=99]
		 */
		if (cvec_merge2(vec, old) < 0)
		    goto quit;
	    }
	    else
		if (cvec_merge(vec, old) < 0)
		    goto quit;
	}
    }
    /* Check sanity */
    if (sanity_check_cvec(key, spec, vec) < 0)     /* sanity check */
	goto quit;
    /* 
     * Merge with dataspec default values (if not present) It may be
     * 'early' to add default values, but if we wait to validate or
     * even later there will a diff with running db. But see also
     * generic_validate() where we also add default values for netconf
     * for example.
     */
    def = db_spec2cvec(spec);
    /* Mark them as default values */
    while ((v = cvec_each(def, v))) 
	cv_flag_set(v, V_DEFAULT);
    if (cvec_merge(vec, def) < 0)
	goto quit;

    /* write to database, key and a vector of variables */
    if (cvec2dbkey(dbname, key, vec) < 0)
	goto quit;
    
    retval = 0;

quit:
    if (old)
	cvec_free (old);

    return retval;
}

/*
 * db_lv_vec_set
 * Find a subkey which matches the lvalue vector lvec.
 * set the value of that subkey to lvec.
 */
static int
db_lv_vec_set(dbspec_key *dbspec,
	      char *dbname, 
	      char *basekey, 
	      cvec *setvars, 
	      lv_op_t op)
{
    char            *key = NULL;
    int              i;
    int              matched;
    int              retval = -1;
    cvec            *dbvars = NULL;
    dbspec_key      *spec;
    size_t           lvlen;
    char            *lvec = NULL;

    if ((i = db_lv_vec_find(dbspec, dbname, basekey, setvars, &matched)) < 0)
	goto quit;
    if ((key = chunk_sprintf(__FUNCTION__, "%s.%u", basekey, i)) == NULL) {
	clicon_err(OE_UNIX, errno, "chunk");
	goto quit;
    }
    if (matched >= 0)
	clicon_debug(2, "%s: new lvalue matches key %s", __FUNCTION__, key);

    /* Merge variables? */
    if(op == LV_MERGE) {
	if (db_get_alloc(dbname, key, (void*)&lvec, &lvlen) < 0)
	    goto quit;
	if (lvlen > 0){
	    if ((dbvars = lvec2cvec(lvec, lvlen)) == NULL)
		goto quit;
	    free(lvec);
	    cvec_merge(setvars, dbvars); /* XXX Here is where two same variables dont work */
	    cvec_free (dbvars);
	}
    }

    if ((spec = key2spec_key(dbspec, key)) == NULL){
	clicon_log(LOG_ERR, "%s: %s not found in database spec\n",
		   __FUNCTION__, key);
	goto quit;
    }
    if (db_lv_set(spec, dbname, key, setvars, LV_SET) < 0)
	goto quit;
    clicon_debug(2, "%s: set key %s to new lvalue", __FUNCTION__, key);
    retval = 0;
quit:
    unchunk_group(__FUNCTION__);
    return retval;
}



#ifdef DB_KEYCONTENT
/*
 * db_lv_vec_replace(dbspec_key *dbspec, 
 * Specialized function replacing the (last) unique key of an entry.
 * Example: 
 *     A.B[] $!a=42 $b $c 
 * should be replaced by
 *     A.B[] $!a=99 $b $c 
 * Args:
 *   basekey: e.g. A.B[]
 *   prevval: eg 42
 *   prevval: eg 99
*/
int
db_lv_vec_replace(char *dbname, 
		  char *basekey, 
		  char *prevval,
		  char *newval)
{
    int              retval = -1;
    char            *bkey = NULL;
    char            *prevkey;
    char            *newkey;
    int             i;
    size_t          lvlen;

    if (!key_isvector(basekey))
	goto quit;
    bkey = strdup(basekey);
    bkey[strlen(basekey)-2] = '\0';
    /* keycontent key: A.n.<value of unique var> */
    if((prevkey = chunk_sprintf(__FUNCTION__, "%s.n.%s", bkey, prevval)) == NULL) {
	clicon_err(OE_UNIX, errno, "chunk");
	goto quit;
    }
    if((newkey = chunk_sprintf(__FUNCTION__, "%s.n.%s", bkey, newval)) == NULL) {
	clicon_err(OE_UNIX, errno, "chunk");
	goto quit;
    }
    lvlen = sizeof(int);
    if (db_get(dbname, prevkey, (void*)&i, &lvlen) < 0)
	goto quit;
    if (db_set(dbname, newkey, &i, lvlen) <0)
	goto quit;
    if (db_del(dbname, prevkey) < 0)
	goto quit;
    retval = 0;
  quit:
    if (bkey)
	free(bkey);
    unchunk_group(__FUNCTION__);
    return retval;
}


/*
 * db_lv_vec_del
 * Find subkey which matches the lvalue vector lvec, and remove them
 */
static int
db_lv_vec_del(dbspec_key *dbspec, /* spec list */
	      char *dbname, 
	      char *basekey, 
	      cvec *setvars)
{
    int             retval = -1;
    char           *key;
    char           *str;
    int             i;
    size_t          lvlen;
    dbspec_key  *ds;

    /* XXX: Create dummy index key just for key2spec_key */
    if((key = chunk_sprintf(__FUNCTION__, "%s.0", basekey)) == NULL) {
	clicon_err(OE_UNIX, errno, "chunk");
	goto quit;
    }
    if ((ds = key2spec_key(dbspec, key)) == NULL){
	clicon_log(LOG_ERR, "%s: %s not found in database spec\n",
		   __FUNCTION__, key);
	goto quit;
    }
    /* vu is last unique variable */
    if ((str = dbspec_unique_str(ds, setvars)) == NULL)
	goto quit;
    /* keycontent key: A.n.<value of unique vars> */
    if((key = chunk_sprintf(__FUNCTION__, "%s.n.%s", basekey, str)) == NULL) {
	clicon_err(OE_UNIX, errno, "chunk");
	free(str);
	goto quit;
    }
    free(str);
    /* Read value of key.n.<value> */
    lvlen = sizeof(i);
    if (db_get(dbname, key, (void*)&i, &lvlen) < 0)
	goto quit;
    if (lvlen){  /* entry exists, delete */
	/* Delete content key: A.n.<value> */
	if (db_del(dbname, key) < 0)
	    goto quit;
	/* Delete index key: A.<i> */
	if((key = chunk_sprintf(__FUNCTION__, "%s.%u", basekey, i)) == NULL) {
	    clicon_err(OE_UNIX, errno, "chunk");
	    goto quit;
	}
	if (db_del(dbname, key) < 0)
	    goto quit;
    }
    retval = 0;
  quit:
    return retval;
}
#else /* DB_KEYCONTENT */
/*
 * db_lv_vec_del
 * Find all subkey which matches the lvalue vector lvec, and remove them
 */
static int
db_lv_vec_del(dbspec_key *dbspec, /* spec list */
	      char *dbname, 
	      char *basekey, 
	      cvec *setvars)
{

    char *key;
    int i, maxi, hole;
    int retval = -1;
    size_t len;
    char *lvec = NULL;  /* Value vector consisting of lvalues */
    cvec *dbvars = NULL;

    hole = -1; /* empty hole in vector */
    if ((key = malloc(strlen(basekey)+10)) == NULL){
	clicon_err(OE_UNIX, errno, "malloc");
	goto quit;
    }
    /* Find max index */
    if (keyindex_max_get(dbname, basekey, &maxi, 0) < 0)
	goto quit;

    for (i=0; i<maxi; i++){  	/* Remove if equal */
	snprintf(key, strlen(basekey)+9, "%s.%u", basekey, i);
	if (db_get_alloc(dbname, key, 
			 (void**)&lvec, &len) < 0)
	  goto quit;
	if (len == 0){
	  if (hole<0)
	    hole = i;
	  continue;
	}
	hole = -1; /* reset */
	if ((dbvars = lvec2cvec (lvec, len)) == NULL)
	  goto quit;
	free (lvec); lvec=NULL;
	
	/* Check if Value matches */
	if (lv_matchvar (dbvars, setvars, 0)) {
	    clicon_debug(1, "%s: remove key %s\n", 
			__FUNCTION__, key);
	    if (db_del(dbname, key) < 0)
		goto quit;
	    if (hole<0)
	        hole = i;
	}
	if (dbvars){
	    cvec_free(dbvars);
	    dbvars = NULL;
	}

    }
    /* Shrink maxi to nearest unused */
    if (hole >= 0)
	if (keyindex_max_set(dbname, basekey, hole+1) < 0)
	    goto quit;
    retval = 0;
  quit:
    if (dbvars)
	cvec_free(dbvars);
    if (key)
	free(key);
    return retval;
}
#endif /* DB_KEYCONTENT */



/*
 * dv_lv_del
 * Delete keys from database. If key starts with '^' it is regarded as a
 * regular expression to be used in matching database keys.
 * Returns number of deletes keys or -1 on failure.
 */
static int
db_lv_del(char *dbname, char *basekey)
{
    int   ret;
    int   retval;
    int   npairs;
    struct db_pair *pairs;

    /* If not a regexp key, return db_del() */
    if (!key_isregex(basekey)) 
        return db_del(dbname, basekey);

    retval = 0;
    npairs = db_regexp(dbname, basekey, __FUNCTION__, &pairs, 0);
    for (--npairs; npairs >= 0; npairs--) {
      if ((ret = db_del (dbname, pairs[npairs].dp_key)) < 0) {
	retval = -1;
	break;
      }
      retval += ret;
    }
    unchunk_group(__FUNCTION__);

    return retval;
}

/*
 * db_lv_spec2dbvec
 * Create a db vector key based on db-spec variables
 */
int
db_lv_spec2dbvec(dbspec_key *dbspec, 
		 char *dbname, char *veckey, int idx, cvec *vhead)
{
    char *key;
    int retval = -1;
    cg_var *v;
    cg_var *vs;
    cg_var *newv;
    cvec *vh = NULL;
    cvec *vhnew = NULL;
    dbspec_key *os, *spec;

    if ((os = key2spec_key(dbspec, veckey)) == NULL) {
	fprintf(stderr, "No data specification for '%s'\n", veckey);
	goto catch;
    }
    if ((vhnew = cvec_new(0)) == NULL)
	goto catch;


    vh = db_spec2cvec(os);
    vs = NULL; 
    while ((vs = cvec_each(vh, vs))) {
	if (cv_flag(vs, V_UNIQUE) == 0)
	    continue;
	if ((v = cvec_find (vhead, cv_name_get(vs))) == NULL) {
	    clicon_err(OE_UNIX, errno, "key not found in var-list: %s\n", cv_name_get(vs));
	    goto catch;
	}
	if((newv = cvec_add(vhnew, cv_type_get(v))) == NULL) {
	    clicon_err(OE_UNIX, errno, "var_add");
	    goto catch;
	}
	if (cv_cp(newv, v) != 0) {
	    clicon_err(OE_UNIX, errno, "cv_cp");
	    goto catch;
	}
    }
    /* Format real key */
    if (strcmp(veckey+strlen(veckey)-2, "[]") == 0)
	key = chunk_sprintf(__FUNCTION__, "%.*s.%u", strlen(veckey)-2, veckey, idx);
    else if (strrchr (veckey, '.'))
	key = chunk_sprintf(__FUNCTION__, "%.*s.%u", strlen(veckey)-strlen(strrchr(veckey, '.')), veckey, idx);
    else
	key = chunk_sprintf(__FUNCTION__, "%s.%u", veckey, idx);
    
    if (key == NULL) {
	clicon_err(OE_UNIX, errno, "chunk");
	goto catch;
    }

    if ((spec = key2spec_key(dbspec, key)) == NULL) {
	fprintf(stderr, "No data specification for '%s'\n", key);
	goto catch;
    }
    if (db_lv_set(spec, dbname, key, vhnew, LV_SET) < 0)
	goto catch;

    retval = 0;

catch:	
    if (vhnew)
	cvec_free(vhnew);
    unchunk_group(__FUNCTION__);

    return retval;

}

char *
db_lv_op_keyfmt (dbspec_key *dbspec,
		  char *dbname, 
		 char *basekey,
		 cvec *cvec,
		 const char *label)
{
    int i;
    int idx;
    int len;
    int match;
    int nvec;
    char **vec; 
    char **new;
    char *str;
    char *tmp;
    cg_var *cv = NULL;
    char *key = NULL;
    
    if ((vec = clicon_sepsplit (basekey, ".", &nvec, __FUNCTION__)) == NULL){
	clicon_err(OE_UNIX, errno, "clicon_strsplit");
	goto catch;
    }

    if ((new = chunk(nvec * sizeof(char *), __FUNCTION__)) == NULL){
	clicon_err(OE_UNIX, errno, "malloc");
	goto catch;
    }
	
    /* Loop through key nodes and substitute as required */
    for (i = 0; i < nvec; i++) {
	str = vec[i];
	
	/* Substitute variable ? */
	if (str[0] == '$') { 
	    if ((cv = cvec_find (cvec, str+1)) == NULL){
		clicon_err(OE_UNIX, errno, "cvec_find");
		goto catch;
	    }

	    /* Can use cv2str_dup() here, but this code uses chunks instead of malloc */
	    if ((len = cv2str (cv, NULL, 0)) <= 0){
		clicon_err(OE_UNIX, errno, "cv2str");
		goto catch;
	    }
	    if ((tmp = chunk(len+1, __FUNCTION__)) == NULL) {
		clicon_err(OE_UNIX, errno, "chunk");
		goto catch;
	    }
	    if (cv2str(cv, tmp, len+1) != len){
		clicon_err(OE_UNIX, errno, "cv2str");
		goto catch;
	    }
	    str = tmp;
	    cv = NULL;
	}
		   
	/* Do we have a mid-key vector id to resolve? */
	else if(i < nvec-1 && strlen(str) > 2 && 
		strcmp(str+strlen(str)-2, "[]")==0) {
	    str[strlen(str)-2] = '\0';
	    new[i] = vec[i];
	    if ((tmp = clicon_strjoin(i+1, new, ".", __FUNCTION__)) == NULL) {
		clicon_err(OE_UNIX, errno, "clicon_strjoin");
		goto catch;
	    }
	    if (tmp[0] == '^')
		idx = db_lv_vec_find(dbspec, dbname, tmp+1, cvec, &match);
	    else
		idx = db_lv_vec_find(dbspec, dbname, tmp, cvec, &match);
	    if (idx < 0)
		goto catch;
	    unchunk(tmp);
	    if (match < 0) { /* Parent does not exist, create based on db-spec */
		if ((tmp = clicon_strjoin(i+1, new, ".", __FUNCTION__)) == NULL ||
		    (tmp = chunk_sprintf(__FUNCTION__, "%s.%u", tmp, idx)) == NULL) {
		    clicon_err(OE_UNIX, errno, "chunk");
		    goto catch;
		}
		if (db_lv_spec2dbvec(dbspec, dbname, tmp[0]=='^'?tmp+1:tmp, idx, cvec) < 0) 
		    goto catch;
		unchunk(tmp);
	    }
	    
	    if ((tmp = chunk_sprintf(__FUNCTION__, "%s.%u", str, idx)) ==NULL){
		clicon_err(OE_UNIX, errno, "chunk");
		goto catch;
	    }
	    str = tmp;
	}

	new[i] = str;	    
    }

    if ((key = clicon_strjoin (nvec, new, ".", label)) == NULL) {
	clicon_err(OE_UNIX, errno, "clicon_strjoin");
	goto catch;
    }

catch:
    if (cv)
	cv_free(cv);
    unchunk_group(__FUNCTION__);
    return key;
}

/*
 * db_lv_op_exec
 * Perform actual operation of db value
 */
int
db_lv_op_exec(dbspec_key *dbspec, 
	      char *dbname, 
	      char *basekey, 
	      lv_op_t op, 
	      cvec *vh)
{
    dbspec_key *spec;
    int             len;
    int             retval = -1;

    /* Set the value, must find matching subkey if vector */
    if (key_isvector(basekey)){
	len = strlen(basekey);
	basekey[len-2] = '\0';
	switch (op){
	case LV_MERGE:
	case LV_SET:
	    if (db_lv_vec_set(dbspec, dbname, basekey, vh, op) < 0)
		goto quit;
	    break;
	case LV_DELETE:
	    if (db_lv_vec_del(dbspec, dbname, basekey, vh) < 0)
		goto quit;
	    break;
	}
    }
    else{
	switch (op){
	case LV_MERGE:
	case LV_SET:
	    if ((spec = key2spec_key(dbspec, basekey)) == NULL)
		goto quit; /* shouldnt happen after sanity */
	    if (db_lv_set(spec, dbname, basekey, vh, op) < 0)
		goto quit;
	    break;
	case LV_DELETE:
	    if (db_lv_del(dbname, basekey) < 0)
		goto quit;
	    break;
	}
    }
    retval = 0;
  quit:
    return retval;
}


/*
 * db_lv_op
 * Perform operation on db value.
 * Input args:
 *   dbspec: database specification
 *   dbname: name of database
 *   op:     LV_DELETE, LV_SET, LV_MERGE.
 *   fmt:    lvec format string (see lvmaps.txt)
 *   vr:     vector of cligen variables (optional: can be NULL)
 */
int 
db_lv_op(dbspec_key *dbspec, 
	 char *dbname, 
	 lv_op_t op, 
	 char *fmt, 
	 cvec *vr)
{
    int retval = -1;
    cvec *cvec;
    clicon_dbvars_t *dbv = NULL;

    /* Here vr is optional, but in db_lv_op_lvec(), vr is mandatory: create if NULL */
    if (vr == NULL){
	if ((cvec = cvec_new(0)) == NULL){
	    clicon_err(OE_UNIX, errno, "cvec_new");
	    goto quit;
	}
    }
    else
	cvec = vr;

    dbv = clicon_dbvars_parse(dbspec, dbname, cvec,  fmt, NULL,  NULL);
    if (dbv == NULL)
	goto quit;
    if (vr == NULL)
	cvec_free(cvec);
    if (db_lv_op_exec(dbspec, dbname, dbv->dbv_key, op, dbv->dbv_vec) < 0)
	goto quit;
    retval = 0;
  quit:
    unchunk_group(__FUNCTION__);
    if (dbv)
	clicon_dbvars_free(dbv);
    return retval;
}

/*
 * db_lv_string
 * Get a value from database given key, 
 * Get length of the string (which we will create)
 * Translate value (lvec) according to fmt by translating
 * from database value (lvec) to cligen variables (cgv). 
 * then return malloced string with value of cgv.
 *
 * Note: the returned string must be freed.
 */
#if 0
char *
db_lv_string(char *dbname, char *key, char *fmt)
{
    char *lvec;
    size_t lvec_len;
    char *str = NULL;
    struct var_head *vhead = NULL;
    
    /* Get value from database */
    if (db_get_alloc(dbname, 
                     key, 
                     (void**)&lvec, 
                     &lvec_len) == 0 && 
        lvec_len){
	if ((vhead = lvec2vh (lvec, lvec_len)))  {

	    /* Get formatted string */
	    str = lvmap_var_fmt (vhead, fmt);
	    var_free (vhead);
	}
	free(lvec);
      
	if (0)
	    fprintf(stderr, "%s: %s = %s\n", __FUNCTION__, key, str);
    }
    return str;
}
#else
char *
db_lv_string(char *dbname, char *key, char *fmt)
{
    return lvmap_fmt (dbname, fmt, key);
}
#endif


/*
 * cgv_fmt_string XXX: name does not properly describe function (cvec2str()?)
 * Return a malloced string using print-like techniques using a format string 
 * and arguments, but expanding printf using the following % keywords:
 * n:	 Number 
 * s:	 String 
 * i:	 Interface 
 * a:	 IPv4 address 
 * p:	 IPv4 Prefix 
 * m:	 MAC address 
 * u:	 URL 
 * U:    uuid
 * r:	 Rest
 * e:    Explicit 
 * XXX. IPV6
 */
char *
cgv_fmt_string(cvec *vr, char *fmt)
{
    int     len;
    int     argi;
    char   *ptr;
    char   *cmd = NULL;
    cg_var *cv;

    /* 
     * First calculate length of required string 
     */
    len = 0;
    ptr = fmt;
    argi = 1;

    while (*ptr) {
    
	if (*ptr == '%' && *(++ptr)) {

	    switch (*ptr) {

	    case 'n':	/* Number */
		cv = cvec_i(vr, argi++);
		len += snprintf (NULL, 0, "%d", cv_int_get(cv));
		break;
	
	    case 's':	/* String */
		cv = cvec_i(vr, argi++);
		len += snprintf (NULL, 0, "%s", cv_string_get(cv));
		break;
	
	    case 'i':	/* Interface */
		cv = cvec_i(vr, argi++);
		len += snprintf (NULL, 0, "%s", cv_string_get(cv));
		break;
	
	    case 'a':	/* IPv4 address */
		cv = cvec_i(vr, argi++);
		len += snprintf (NULL, 0, "%s", inet_ntoa (*cv_ipv4addr_get(cv)));
		break;
			
	    case 'p':	/* IPv4 Prefix */
		cv = cvec_i(vr, argi++);
		len += snprintf (NULL, 0, "%s/%u", 
				 inet_ntoa (*cv_ipv4addr_get(cv)),
				 cv_ipv4masklen_get(cv));
		break;
	    case 'A':
	    case 'P':	/* XXX: IPv6 prefix not yet supported by clicon? */
		break;

	    case 'm':{	/* MAC address */
		uint8_t *mac;
		cv = cvec_i(vr, argi++);
		mac = (uint8_t*)cv_mac_get(cv);
		len += 	snprintf (NULL, 0, "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x", 
				  mac[0],
				  mac[1],
				  mac[2],
				  mac[3],
				  mac[4],
				  mac[5]);
		break;
	    }
	    case 'u':	/* URL */
		cv = cvec_i(vr, argi++);
		len += snprintf (NULL, 0, "%s://%s/%s", 	/* FIXME: User & password */
				 cv_urlproto_get(cv),
				 cv_urladdr_get(cv),
				 cv_urlpath_get(cv));
		break;
	    case 'U':	/* UUID */
		len += 37;
		break;
	    case 'r':	/* Rest */
		cv = cvec_i(vr, argi++);
		len += snprintf (NULL, 0, "%s", cv_string_get(cv));
		break;

	    }
	    ptr++;

	} else {
	    len++;
	    ptr++;
	}
    }
    len++; /* Space for trailing \0 */

    /* Allocate string */
    cmd = (char *)malloc ((len) * sizeof (char));
    if (!cmd) {
	clicon_err(OE_UNIX, errno, "malloc");
	return 0;
    }
    memset (cmd, '\0', len);
  
    /* 
     * Now do it again putting the command together
     */
    ptr = fmt;
    argi = 1;
    while (*ptr) {
    
	char *s = cmd + strlen (cmd);

	if (*ptr == '%' && *(++ptr)) {

	    int r = len - strlen (cmd);

	    switch (*ptr) {

	    case 'n':	/* Number */
		cv = cvec_i(vr, argi++);
		snprintf (s, r, "%d", cv_int_get(cv));
		break;
	
	    case 's':	/* String */
		cv = cvec_i(vr, argi++);
		snprintf (s, r, "%s", cv_string_get(cv));
		break;
	
	    case 'i':	/* Interface */
		cv = cvec_i(vr, argi++);
		snprintf (s, r, "%s", cv_string_get(cv));
		break;
	
	    case 'a':	/* IPv4 address */
		cv = cvec_i(vr, argi++);
		snprintf (s, r, "%s", inet_ntoa (*cv_ipv4addr_get(cv)));
		break;
			
	    case 'p':	/* IPv4 Prefix */
		cv = cvec_i(vr, argi++);
		snprintf (s, r, "%s/%u", 
			  inet_ntoa (*cv_ipv4addr_get(cv)), cv_ipv4masklen_get(cv));
		break;

	    case 'm':{	/* MAC address */
		uint8_t *mac;
		cv = cvec_i(vr, argi++);
		mac = (uint8_t*)cv_mac_get(cv);
		snprintf (s, r, "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x", 
			  mac[0],
			  mac[1],
			  mac[2],
			  mac[3],
			  mac[4],
			  mac[5]);
		break;
	    }
	    case 'u':	/* URL */
		cv = cvec_i(vr, argi++);
		snprintf (s, r, "%s://%s/%s", 	/* FIXME: User & password */
			  cv_urlproto_get(cv),
			  cv_urladdr_get(cv),
			  cv_urlpath_get(cv));
		break;
	    case 'U':{ /* uuid */
		char uuidstr[37];
		cv = cvec_i(vr, argi++);
		uuid2str(cv_uuid_get(cv), uuidstr, sizeof(uuidstr));
		snprintf (s, r, "%s", uuidstr);
		break;
	    }
	    case 'r':	/* Rest */
		cv = cvec_i(vr, argi++);
		snprintf (s, r, "%s", cv_string_get(cv));
		break;

	    case 'A':
	    case 'P':	/* IPv6 prefix not yet supported by cli_gen */
		/* pass through */

	    default:
		*s++ = '%';
		*s = *ptr;
		break;

	    }
	    ptr++;

	} else {
	    *s = *ptr;
	    ptr++;
	}
    }


    return cmd;
}

