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
#include "clicon_spec.h"
#ifdef USE_DBSPEC_PT
#include "clicon_dbspec_parsetree.h"
#endif /* USE_DBSPEC_PT */
#include "clicon_yang.h"
#include "clicon_hash.h"
#include "clicon_lvalue.h"
#include "clicon_lvmap.h"
#include "clicon_chunk.h"
#include "clicon_options.h"
#include "clicon_dbutil.h"
#ifdef USE_DBSPEC_PT
#include "clicon_dbspec.h"
#endif /* USE_DBSPEC_PT */
#include "clicon_yang.h"
#include "clicon_yang_parse.h"

/*
 * Private data types
 */
/* Struct used to map between int and strings. Used  for:
 * - mapping yang types/typedefs (strings) and cligen types (ints). 
 * - mapping yang keywords (strings) and enum (clicon)
 */
struct map_str2int{
    char         *ms_str; /* string as in 4.2.4 in RFC 6020 */
    int           ms_int;
};

/* Mapping between yang types <--> cligen types
   Note, first match used wne translating from cv to yang --> order is significant */
static const struct map_str2int ytmap[] = {
    {"int32",       CGV_INT},
    {"binary",      CGV_INT}, /* XXX not really int */
    {"bits",        CGV_INT}, /* XXX not really int */
    {"boolean",     CGV_BOOL},
    {"decimal64",   CGV_INT},  /* XXX not really int */
    {"empty",       CGV_INT},  /* XXX not really int */
    {"enumeration", CGV_INT},  /* XXX not really int */
    {"identityref", CGV_INT},  /* XXX not really int */
    {"instance-identifier", CGV_INT}, /* XXX not really int */
    {"int8",        CGV_INT},  /* XXX not really int */
    {"int16",       CGV_INT},  /* XXX not really int */
    {"int64",       CGV_LONG},
    {"leafref",     CGV_INT},  /* XXX not really int */
    {"string",      CGV_STRING},
    {"uint8",       CGV_INT},  /* XXX not really int */
    {"uint16",      CGV_INT},  /* XXX not really int */
    {"uint32",      CGV_INT},  /* XXX not really int */
    {"uint64",      CGV_LONG}, /* XXX not really int */
    {"union",       CGV_INT},  /* XXX not really int */
    {NULL, -1}
};

/* Mapping between yang keyword string <--> clicon constants */
static const struct map_str2int ykmap[] = {
    {"anyxml",           Y_ANYXML}, 
    {"argument",         Y_ARGUMENT}, 
    {"augment",          Y_AUGMENT}, 
    {"base",             Y_BASE}, 
    {"belongs-to",       Y_BELONGS_TO}, 
    {"bit",              Y_BIT}, 
    {"case",             Y_CASE}, 
    {"choice",           Y_CHOICE}, 
    {"config",           Y_CONFIG}, 
    {"contact",          Y_CONTACT}, 
    {"container",        Y_CONTAINER}, 
    {"default",          Y_DEFAULT}, 
    {"description",      Y_DESCRIPTION}, 
    {"deviate",          Y_DEVIATE}, 
    {"deviation",        Y_DEVIATION}, 
    {"enum",             Y_ENUM}, 
    {"error-app-tag",    Y_ERROR_APP_TAG}, 
    {"error_message",    Y_ERROR_MESSAGE}, 
    {"extension",        Y_EXTENSION}, 
    {"feature",          Y_FEATURE}, 
    {"fraction-digits",  Y_FRACTION_DIGITS}, 
    {"grouping",         Y_GROUPING}, 
    {"identity",         Y_IDENTITY}, 
    {"if-feature",       Y_IF_FEATURE}, 
    {"import",           Y_IMPORT}, 
    {"include",          Y_INCLUDE}, 
    {"input",            Y_INPUT}, 
    {"key",              Y_KEY}, 
    {"leaf",             Y_LEAF}, 
    {"leaf-list",        Y_LEAF_LIST}, 
    {"length",           Y_LENGTH}, 
    {"list",             Y_LIST}, 
    {"mandatory",        Y_MANDATORY}, 
    {"max-elements",     Y_MAX_ELEMENTS}, 
    {"min-elements",     Y_MIN_ELEMENTS}, 
    {"module",           Y_MODULE}, 
    {"must",             Y_MUST}, 
    {"namespace",        Y_NAMESPACE}, 
    {"notification",     Y_NOTIFICATION}, 
    {"ordered-by",       Y_ORDERED_BY}, 
    {"organization",     Y_ORGANIZATION}, 
    {"output",           Y_OUTPUT}, 
    {"path",             Y_PATH}, 
    {"pattern",          Y_PATTERN}, 
    {"position",         Y_POSITION}, 
    {"prefix",           Y_PREFIX}, 
    {"presence",         Y_PRESENCE}, 
    {"range",            Y_RANGE}, 
    {"reference",        Y_REFERENCE}, 
    {"refine",           Y_REFINE}, 
    {"require-instance", Y_REQUIRE_INSTANCE}, 
    {"revision",         Y_REVISION}, 
    {"revision-date",    Y_REVISION_DATE}, 
    {"rpc",              Y_RPC}, 
    {"status",           Y_STATUS}, 
    {"submodule",        Y_SUBMODULE}, 
    {"type",             Y_TYPE}, 
    {"typedef",          Y_TYPEDEF}, 
    {"unique",           Y_UNIQUE}, 
    {"units",            Y_UNITS}, 
    {"uses",             Y_USES}, 
    {"value",            Y_VALUE}, 
    {"when",             Y_WHEN}, 
    {"yang-version",     Y_YANG_VERSION}, 
    {"yin-element",      Y_YIN_ELEMENT}, 
    {NULL,               -1}
};

yang_spec *
yspec_new(void)
{
    yang_spec *yspec;

    if ((yspec = malloc(sizeof(*yspec))) == NULL){
	clicon_err(OE_DB, errno, "%s: malloc", __FUNCTION__);
	return NULL;
    }
    memset(yspec, 0, sizeof(*yspec));
    return yspec;
}

yang_stmt *
ys_new(enum rfc_6020 keyw)
{
    yang_stmt *ys;

    if ((ys = malloc(sizeof(*ys))) == NULL){
	clicon_err(OE_DB, errno, "%s: malloc", __FUNCTION__);
	return NULL;
    }
    memset(ys, 0, sizeof(*ys));
    ys->ys_keyword    = keyw;
    return ys;
}

/*! Free a single yang statement */
static int 
ys_free1(yang_stmt *ys)
{
    if (ys->ys_argument)
	free(ys->ys_argument);
    if (ys->ys_dbkey)
	free(ys->ys_dbkey);
    if (ys->ys_cv)
	cv_free(ys->ys_cv);
    free(ys);
    return 0;
}

/*! Free a tree of yang statements recursively */
int 
ys_free(yang_stmt *ys)
{
    int i;
    yang_stmt *yc;

    for (i=0; i<ys->ys_len; i++){
	if ((yc = ys->ys_stmt[i]) != NULL)
	    ys_free(yc);
    }
    if (ys->ys_stmt)
	free(ys->ys_stmt);
    ys_free1(ys);
    return 0;
}

/*! Free a yang specification recursively */
int 
yspec_free(yang_spec *yspec)
{
    int i;
    yang_stmt *ys;

    for (i=0; i<yspec->yp_len; i++){
	if ((ys = yspec->yp_stmt[i]) != NULL)
	    ys_free(ys);
    }
    if (yspec->yp_stmt)
	free(yspec->yp_stmt);
    free(yspec);
    return 0;
}

/*! Allocate larger yang statement vector */
static int 
yn_realloc(yang_node *yn)
{
    yn->yn_len++;

    if ((yn->yn_stmt = realloc(yn->yn_stmt, (yn->yn_len)*sizeof(yang_stmt *))) == 0){
	clicon_err(OE_DB, errno, "%s: realloc", __FUNCTION__);
	return -1;
    }
    yn->yn_stmt[yn->yn_len - 1] = NULL; /* init field */
    return 0;
}

/*! Insert yang statement as child of a parent yang_statement, last in list 
 *
 * Also add parent to child as up-pointer
 */
int
yn_insert(yang_node *yn_parent, yang_stmt *ys_child)
{
    int pos = yn_parent->yn_len;

    if (yn_realloc(yn_parent) < 0)
	return -1;
    yn_parent->yn_stmt[pos] = ys_child;
    ys_child->ys_parent = yn_parent;
    return 0;
}

/*! Iterate through all yang statements from a yang node 
 *
 * Note that this is not optimized, one could use 'i' as index?
 * @code
 *   yang_stmt *ys = NULL;
 *   while ((ys = yn_each(yn, ys)) != NULL) {
 *     ...ys...
 *   }
 * @endcode
 */
yang_stmt *
yn_each(yang_node *yn, yang_stmt *ys)
{
    yang_stmt *yc = NULL;
    int i;

    for (i=0; i<yn->yn_len; i++){
	yc = yn->yn_stmt[i];
	if (ys==NULL)
	    return yc;
	if (ys==yc)
	    ys = NULL;
    }
    return NULL;
}

/*! Find a child yang_stmt with matching keyword and argument
 *
 * If argument is NULL, match any argument.
 * If key is 0 match any keyword
 * This however means that if you actually want to match only a yang-stmt with 
 * argument==NULL you cannot, but I have not seen any such examples.
 */
yang_stmt *
yang_find(yang_node *yn, int keyword, char *argument)
{
    yang_stmt *ys = NULL;
    int i;
    int match = 0;

    for (i=0; i<yn->yn_len; i++){
	ys = yn->yn_stmt[i];
	if (keyword == 0 || ys->ys_keyword == keyword){
	    if (argument == NULL)
		match++;
	    else
		if (ys->ys_argument && strcmp(argument, ys->ys_argument) == 0)
		    match++;
	    if (match)
		break;
	    }

    }
    return match ? ys : NULL;
}

/*! Find a child spec-node yang_stmt with matching argument (container, leaf, etc)
 *
 * See also yang_find() but this looks only for the yang specification nodes with
 * the following keyword: container, leaf, list, leaf-list
 * That is, basic syntax nodes.
 */
yang_stmt *
yang_find_specnode(yang_node *yn, char *argument)
{
    yang_stmt *ys = NULL;
    int i;
    int match = 0;

    for (i=0; i<yn->yn_len; i++){
	ys = yn->yn_stmt[i];
	if (ys->ys_keyword == Y_CONTAINER || ys->ys_keyword == Y_LEAF || 
	    ys->ys_keyword == Y_LIST || ys->ys_keyword == Y_LEAF_LIST){
	    if (argument == NULL)
		match++;
	    else
		if (ys->ys_argument && strcmp(argument, ys->ys_argument) == 0)
		    match++;
	    if (match)
		break;
	    }
    }
    return match ? ys : NULL;
}

/* RFC 6020 keywords mapping.
   linear search,...
 */
char *
yang_key2str(int keyword)
{
    const struct map_str2int *yk;

    for (yk = &ykmap[0]; yk->ms_str; yk++)
	if (yk->ms_int == keyword)
	    return yk->ms_str;
    return NULL;
}

/*! string is quoted if it contains space or tab, needs double '' */
static int inline
quotedstring(char *s)
{
    int len = strlen(s);
    int i;

    for (i=0; i<len; i++)
	if (isblank(s[i]))
	    break;
    return i < len;
}

int
yang_print(FILE *f, yang_node *yn, int marginal)
{
    yang_stmt *ys = NULL;

    while ((ys = yn_each(yn, ys)) != NULL) {
	fprintf(f, "%*s%s", marginal, "", yang_key2str(ys->ys_keyword));
	if (ys->ys_argument){
	    if (quotedstring(ys->ys_argument))
		fprintf(f, " \"%s\"", ys->ys_argument);
	    else
		fprintf(f, " %s", ys->ys_argument);
	}
	if (ys->ys_len){
	    fprintf(f, " {\n");
	    yang_print(f, (yang_node*)ys, marginal+3);
	    fprintf(f, "%*s%s\n", marginal, "", "}");
	}
	else
	    fprintf(f, ";\n");
    }
    return 0;
}

/*! Populate yang leafs after first round of parsing. Create a cv and fill it in.
 *
 * A leaf should consist of a cligen variable (cv) for type information and
 * mandatory status, default value, and key in a list.
 */
static int
ys_populate_leaf(yang_stmt *ys, void *arg)
{
    int             retval = -1;
    cg_var         *cv = NULL;
    yang_node      *yparent; 
    yang_stmt      *ytype; 
    yang_stmt      *ydef; 
    yang_stmt      *ykey = NULL; 
    enum cv_type    cvtype = CGV_ERR;
    int             cvret;
    char           *reason = NULL;

    /* 1. Find parent and matching 'key'-statemnt */
    if ((yparent = ys->ys_parent) != NULL && yparent->yn_keyword == Y_LIST)
	ykey = yang_find(yparent, Y_KEY, ys->ys_argument);

    /* 2. Find type specification and set cv type accordingly */
    if ((ytype = yang_find((yang_node*)ys, Y_TYPE, NULL)) != NULL){
	if (yang2cv_type(ytype->ys_argument, &cvtype) < 0)
	    goto done;
    }
    if (cvtype == CGV_ERR){
	clicon_err(OE_DB, 0, "%s: No mapping to cv-type from yang type: %s\n", 
		   __FUNCTION__, ytype->ys_argument);
	goto done;
    }
	
    /* 3. Check if default value. Here we parse the cv in the default-stmt
          Or should we parse this into the key-stmt itself?
     */
    if ((cv = cv_new(cvtype)) == NULL){
	clicon_err(OE_DB, errno, "%s: cv_new", __FUNCTION__); 
	goto done;
    }
    if ((ydef = yang_find((yang_node*)ys, Y_DEFAULT, NULL)) != NULL){
	if ((cvret = cv_parse1(ydef->ys_argument, cv, &reason)) < 0){ /* error */
	    clicon_err(OE_DB, errno, "parsing cv");
	    goto done;
	}
	if (cvret == 0){ /* parsing failed */
	    clicon_err(OE_DB, errno, "Parsing CV: %s", reason);
	    free(reason);
	    goto done;
	}
    }
    else{
	/* 3b. If not default value, just create a var. */
	cv_flag_set(cv, V_UNSET); /* no value (no default) */
    }
    if (cv_name_set(cv, ys->ys_argument) == NULL){
	clicon_err(OE_DB, errno, "%s: cv_new_set", __FUNCTION__); 
	goto done;
    }

    /* 4. Check if leaf is part of list and this is the key */
    if (ykey)
	cv_flag_set(cv, V_UNIQUE);

#ifdef moved_to_ys_parse_sub
    /* 5. Check if mandatory */
    if ((yman = yang_find((yang_node*)ys, Y_MANDATORY, NULL)) != NULL)
	ys->ys_mandatory = cv_bool_get(yman->ys_cv);
#endif

    ys->ys_cv = cv;
    retval = 0;
  done:
    if (cv && retval < 0)
	cv_free(cv);
    return retval;
}

/*! Populate with cligen-variables, default values etc
 *
 * We do this in 2nd pass after complete parsing to be sure to have a complete parse-tree
 */
static int
ys_populate(yang_stmt *ys, void *arg)
{
    int retval = -1;

    if (ys->ys_keyword == Y_LEAF || ys->ys_keyword == Y_LEAF_LIST){
	if (ys_populate_leaf(ys, arg) < 0)
	    goto done;
    }
    retval = 0;
  done:
    return retval;
}


/*! Parse a string containing a YANG spec into a parse-tree
 * 
 * Syntax parsing. A string is input and a syntax-tree is returned (or error). 
 * A variable record is also returned containing a list of (global) variable values.
 * (cloned from cligen)
 */
static int
yang_parse_str(clicon_handle h,
	       char *str,
	       const char *name, /* just for errs */
	       yang_spec *yspec
    )
{
    int                retval = -1;
    struct clicon_yang_yacc_arg yy = {0,};

    yy.yy_handle       = h; 
    yy.yy_name         = (char*)name;
    yy.yy_linenum      = 1;
    yy.yy_parse_string = str;
    yy.yy_stack        = NULL;

    if (ystack_push(&yy, (yang_node*)yspec) == NULL)
	goto done;
    if (strlen(str)){ /* Not empty */
	if (yang_scan_init(&yy) < 0)
	    goto done;
	if (yang_parse_init(&yy, yspec) < 0)
	    goto done;
	if (clicon_yang_parseparse(&yy) != 0) {
	    yang_parse_exit(&yy);
	    yang_scan_exit(&yy);
	    goto done;
	}
	if (yang_parse_exit(&yy) < 0)
	    goto done;		
	if (yang_scan_exit(&yy) < 0)
	    goto done;		
	/* Go through parse tree and populate it with cv types */
	if (yang_apply((yang_node*)yspec, ys_populate, NULL) < 0)
	    goto done;
    }
    retval = 0;
  done:
    ystack_pop(&yy);

    return retval;

}



/*! Parse a file containing a YANG into a parse-tree
 *
 * Similar to clicon_yang_str(), just read a file first
 * (cloned from cligen)
 * The database symbols are inserted in alphabetical order.
 */
static int
yang_parse_file(clicon_handle h,
		FILE *f,
		const char *name, /* just for errs */
		yang_spec *ysp
    )
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
    if (yang_parse_str(h, buf, name, ysp) < 0)
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
yang_parse(clicon_handle h, const char *filename, yang_spec *ysp)
{
    FILE       *f;
    int         retval = -1;
    yang_stmt  *ys;

    if ((f = fopen(filename, "r")) == NULL){
	clicon_err(OE_UNIX, errno, "fopen(%s)", filename);	
	goto done;
    }
    if (yang_parse_file(h, f, filename, ysp) < 0)
	goto done;
    /* pick up name="myname"; from spec */
    if ((ys = yang_find((yang_node*)ysp, Y_MODULE, NULL)) == NULL){
	clicon_err(OE_DB, 0, "No module found in %s", filename);	
	goto done;
    }
    clicon_dbspec_name_set(h, ys->ys_argument);

    retval = 0;
  done:
    if (f)
	fclose(f);
    return retval;
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
	return "ipv4_address";

    if (cv_type == CGV_IPV6ADDR) /* RFC6991 */
	return "ipv6_address";

    if (cv_type == CGV_IPV4PFX) /* RFC6991 */
	return "ipv4_prefix";

    if (cv_type == CGV_IPV6PFX) /* RFC6991 */
	return "ipv6_prefix";

    if (cv_type == CGV_TIME) /* RFC6991 */
	return "date-and-time";

    if (cv_type == CGV_MACADDR) /* RFC6991 */
	return "mac-address";

    if (cv_type == CGV_UUID) /* RFC6991 */
	return "uuid";

    return ytype;
}


/*! Get dbspec key of a yang statement, used when generating cli
 *
 * This key is computed when generating a dbspec key syntax from yang specification.
 * It is necessary to know which database key corresponds to a specific node in the
 * yang specification, used in, for example, cli_set() callbacks in the generated CLI
 * code
 */
char *
yang_dbkey_get(yang_stmt *ys)
{
    return ys->ys_dbkey;
}

/*! Set dbspec key of a yang statement, used when generating cli 
 *
 * @param   val   string (copied) defining the db key string.
 */
int 
yang_dbkey_set(yang_stmt *ys, char *val)
{
    if ((ys->ys_dbkey = strdup(val)) == NULL){
	clicon_err(OE_UNIX, errno, "%s: strdup", __FUNCTION__); 
	return -1;
    }
    return 0;
}

/*! Apply a function call recursively on all yang-stmt s recursively
 *
 * Recursively traverse all yang-nodes in a parse-tree and apply fn(arg) for each
 * object found. The function is called with the yang-stmt and an argument as args.
 */
int
yang_apply(yang_node *yn, yang_applyfn_t fn, void *arg)
{
    yang_stmt *ys = NULL;
    int     i;
    int     retval = -1;

    for (i=0; i<yn->yn_len; i++){
	ys = yn->yn_stmt[i];
	if (fn(ys, arg) < 0)
	    goto done;
	if (yang_apply((yang_node*)ys, fn, arg) < 0)
	    goto done;
    }
    retval = 0;
  done:
    return retval;
}

static yang_stmt *
yang_dbkey_vec(yang_node *yn, char **vec, int nvec)
{
    char            *key;
    yang_stmt       *ys;
    long             i;

    if (nvec <= 0)
	return NULL;
    key = vec[0];
    if (yn->yn_keyword == Y_LIST){
	i = strtol(key, (char **) NULL, 10);
	if ((i == LONG_MIN || i == LONG_MAX) && errno)
	    goto done;
	if (nvec == 1)
	    return (yang_stmt*)yn;
	vec++;
	nvec--;
	key = vec[0];
    }
    if ((ys = yang_find_specnode(yn, key)) == NULL)
	goto done;
    if (nvec == 1)
	return ys;
    return yang_dbkey_vec((yang_node*)ys, vec+1, nvec-1);
  done:
    return NULL;
}

/*! Given a dbkey (eg a.b.0) find matching yang specification
 *
 * e.g. a.0 matches the db_spec corresponding to a[].
 * Input args:
 * @param key  key to find in dbspec
 */
yang_stmt *
dbkey2yang(yang_node *yn, char *dbkey)
{
    char           **vec;
    int              nvec;
    yang_stmt       *ys;

    if ((vec = clicon_strsplit(dbkey, ".", &nvec, __FUNCTION__)) == NULL){
	clicon_err(OE_DB, errno, "%s: strsplit", __FUNCTION__); 
	return NULL;
    }
    ys = yang_dbkey_vec(yn, vec, nvec);
    unchunk_group(__FUNCTION__);
    return ys;
}


static yang_stmt *
yang_xpath_vec(yang_node *yn, char **vec, int nvec)
{
    char            *key;
    yang_stmt       *ys;

    if (nvec <= 0)
	return NULL;
    key = vec[0];
    if ((ys = yang_find_specnode(yn, key)) == NULL)
	goto done;
    if (nvec == 1)
	return ys;
    return yang_xpath_vec((yang_node*)ys, vec+1, nvec-1);
  done:
    return NULL;
}

/*! Given an xpath (eg /a/b/c) find matching yang specification
 */
yang_stmt *
yang_xpath(yang_node *yn, char *xpath)
{
    char           **vec;
    int              nvec;
    yang_stmt       *ys;

    if ((vec = clicon_strsplit(xpath, "/", &nvec, __FUNCTION__)) == NULL){
	clicon_err(OE_DB, errno, "%s: strsplit", __FUNCTION__); 
	return NULL;
    }
    ys = yang_xpath_vec(yn, vec, nvec);
    unchunk_group(__FUNCTION__);
    return ys;
}

/*! Parse argument as CV and save result in yang cv variable
 *
 * Note that some CV:s are parsed directly (eg mandatory) while others are parsed in second pass
 * (ys_populate). The reason being that all information is not available in the first pass.
 */
cg_var *
ys_parse(yang_stmt *ys, enum cv_type cvtype)
{
    int             cvret;
    char           *reason = NULL;

    assert(ys->ys_cv == NULL); /* Cv:s are parsed in different places, difficult to separate */
    if ((ys->ys_cv = cv_new(cvtype)) == NULL){
	clicon_err(OE_DB, errno, "%s: cv_new", __FUNCTION__); 
	goto done;
    }
    if ((cvret = cv_parse1(ys->ys_argument, ys->ys_cv, &reason)) < 0){ /* error */
	clicon_err(OE_DB, errno, "parsing cv");
	ys->ys_cv = NULL;
	goto done;
    }
    if (cvret == 0){ /* parsing failed */
	clicon_err(OE_DB, errno, "Parsing CV: %s", reason);
	ys->ys_cv = NULL;
	goto done;
    }
    /* cvret == 1 means parsing is OK */
  done:
    if (reason)
	free(reason);
    return ys->ys_cv;
}

/*
 * Actually: min..max [| min..max]*  
 *   where min,max is integer or keywords 'min' or 'max. 
 * We only allow:
 * - numbers in min..max, no keywords
 * - only int64
 * - only one range, ie not 1..2|4..5
 * - both min..max and max, ie both 1..3 and 3.
 */
static int
ys_parse_range(yang_stmt *ys)
{
    int     retval = -1;
    int     retval2;
    char   *minstr;
    char   *maxstr;
    char   *reason = NULL;

    if ((minstr = strdup(ys->ys_argument)) == NULL){
	clicon_err(OE_DB, errno, "strdup");
	goto done;
    }
    if ((maxstr = strstr(minstr, "..")) != NULL){
	if (strlen(maxstr) < 2){
	    clicon_err(OE_DB, 0, "range statement: %s not on the form: <int>..<int>", minstr);
	    goto done;
	}
	minstr[maxstr-minstr] = '\0';
	maxstr += 2;
	if ((retval2 = parse_int64(minstr, &ys->ys_range_min, &reason)) < 0){
	    clicon_err(OE_DB, errno, "range statement, min str not well-formed: %s", minstr);
	    goto done;
	}
	if (retval2 == 0){
	    clicon_err(OE_DB, errno, "range statement, min str %s: %s", minstr, reason);
	    free(reason);
	    goto done;
	}
    }
    else{
	ys->ys_range_min = LLONG_MIN;
	maxstr = minstr;
    }
    if ((retval2 = parse_int64(maxstr, &ys->ys_range_max, &reason)) < 0){
	clicon_err(OE_DB, errno, "range statement, min str not well-formed: %s", maxstr);
	goto done;
    }
    if (retval2 == 0){
	clicon_err(OE_DB, errno, "range statement, min str %s: %s", maxstr, reason);
	free(reason);
	goto done;
    }
  done:
    if (minstr)
	free(minstr);
    return retval;
}


/*! First round yang syntactic statement specific checks. No context checks.
 *
 * Specific syntax checks for yang statements where one cannot assume the context is parsed. 
 * That is, siblings, etc, may not be there. Complete checks are made in ys_populate instead.
 */
int
ys_parse_sub(yang_stmt *ys)
{
    int        retval = -1;
    yang_stmt *yp;
    
    switch (ys->ys_keyword){
    case Y_RANGE: 
    case Y_LENGTH: 
	if (ys_parse_range(ys) < 0)
	    goto done;
	break;
    case Y_MANDATORY:
	if (ys_parse(ys, CGV_BOOL) == NULL) 
	    goto done;
	if ((yp = (yang_stmt*)ys->ys_parent) != NULL)
	    yp->ys_mandatory = cv_bool_get(ys->ys_cv);
	break;
    default:
	break;
    }
    retval = 0;
  done:
    return retval;
}

/*! Validate cligen variable cv using yang statement as spec
 *
 * @param [in]  cv      A cligen variable to validate. This is a correctly parsed cv.
 * @param [in]  ys      A yang statement, must be leaf of leaf-list.
 * @param [out] reason  If given, and if return value is 0, contains a malloced string
 *                      describing the reason why the validation failed. Must be freed.
 * @retval -1  Error (fatal), with errno set to indicate error
 * @retval 0   Validation not OK, malloced reason is returned. Free reason with free()
 * @retval 1   Validation OK
 */
int
ys_cv_validate(cg_var *cv, yang_stmt *ys, char **reason)
{
    int             retval = 1; /* OK */
    cg_var         *ycv;        /* cv of yang-statement */  
    long long       i = 0;
    char           *str;
    yang_stmt      *ytype;
    yang_stmt      *yrange;
    yang_stmt      *ypattern;
    int             retval2;
    char           *pattern;

    if (ys->ys_keyword != Y_LEAF && ys->ys_keyword != Y_LEAF_LIST)
	return 0;
    ycv = ys->ys_cv;
    
    if ((ytype = yang_find((yang_node*)ys, Y_TYPE, NULL)) == NULL){
	clicon_err(OE_DB, 0, "type not found under leaf %s", ys->ys_argument);
	return -1;
    }
    switch (cv_type_get(ycv)){
    case CGV_INT:
	i = cv_int_get(cv);
    case CGV_LONG: /* fallthru */
	 /* Check range if specified */
	if (cv_type_get(ycv) == CGV_LONG)
	    i = cv_long_get(cv);
	if ((yrange = yang_find((yang_node*)ytype, Y_RANGE, NULL)) != NULL){
	    if (i < yrange->ys_range_min || i > yrange->ys_range_max) {
		if (reason)
		    *reason = cligen_reason("Number out of range: %i", i);
		retval = 0;
	    }
	}
	break;
    case CGV_STRING:
	str = cv_string_get(cv);
	i = strlen(str);
	if ((yrange = yang_find((yang_node*)ytype, Y_LENGTH, NULL)) != NULL){
	    if (i < yrange->ys_range_min || i > yrange->ys_range_max) {
		if (reason)
		    *reason = cligen_reason("String length out of range: %i", i);
		retval = 0;
	    }
	}
	if ((ypattern = yang_find((yang_node*)ytype, Y_PATTERN, NULL)) != NULL){
	    pattern = ypattern->ys_argument;
	    if ((retval2 = match_regexp(str, pattern)) < 0){
		clicon_err(OE_DB, 0, "match_regexp: %s", pattern);
		return -1;
	    }
	    if (retval2 == 0){
		if (reason)
		    *reason = cligen_reason("regexp match fail: %s does not match %s",
					    str, pattern);
		retval = 0;
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
    case CGV_REST:
    case CGV_IPV4ADDR: 
    case CGV_IPV6ADDR: 
    case CGV_IPV4PFX: 
    case CGV_IPV6PFX: 
    case CGV_MACADDR:
    case CGV_URL: 
    case CGV_UUID: 
    case CGV_TIME: 
	break;
    }
    if (reason && *reason)
	assert(retval == 0);
    return retval;
}
