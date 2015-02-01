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

 * Yang functions
  */

#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
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
#include "clicon_dbspec_key.h"
#include "clicon_yang.h"
#include "clicon_hash.h"
#include "clicon_lvalue.h"
#include "clicon_lvmap.h"
#include "clicon_chunk.h"
#include "clicon_options.h"
#include "clicon_dbutil.h"
#include "clicon_yang.h"
#include "clicon_yang_type.h"

/* 
 * Local types and variables
 */
/* Struct used to map between int and strings. Used  for:
 * - mapping yang types/typedefs (strings) and cligen types (ints). 
 * - mapping yang keywords (strings) and enum (clicon)
 * (same struct in clicon_yang.c)
 */
struct map_str2int{
    char         *ms_str; /* string as in 4.2.4 in RFC 6020 */
    int           ms_int;
};

/* Mapping between yang types <--> cligen types
   Note, first match used wne translating from cv to yang --> order is significant */
static const struct map_str2int ytmap[] = {
    {"int32",       CGV_INT32},  /* NOTE, first match on right is significant, dont move */
    {"string",      CGV_STRING}, /* NOTE, first match on right is significant, dont move */
    {"string",      CGV_REST},   /* For cv -> yang translation of rest */
    {"binary",      CGV_STRING},    
    {"bits",        CGV_STRING},    
    {"boolean",     CGV_BOOL},
    {"decimal64",   CGV_DEC64},  
    {"empty",       CGV_VOID},  /* May not include any content */
    {"enumeration", CGV_STRING}, 
    {"identityref", CGV_STRING},  /* XXX */
    {"instance-identifier", CGV_STRING}, /* XXX */
    {"int8",        CGV_INT8},  
    {"int16",       CGV_INT16},  
    {"int64",       CGV_INT64},
    {"leafref",     CGV_STRING},  /* XXX */

    {"uint8",       CGV_UINT8}, 
    {"uint16",      CGV_UINT16},
    {"uint32",      CGV_UINT32},
    {"uint64",      CGV_UINT64},
    {"union",       CGV_VOID},  /* Is replaced by actual type */
    {NULL, -1}
};

/* return 1 if built-in, 0 if not */
static int
yang_builtin(char *type)
{
    const struct map_str2int *yt;

    /* built-in types */
    for (yt = &ytmap[0]; yt->ms_str; yt++)
	if (strcmp(yt->ms_str, type) == 0)
	    return 1;
    return 0;
}

/*! Translate from a yang type to a cligen variable type
 *
 * Currently many built-in types from RFC6020 and some RFC6991 types.
 * But not all, neither built-in nor 6991.
 * Also, there is no support for derived types, eg yang typedefs.
 * See 4.2.4 in RFC6020
 * Return 0 if no match but set cv_type to CGV_ERR
 */
int
yang2cv_type(char *ytype, enum cv_type *cv_type)
{
    const struct map_str2int *yt;

    *cv_type = CGV_ERR;
    /* built-in types */
    for (yt = &ytmap[0]; yt->ms_str; yt++)
	if (strcmp(yt->ms_str, ytype) == 0){
	    *cv_type = yt->ms_int;
	    return 0;
	}
    /* special derived types */
    if (strcmp("ipv4-address", ytype) == 0){ /* RFC6991 */
	*cv_type = CGV_IPV4ADDR;
	return 0;
    }
    if (strcmp("ipv6-address", ytype) == 0){ /* RFC6991 */
	*cv_type = CGV_IPV6ADDR;
	return 0;
    }
    if (strcmp("ipv4-prefix", ytype) == 0){ /* RFC6991 */
	*cv_type = CGV_IPV4PFX;
	return 0;
    }
    if (strcmp("ipv6-prefix", ytype) == 0){ /* RFC6991 */
	*cv_type = CGV_IPV6PFX;
	return 0;
    }
    if (strcmp("date-and-time", ytype) == 0){ /* RFC6991 */
	*cv_type = CGV_TIME;
	return 0;
    }
    if (strcmp("mac-address", ytype) == 0){ /* RFC6991 */
	*cv_type = CGV_MACADDR;
	return 0;
    }
    if (strcmp("uuid", ytype) == 0){ /* RFC6991 */
	*cv_type = CGV_UUID;
	return 0;
    }
    return 0;
}

/*! Translate from a cligen variable type to a yang type
 */
char *
cv2yang_type(enum cv_type cv_type)
{
    const struct map_str2int  *yt;
    char                *ytype;

    ytype = "empty";
    /* built-in types */
    for (yt = &ytmap[0]; yt->ms_str; yt++)
	if (yt->ms_int == cv_type)
	    return yt->ms_str;

    /* special derived types */
    if (cv_type == CGV_IPV4ADDR) /* RFC6991 */
	return "ipv4-address";

    if (cv_type == CGV_IPV6ADDR) /* RFC6991 */
	return "ipv6-address";

    if (cv_type == CGV_IPV4PFX) /* RFC6991 */
	return "ipv4-prefix";

    if (cv_type == CGV_IPV6PFX) /* RFC6991 */
	return "ipv6-prefix";

    if (cv_type == CGV_TIME) /* RFC6991 */
	return "date-and-time";

    if (cv_type == CGV_MACADDR) /* RFC6991 */
	return "mac-address";

    if (cv_type == CGV_UUID) /* RFC6991 */
	return "uuid";

    return ytype;
}

/*! Translate from yang type -> cligen type, after yang resolve has been made.
 * handle case where yang resolve did not succedd (rtype=NULL) and then try
 * to find special cligen types such as ipv4addr.
 * not true yang types
 * @param[in]  origtype
 * @param[in]  restype
 * @param[out] cvtype
 */
int
clicon_type2cv(char *origtype, char *restype, enum cv_type *cvtype)
{
    int retval = -1;

    *cvtype = CGV_ERR;
    if (restype != NULL){ 
	yang2cv_type(restype, cvtype);
	if (*cvtype == CGV_ERR){
	    clicon_err(OE_DB, 0, "%s: \"%s\" type not translated", __FUNCTION__, restype);
	    goto done;
	}
    }
    else{
	/* 
	 * Not resolved, but we can use special cligen types, eg ipv4addr 
	 * Note this is a kludge or at least if we intend of using rfc types
	 */
	yang2cv_type(origtype, cvtype);
	if (*cvtype == CGV_ERR){
	    clicon_err(OE_DB, 0, "%s: \"%s\": type not resolved", __FUNCTION__, origtype);
	    goto done;
	}
    }
    retval = 0;
  done:
    return retval;
}

/* cf cligen/cligen_var.c */
#define range_check(i, rmin, rmax, type)       \
    ((rmin && (i) < cv_##type##_get(rmin)) ||  \
     (rmax && (i) > cv_##type##_get(rmax)))


/*! Validate cligen variable cv using yang statement as spec
 *
 * @param [in]  cv      A cligen variable to validate. This is a correctly parsed cv.
 * @param [in]  ys      A yang statement, must be leaf of leaf-list.
 * @param [out] reason  If given, and if return value is 0, contains a malloced string
 *                      describing the reason why the validation failed. Must be freed.
 * @retval -1  Error (fatal), with errno set to indicate error
 * @retval 0   Validation not OK, malloced reason is returned. Free reason with free()
 * @retval 1   Validation OK
 * See also cv_validate - the code is similar.
 */
int
ys_cv_validate(cg_var *cv, yang_stmt *ys, char **reason)
{
    int             retval = 1; /* OK */
    cg_var         *ycv;        /* cv of yang-statement */  
    int64_t         i = 0;
    uint64_t        u = 0;
    char           *str;
    int             options;
    cg_var         *range_min; 
    cg_var         *range_max; 
    char           *pattern;
    int             retval2;
    enum cv_type    cvtype;
    char           *type;  /* orig type */
    yang_stmt      *yrestype; /* resolved type */
    char           *restype;
    uint8_t         fraction; 
    yang_stmt      *yi = NULL;

    if (ys->ys_keyword != Y_LEAF && ys->ys_keyword != Y_LEAF_LIST)
	return 0;
    ycv = ys->ys_cv;
    if (yang_type_get(ys, &type, &yrestype, 
		      &options, &range_min, &range_max, &pattern,
		      &fraction) < 0)
	goto err;
    restype = yrestype?yrestype->ys_argument:NULL;
    if (clicon_type2cv(type, restype, &cvtype) < 0)
	goto err;

    if (cv_type_get(ycv) != cvtype){
	/* special case: dbkey has rest syntax-> cv but yang cant have that */
	if (cvtype == CGV_STRING && cv_type_get(ycv) == CGV_REST)
	    ;
	else {
	    clicon_err(OE_DB, 0, "%s: Type mismatch data:%s != yang:%s", 
		       __FUNCTION__, cv_type2str(cvtype), cv_type2str(cv_type_get(ycv)));
	    goto err;
	}
    }
    switch (cvtype){
    case CGV_INT8:
	if ((options & YANG_OPTIONS_RANGE) != 0){
	    i = cv_int8_get(cv);
	    if (range_check(i, range_min, range_max, int8)){
		if (reason)
		    *reason = cligen_reason("Number out of range: %ld", i);
		retval = 0;
		break;
	    }
	}
	break;
    case CGV_INT16:
	if ((options & YANG_OPTIONS_RANGE) != 0){
	    i = cv_int16_get(cv);
	    if (range_check(i, range_min, range_max, int16)){
		if (reason)
		    *reason = cligen_reason("Number out of range: %ld", i);
		retval = 0;
		break;
	    }
	}
	break;
    case CGV_INT32:
	if ((options & YANG_OPTIONS_RANGE) != 0){
	    i = cv_int32_get(cv);
	    if (range_check(i, range_min, range_max, int32)){
		if (reason)
		    *reason = cligen_reason("Number out of range: %ld", i);
		retval = 0;
		break;
	    }
	}
	break;
    case CGV_INT64:
	if ((options & YANG_OPTIONS_RANGE) != 0){
	    i = cv_int64_get(cv);
	    if (range_check(i, range_min, range_max, int64)){
		if (reason)
		    *reason = cligen_reason("Number out of range: %ld", i);
		retval = 0;
		break;
	    }
	}
	break;
    case CGV_UINT8:
	if ((options & YANG_OPTIONS_RANGE) != 0){
	    u = cv_uint8_get(cv);
	    if (range_check(u, range_min, range_max, uint8)){
		if (reason)
		    *reason = cligen_reason("Number out of range: %lu", u);
		retval = 0;
		break;
	    }
	}
	break;
    case CGV_UINT16:
	if ((options & YANG_OPTIONS_RANGE) != 0){
	    u = cv_uint16_get(cv);
	    if (range_check(u, range_min, range_max, uint16)){
		if (reason)
		    *reason = cligen_reason("Number out of range: %lu", u);
		retval = 0;
		break;
	    }
	}
	break;
    case CGV_UINT32:
	if ((options & YANG_OPTIONS_RANGE) != 0){
	    u = cv_uint32_get(cv);
	    if (range_check(u, range_min, range_max, uint32)){
		if (reason)
		    *reason = cligen_reason("Number out of range: %lu", u);
		retval = 0;
		break;
	    }
	}
	break;
    case CGV_UINT64:
	if ((options & YANG_OPTIONS_RANGE) != 0){
	    u = cv_uint64_get(cv);
	    if (range_check(u, range_min, range_max, uint64)){
		if (reason)
		    *reason = cligen_reason("Number out of range: %lu", u);
		retval = 0;
		break;
	    }
	}
	break;
    case CGV_DEC64:
	if ((options & YANG_OPTIONS_RANGE) != 0){
	    i = cv_int64_get(cv);
	    if (range_check(i, range_min, range_max, int64)){
		if (reason)
		    *reason = cligen_reason("Number out of range: %ld", i);
		retval = 0;
		break;
	    }
	}
	break;
    case CGV_STRING:
    case CGV_REST:
	str = cv_string_get(cv);
	if (restype && 
	    (strcmp(restype, "enumeration") == 0 || strcmp(restype, "bits") == 0)){
	    int found = 0;
	    while ((yi = yn_each((yang_node*)yrestype, yi)) != NULL){
		if (yi->ys_keyword != Y_ENUM && yi->ys_keyword != Y_BIT)
		    continue;
		if (strcmp(yi->ys_argument, str) == 0){
		    found++;
		    break;
		}
	    }
	    if (!found){
		if (reason)
		    *reason = cligen_reason("'%s' does not match enumeration", str);
		retval = 0;
		break;
	    }
	}
	if ((options & YANG_OPTIONS_LENGTH) != 0){
	    u = strlen(str);
	    if (range_check(u, range_min, range_max, uint64)){
		if (reason)
		    *reason = cligen_reason("string length out of range: %lu", u);
		retval = 0;
		break;
	    }
	}
	if ((options & YANG_OPTIONS_PATTERN) != 0){
	    if ((retval2 = match_regexp(str, pattern)) < 0){
		clicon_err(OE_DB, 0, "match_regexp: %s", pattern);
		return -1;
	    }
	    if (retval2 == 0){
		if (reason)
		    *reason = cligen_reason("regexp match fail: \"%s\" does not match %s",
					    str, pattern);
		retval = 0;
		break;
	    }
	}
	break;
    case CGV_ERR:
    case CGV_VOID:
	retval = 0;
	if (reason)
	    *reason = cligen_reason("Invalid cv");
	retval = 0;
	break;
    case CGV_BOOL:
    case CGV_INTERFACE:
    case CGV_IPV4ADDR: 
    case CGV_IPV6ADDR: 
    case CGV_IPV4PFX: 
    case CGV_IPV6PFX: 
    case CGV_MACADDR:
    case CGV_URL: 
    case CGV_UUID: 
    case CGV_TIME: 
    case CGV_EMPTY:  /* XXX */
	break;
    }

    if (reason && *reason)
	assert(retval == 0);
    return retval;
  err:
    return -1;
}

/*
 * a typedef can be under module, submodule, container, list, grouping, rpc, 
 * input, output, notification
 */
static inline int
ys_typedef(yang_stmt *ys)
{
    return ys->ys_keyword == Y_MODULE || ys->ys_keyword == Y_SUBMODULE ||
	ys->ys_keyword == Y_CONTAINER || ys->ys_keyword == Y_LIST;
}

/* find next ys up which can contain a typedef */
static yang_stmt *
ys_up(yang_stmt *ys)
{
    yang_node *yn;

    while (ys != NULL && !ys_typedef(ys)){
	yn = ys->ys_parent;
	/* Some extra stuff to ensure ys is a stmt */
	if (yn && yn->yn_keyword == Y_SPEC)
	    yn = NULL;
	ys = (yang_stmt*)yn;
    }
    /* Here it is either NULL or is a typedef-kind yang-stmt */
    return (yang_stmt*)ys;
}

/* find top of tree: module or sub-module */
static yang_stmt *
ys_top(yang_stmt *ys)
{
    yang_node *yn;

    while (ys != NULL && ys->ys_keyword != Y_MODULE && ys->ys_keyword != Y_SUBMODULE){
	yn = ys->ys_parent;
	/* Some extra stuff to ensure ys is a stmt */
	if (yn && yn->yn_keyword == Y_SPEC)
	    yn = NULL;
	ys = (yang_stmt*)yn;
    }
    /* Here it is either NULL or is a typedef-kind yang-stmt */
    return (yang_stmt*)ys;
}

/* find top of tree: specification */
static yang_spec *
ys_spec(yang_stmt *ys)
{
    yang_node *yn;

    while (ys != NULL && ys->ys_keyword != Y_SPEC){
	yn = ys->ys_parent;
	ys = (yang_stmt*)yn;
    }
    /* Here it is either NULL or is a typedef-kind yang-stmt */
    return (yang_spec*)ys;
}

static yang_stmt *
ys_prefix2import(yang_stmt *ys, char *prefix)
{
    yang_stmt *ytop;
    yang_stmt *yimport = NULL;
    yang_stmt *yprefix;

    ytop      = ys_top(ys);
    while ((yimport = yn_each((yang_node*)ytop, yimport)) != NULL) {
	if (yimport->ys_keyword != Y_IMPORT)
	    continue;
	if ((yprefix = yang_find((yang_node*)yimport, Y_PREFIX, NULL)) != NULL &&
	    strcmp(yprefix->ys_argument, prefix) == 0)
	    return yimport;
    }
    return NULL;
}

/*
 * Extract id from type argument. two cases:
 * argument is prefix:id, 
 * argument is id,        
 * Just return string from id
 */
char*
ytype_id(yang_stmt *ys)
{
    char   *id;
    
    if ((id = strchr(ys->ys_argument, ':')) == NULL)
	id = ys->ys_argument;
    else
	id++;
    return id;
}

/*
 * Extract prefix from type argument. two cases:
 * argument is prefix:id, 
 * argument is id,        
 * return either NULL or a new prefix string that needs to be freed by caller.
 */
static char*
ytype_prefix(yang_stmt *ys)
{
    char   *id;
    char   *prefix = NULL;
    
    if ((id = strchr(ys->ys_argument, ':')) != NULL){
	prefix = strdup(ys->ys_argument);
	prefix[id-ys->ys_argument] = '\0';
    }
    return prefix;
}


/*
 */
static int
resolve_restrictions(yang_stmt   *yrange,
		     yang_stmt   *ypattern,
		     yang_stmt   *yfraction,
		     int         *options, 
		     cg_var     **mincv, 
		     cg_var     **maxcv, 
		     char       **pattern,
		     uint8_t     *fraction)
{
    if (options && mincv && maxcv && yrange != NULL){
	*mincv = cvec_find(yrange->ys_cvec, "range_min");
	*maxcv = cvec_find(yrange->ys_cvec, "range_max");
	*options  |= YANG_OPTIONS_RANGE;
    }
    if (options && pattern && ypattern != NULL){
	*pattern   = ypattern->ys_argument;
	*options  |= YANG_OPTIONS_PATTERN;
    }
    if (options && fraction && yfraction != NULL){
	*fraction  = cv_uint8_get(yfraction->ys_cv);
	*options  |= YANG_OPTIONS_FRACTION_DIGITS;
    }
    return 0;
}

/*! Recursively resolve a yang type to built-in type with optional restrictions
 * @param [in]  ys       yang-stmt from where the current search is based
 * @param [in]  ytype    yang-stmt object containing currently resolving type
 * @param [out] yrestype    resolved type. return built-in type or NULL. mandatory
 * @param [out] options  pointer to flags field of optional values. optional
 * @param [out] mincv    pointer to cv woth min range or length. optional
 * @param [out] maxcv    pointer to cv with max range or length. optional
 * @param [out] pattern  pointer to static string of yang string pattern. optional
 * @param [out] fraction for decimal64, how many digits after period
 * @retval      0        OK. Note yrestype may still be NULL.
 * @retval     -1        Error, clicon_err handles errors
 * Note that the static output strings (type, pattern) should be copied if used asap.
 * Note also that for all pointer arguments, if NULL is given, no value is assigned.
 */
int 
yang_type_resolve(yang_stmt   *ys, 
		  yang_stmt   *ytype, 
		  yang_stmt  **yrestype, 
		  int         *options, 
		  cg_var     **mincv, 
		  cg_var     **maxcv, 
		  char       **pattern,
		  uint8_t     *fraction)
{
    yang_stmt   *rytypedef = NULL; /* Resolved typedef of ytype */
    yang_stmt   *rytype;           /* Resolved type of ytype */
    yang_stmt   *yrange;
    yang_stmt   *ypattern;
    yang_stmt   *yfraction;
    yang_stmt   *yimport;
    char        *type;
    char        *prefix = NULL;
    int          retval = -1;
    yang_node   *yn;
    yang_spec   *yspec;
    yang_stmt   *ymodule;

    if (options)
	*options = 0x0;
    *yrestype    = NULL; /* Initialization of resolved type that may not be necessary */
    type      = ytype_id(ytype);     /* This is the type to resolve */
    prefix    = ytype_prefix(ytype); /* And this its prefix */

    yrange    = yang_find((yang_node*)ytype, Y_RANGE, NULL);
    /* XXX BUG: gcv gets char* type should be uint64 or something
	yrange    = yang_find((yang_node*)ytype, Y_LENGTH, NULL); */
    ypattern  = yang_find((yang_node*)ytype, Y_PATTERN, NULL);
    yfraction = yang_find((yang_node*)ytype, Y_FRACTION_DIGITS, NULL);
    /* Check if type is basic type. If so, return that */
    if (prefix == NULL && yang_builtin(type)){
	*yrestype = ytype; /* XXX: This is the place */
	resolve_restrictions(yrange, ypattern, yfraction, options, 
			     mincv, maxcv, pattern, fraction);
	goto ok;
    }
    /* Not basic type. Now check if prefix which means we look in other module */
    if (prefix){ /* Go to top and find import that matches */
	if ((yimport = ys_prefix2import(ys, prefix)) == NULL){
	    clicon_err(OE_DB, 0, "Prefix %s not defined not found", prefix);
	    goto done;
	}
	yspec = ys_spec(ys);
	if ((ymodule = yang_find((yang_node*)yspec, Y_MODULE, yimport->ys_argument)) == NULL)
	    goto ok; /* unresolved */
	if ((rytypedef = yang_find((yang_node*)ymodule, Y_TYPEDEF, type)) == NULL)
	    goto ok; /* unresolved */
    }
    else
	while (1){
	    /* Check if ys may have typedefs otherwise find one that can */
	    if ((ys = ys_up(ys)) == NULL){ /* If reach top */
		*yrestype = NULL;
		break;
	    }
	    /* Here find typedef */
	    if ((rytypedef = yang_find((yang_node*)ys, Y_TYPEDEF, type)) != NULL)
		break;
	    /* Did not find a matching typedef there, proceed to next level */
	    yn = ys->ys_parent;
	    if (yn && yn->yn_keyword == Y_SPEC)
		yn = NULL;
	    ys = (yang_stmt*)yn;
	}
    if (rytypedef != NULL){     /* We have found a typedef */
	/* Find associated type statement */
	if ((rytype = yang_find((yang_node*)rytypedef, Y_TYPE, NULL)) == NULL){
	    clicon_err(OE_DB, 0, "%s: mandatory type object is not found", __FUNCTION__);
	    goto done;
	}
	/* recursively resolve this new type */
	if (yang_type_resolve(ys, rytype, yrestype, 
			      options, mincv, maxcv, pattern, fraction) < 0)
	    goto done;
	/* overwrites the resolved if any */
	resolve_restrictions(yrange, ypattern, yfraction, 
			     options, mincv, maxcv, pattern, fraction);
    }
  ok:
    retval = 0;
  done:
    if (prefix)
	free(prefix);
    return retval;
}

/*! Get type information about a leaf/leaf-list yang-statement
 *
 * @code
 *   yang_stmt    *yrestype;
 *   int           options;
 *   int64_t       min, max;
 *   char         *pattern;
 *   uint8_t       fraction;
 *
 *   if (yang_type_get(ys, &type, &yrestype, &options, &min, &max, &pattern, &fraction) < 0)
 *      goto err;
 *   if (yrestype == NULL) # unresolved
 *      goto err;
 *   if (options & YANG_OPTIONS_LENGTH != 0)
 *      printf("%d..%d\n", min , max);
 *   if (options & YANG_OPTIONS_PATTERN != 0)
 *      printf("regexp: %s\n", pattern);
 * @endcode
 * @param [in]  ys       yang-stmt, leaf or leaf-list
 * @param [out] origtype original type may be derived or built-in
 * @param [out] yrestype pointer to resolved type stmt. should be built-in or NULL
 * @param [out] options  pointer to flags field of optional values
 * @param [out] mincv    pointer to cv of min range or length. optional
 * @param [out] maxcv    pointer to cv of max range or length. optional
 * @param [out] pattern  pointer to static string of yang string pattern. optional
 * @param [out] fraction for decimal64, how many digits after period
 * @retval      0        OK, but note that restype==NULL means not resolved.
 * @retval     -1        Error, clicon_err handles errors
 * Note that the static output strings (type, pattern) should be copied if used asap.
 * Note also that for all pointer arguments, if NULL is given, no value is assigned.
 * See also yang_type_resolve(). This function is really just a frontend to that.
 */
int 
yang_type_get(yang_stmt    *ys, 
	      char        **origtype, 
	      yang_stmt   **yrestype, 
	      int          *options, 
	      cg_var      **mincv, 
	      cg_var      **maxcv, 
	      char        **pattern,
	      uint8_t      *fraction
    )
{
    int retval = -1;
    yang_stmt    *ytype;        /* type */
    char         *type;

    if (options)
	*options = 0x0;
    /* Find mandatory type */
    if ((ytype = yang_find((yang_node*)ys, Y_TYPE, NULL)) == NULL){
	clicon_err(OE_DB, 0, "%s: mandatory type object is not found", __FUNCTION__);
	goto done;
    }
    type = ytype_id(ytype);
    if (origtype)
	*origtype = type;
    if (yang_type_resolve(ys, ytype, yrestype, 
			  options, mincv, maxcv, pattern, fraction) < 0)
	goto done;
    clicon_debug(3, "%s: %s %s->%s", __FUNCTION__, ys->ys_argument, type, 
		 *yrestype?(*yrestype)->ys_argument:"null");
    retval = 0;
  done:
    return retval;
}
