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
#include <sys/stat.h>
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
#include "clicon_yang_parse.h"
#include "clicon_yang2key.h"

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
    {"yang-specification", Y_SPEC}, /* XXX: NOTE NOT YANG STATEMENT, reserved 
				       for top level spec */
    {NULL,               -1}
};

yang_spec *
yspec_new(void)
{
    yang_spec *yspec;

    if ((yspec = malloc(sizeof(*yspec))) == NULL){
	clicon_err(OE_YANG, errno, "%s: malloc", __FUNCTION__);
	return NULL;
    }
    memset(yspec, 0, sizeof(*yspec));
    yspec->yp_keyword = Y_SPEC;
    return yspec;
}

yang_stmt *
ys_new(enum rfc_6020 keyw)
{
    yang_stmt *ys;

    if ((ys = malloc(sizeof(*ys))) == NULL){
	clicon_err(OE_YANG, errno, "%s: malloc", __FUNCTION__);
	return NULL;
    }
    memset(ys, 0, sizeof(*ys));
    ys->ys_keyword    = keyw;
    /* The cvec contains stmt-specific variables. Only few stmts need variables so the
       cvec could be lazily created to save some heap and cycles. */
    if ((ys->ys_cvec = cvec_new(0)) == NULL){ 
	clicon_err(OE_YANG, errno, "%s: cvec_new", __FUNCTION__);
	return NULL;
    }
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
    if (ys->ys_cvec)
	cvec_free(ys->ys_cvec);
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
	clicon_err(OE_YANG, errno, "%s: realloc", __FUNCTION__);
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

/*! Find first child yang_stmt with matching keyword and argument
 *
 * If argument is NULL, match any argument.
 * If key is 0 match any keyword
 * This however means that if you actually want to match only a yang-stmt with 
 * argument==NULL you cannot, but I have not seen any such examples.
 * @see yang_find_specnode
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
 * @see yang_find
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

/*! Find a child spec-node yang_stmt with matching argument for xpath
 *
 * See also yang_find() but this looks only for the yang specification nodes with
 * the following keyword: container, leaf, list, leaf-list
 * That is, basic syntax nodes.
 * @see yang_find_specnode # Maybe this is the same as specnode?
 */
static yang_node *
yang_find_xpath(yang_node *yn, char *argument)
{
    yang_node *ys = NULL;
    int i;
    int match = 0;

    for (i=0; i<yn->yn_len; i++){
	ys = (yang_node*)yn->yn_stmt[i];
	if (ys->yn_keyword == Y_CONTAINER || ys->yn_keyword == Y_LEAF || 
	    ys->yn_keyword == Y_LIST || ys->yn_keyword == Y_LEAF_LIST ||
	    ys->yn_keyword == Y_MODULE || ys->yn_keyword == Y_SUBMODULE){
	    if (argument == NULL)
		match++;
	    else
		if (ys->yn_argument && strcmp(argument, ys->yn_argument) == 0)
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
 * Populate leaf in 2nd round of yang parsing, now that context is complete:
 * 1. Find type specification and set cv type accordingly 
 * 2. Create the CV using cvtype and name it 
 * 3. Check if default value. Here we parse the string in the default-stmt and add it to leafs cv.
 * 4. Check if leaf is part of list, if key exists mark leaf as key/unique 
 * XXX: extend type search
 *
 * @param[in] ys   The yang statement to populate.
 * @param[in] arg  A void argument not used
 * @retval    0    OK
 * @retval    -1   Error with clicon_err called
 */
static int
ys_populate_leaf(yang_stmt *ys, void *arg)
{
    int             retval = -1;
    cg_var         *cv = NULL;
    yang_node      *yparent; 
    yang_stmt      *ydef; 
    enum cv_type    cvtype = CGV_ERR;
    int             cvret;
    int             ret;
    char           *reason = NULL;
    yang_stmt      *yrestype;  /* resolved type */
    char           *restype;  /* resolved type */
    char           *type;   /* original type */
    uint8_t         fraction_digits;
    int             options = 0x0;

    yparent = ys->ys_parent;     /* Find parent: list/container */
    /* 1. Find type specification and set cv type accordingly */
    if (yang_type_get(ys, &type, &yrestype, &options, NULL, NULL, NULL, &fraction_digits) < 0)
	goto done;
    restype = yrestype?yrestype->ys_argument:NULL;
    if (clicon_type2cv(type, restype, &cvtype) < 0) /* This handles non-resolved also */
	goto done;
    /* 2. Create the CV using cvtype and name it */
    if ((cv = cv_new(cvtype)) == NULL){
	clicon_err(OE_YANG, errno, "%s: cv_new", __FUNCTION__); 
	goto done;
    }
    if (options & YANG_OPTIONS_FRACTION_DIGITS && cvtype == CGV_DEC64) /* XXX: Seems misplaced? / too specific */
	cv_dec64_n_set(cv, fraction_digits);

    if (cv_name_set(cv, ys->ys_argument) == NULL){
	clicon_err(OE_YANG, errno, "%s: cv_new_set", __FUNCTION__); 
	goto done;
    }
    /* 3. Check if default value. Here we parse the string in the default-stmt
     * and add it to the leafs cv.
     */
    if ((ydef = yang_find((yang_node*)ys, Y_DEFAULT, NULL)) != NULL){
	if ((cvret = cv_parse1(ydef->ys_argument, cv, &reason)) < 0){ /* error */
	    clicon_err(OE_YANG, errno, "parsing cv");
	    goto done;
	}
	if (cvret == 0){ /* parsing failed */
	    clicon_err(OE_YANG, errno, "Parsing CV: %s", reason);
	    free(reason);
	    goto done;
	}
    }
    else{
	/* 3b. If not default value, indicate empty cv. */
	cv_flag_set(cv, V_UNSET); /* no value (no default) */
    }

    /* 4. Check if leaf is part of list, if key exists mark leaf as key/unique */
    if (yparent && yparent->yn_keyword == Y_LIST){
	if ((ret = yang_key_match(yparent, ys->ys_argument)) < 0)
	    goto done;
	if (ret == 1)
	    cv_flag_set(cv, V_UNIQUE);
    }
    ys->ys_cv = cv;
    retval = 0;
  done:
    if (cv && retval < 0)
	cv_free(cv);
    return retval;
}

/*! Populate range and length statements
 *
 * Create cvec variables "range_min" and "range_max". Assume parent is type.
 * Actually: min..max [| min..max]*  
 *   where min,max is integer or keywords 'min' or 'max. 
 * We only allow one range, ie not 1..2|4..5
 */
static int
ys_populate_range(yang_stmt *ys, void *arg)
{
    int             retval = -1;
    yang_node      *yparent;        /* type */
    char           *origtype;  /* orig type */
    yang_stmt      *yrestype;   /* resolved type */
    char           *restype;   /* resolved type */
    int             options = 0x0;
    uint8_t         fraction_digits;
    enum cv_type    cvtype = CGV_ERR;
    char           *minstr = NULL;
    char           *maxstr;
    cg_var         *cv;
    char           *reason = NULL;
    int             cvret;

    yparent = ys->ys_parent;     /* Find parent: type */
    if (yparent->yn_keyword != Y_TYPE){
	clicon_err(OE_YANG, 0, "%s: parent should be type", __FUNCTION__); 
	goto done;
    }
    if (yang_type_resolve(ys, (yang_stmt*)yparent, &yrestype, 
			  &options, NULL, NULL, NULL, &fraction_digits) < 0)
	goto done;
    restype = yrestype?yrestype->ys_argument:NULL;
    origtype = ytype_id((yang_stmt*)yparent);
    /* This handles non-resolved also */
    if (clicon_type2cv(origtype, restype, &cvtype) < 0) 
	goto done;
    if ((minstr = strdup(ys->ys_argument)) == NULL){
	clicon_err(OE_YANG, errno, "strdup");
	goto done;
    }
    if ((maxstr = strstr(minstr, "..")) != NULL){
	if (strlen(maxstr) < 2){
	    clicon_err(OE_YANG, 0, "range statement: %s not on the form: <int>..<int>");
           goto done;
       }
       minstr[maxstr-minstr] = '\0';
       maxstr += 2;
	if ((cv = cvec_add(ys->ys_cvec, cvtype)) == NULL){
	    clicon_err(OE_YANG, errno, "cvec_add");
	    goto done;
	}
	if (cv_name_set(cv, "range_min") == NULL){
	    clicon_err(OE_YANG, errno, "cv_name_set");
	    goto done;
	}
	if (options & YANG_OPTIONS_FRACTION_DIGITS && cvtype == CGV_DEC64)
	    cv_dec64_n_set(cv, fraction_digits);

	if ((cvret = cv_parse1(minstr, cv, &reason)) < 0){
	    clicon_err(OE_YANG, errno, "cv_parse1");
	    goto done;
	}
	if (cvret == 0){ /* parsing failed */
	    clicon_err(OE_YANG, errno, "range statement, min: %s", reason);
	    free(reason);
	    goto done;
	}
    }
    else
	maxstr = minstr;
    if (strcmp(maxstr, "max") != 0){ /* no range_max means max */
	if ((cv = cvec_add(ys->ys_cvec, cvtype)) == NULL){
	    clicon_err(OE_YANG, errno, "cvec_add");
	    goto done;
	}
	if (cv_name_set(cv, "range_max") == NULL){
	    clicon_err(OE_YANG, errno, "cv_name_set");
	    goto done;
	}
	if (options & YANG_OPTIONS_FRACTION_DIGITS && cvtype == CGV_DEC64)
	    cv_dec64_n_set(cv, fraction_digits);
	if ((cvret = cv_parse1(maxstr, cv, &reason)) < 0){
	    clicon_err(OE_YANG, errno, "cv_parse1");
	    goto done;
	}
	if (cvret == 0){ /* parsing failed */
	    clicon_err(OE_YANG, errno, "range statement, max: %s", reason);
	    free(reason);
	    goto done;
	}
    }
    retval = 0;
  done:
    if (minstr)
	free(minstr);
    return retval;
}

/*! Sanity check yang type statement
 * XXX: Replace with generic parent/child type-check
 */
static int
ys_populate_type(yang_stmt *ys, void *arg)
{
    int             retval = -1;

    if (strcmp(ys->ys_argument, "decimal64") == 0){
	if (yang_find((yang_node*)ys, Y_FRACTION_DIGITS, NULL) == NULL){
	    clicon_err(OE_YANG, 0, "decimal64 type requires fraction-digits sub-statement");
	    goto done;
	}
    }
    retval = 0;
  done:
    return retval;
}

/*! Populate with cligen-variables, default values, etc. Sanity checks on complete tree.
 *
 * We do this in 2nd pass after complete parsing to be sure to have a complete parse-tree
 * See ys_parse_sub for first pass and what can be assumed
 * After this pass, cv:s are set for LEAFs and LEAF-LISTs
 */
static int
ys_populate(yang_stmt *ys, void *arg)
{
    int retval = -1;

    switch(ys->ys_keyword){
    case Y_LEAF:
    case Y_LEAF_LIST:
	if (ys_populate_leaf(ys, arg) < 0)
	    goto done;
	break;
    case Y_RANGE: 
    case Y_LENGTH: 
	if (ys_populate_range(ys, arg) < 0)
	    goto done;
	break;
    case Y_MANDATORY: /* call yang_mandatory() to check if set */
    case Y_CONFIG:
	if (ys_parse(ys, CGV_BOOL) == NULL) 
	    goto done;
	break;
    case Y_TYPE:
	if (ys_populate_type(ys, arg) < 0)
	    goto done;
	break;
    default:
	break;
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
static yang_stmt *
yang_parse_str(clicon_handle h,
	       char *str,
	       const char *name, /* just for errs */
	       yang_spec *yspec)
{
    struct clicon_yang_yacc_arg yy = {0,};
    yang_stmt                  *ym = NULL;

    yy.yy_handle       = h; 
    yy.yy_name         = (char*)name;
    yy.yy_linenum      = 1;
    yy.yy_parse_string = str;
    yy.yy_stack        = NULL;
    yy.yy_module       = NULL; /* this is the return value - the module/sub-module */

    if (ystack_push(&yy, (yang_node*)yspec) == NULL)
	goto done;
    if (strlen(str)){ /* Not empty */
	if (yang_scan_init(&yy) < 0)
	    goto done;
	if (yang_parse_init(&yy, yspec) < 0)
	    goto done;
	if (clicon_yang_parseparse(&yy) != 0) { /* yacc returns 1 on error */
	    clicon_log(LOG_NOTICE, "Yang error: %s on line %d", name, yy.yy_linenum);
	    if (clicon_errno == 0)
		clicon_err(OE_YANG, 0, "yang parser error with no error code (should not happen)");
	    yang_parse_exit(&yy);
	    yang_scan_exit(&yy);
	    goto done;
	}
	if (yang_parse_exit(&yy) < 0)
	    goto done;		
	if (yang_scan_exit(&yy) < 0)
	    goto done;		
    }
    ym = yy.yy_module;
  done:
    ystack_pop(&yy);
    return ym;
}

/*! Parse a file containing a YANG into a parse-tree
 *
 * Similar to clicon_yang_str(), just read a file first
 * (cloned from cligen)
 * The database symbols are inserted in alphabetical order.
 */
static yang_stmt *
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
    yang_stmt    *ymodule = NULL;

    clicon_debug(1, "Yang parse file: %s", name);
    len = 1024; /* any number is fine */
    if ((buf = malloc(len)) == NULL){
	perror("pt_file malloc");
	return NULL;
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
    if ((ymodule = yang_parse_str(h, buf, name, ysp)) < 0)
	goto done;
  done:
    if (buf)
	free(buf);
    return ymodule;
}

/*
 * module-or-submodule-name ['@' revision-date] ( '.yang' / '.yin' )
 */
static yang_stmt *
yang_parse2(clicon_handle h, const char *yang_dir, const char *module, const char *revision, yang_spec *ysp)
{
    FILE       *f = NULL;
    cbuf       *b;
    char       *filename;
    yang_stmt  *ys = NULL;
    struct stat st;

    if ((b = cbuf_new()) == NULL){
	clicon_err(OE_YANG, errno, "%s: cbuf_new", __FUNCTION__);
	goto done;
    }
    if (revision)
	cprintf(b, "%s/%s@%s.yang", yang_dir, module, revision);
    else
	cprintf(b, "%s/%s.yang", yang_dir, module);
    filename = cbuf_get(b);
    if (stat(filename, &st) < 0){
	clicon_err(OE_YANG, errno, "%s not found", filename);
       goto done;
    }
    if ((f = fopen(filename, "r")) == NULL){
	clicon_err(OE_UNIX, errno, "fopen(%s)", filename);	
	goto done;
    }
    if ((ys = yang_parse_file(h, f, filename, ysp)) == NULL)
	goto done;
  done:
    if (b)
	cbuf_free(b);
    if (f)
	fclose(f);
    return ys;
}

/*! Parse dbspec using yang format
 *
 * The database symbols are inserted in alphabetical order.
 * Find a yang module file, and then recursively parse all its imported modules.
 */
static yang_stmt *
yang_parse1(clicon_handle h, const char *yang_dir, const char *module, const char *revision, yang_spec *ysp)
{
    yang_stmt  *yi = NULL; /* import */
    yang_stmt  *ys;
    yang_stmt  *yrev;
    char       *modname;
    char       *subrevision;

    if ((ys = yang_parse2(h, yang_dir, module, revision, ysp)) == NULL)
	goto done;
    /* go through all import statements of ysp (or its module) */
    while ((yi = yn_each((yang_node*)ys, yi)) != NULL){
	if (yi->ys_keyword != Y_IMPORT)
	    continue;
	modname = yi->ys_argument;
	if ((yrev = yang_find((yang_node*)yi, Y_REVISION_DATE, NULL)) != NULL)
	    subrevision = yrev->ys_argument;
	else
	    subrevision = NULL;
	if (yang_find((yang_node*)ysp, Y_MODULE, modname) == NULL)
	    if (yang_parse1(h, yang_dir, modname, subrevision, ysp) == NULL){
		ys = NULL;
		goto done;
	    }
    }
  done:
    return ys;
}


/*! Parse dbspec using yang format
 *
 * @param h        CLICON handle
 * @param yang_dir Directory where all YANG module files reside
 * @param module   Name of main YANG module. More modules may be parsed if imported
 * @param ysp      Yang specification. Should ave been created by caller using yspec_new
 * @retval 0  Everything OK
 * @retval -1 Error encountered
 * The database symbols are inserted in alphabetical order.
 * Find a yang module file, and then recursively parse all its imported modules.
 */
int
yang_parse(clicon_handle h, 
	   const char *yang_dir, 
	   const char *module, 
	   const char *revision, 
	   yang_spec *ysp)
{
    int         retval = -1;
    yang_stmt  *ys;

    if ((ys = yang_parse1(h, yang_dir, module, revision, ysp)) == NULL)
	goto done;
    /* Add top module name as dbspec-name */
    clicon_dbspec_name_set(h, ys->ys_argument);
    /* Go through parse tree and populate it with cv types */
    if (yang_apply((yang_node*)ysp, ys_populate, NULL) < 0)
	goto done;
    retval = 0;
  done:
    return retval;
}

/*! Get dbspec key of a yang statement, used when generating cli
 *
 * This key is computed when generating a dbspec key syntax from yang specification.
 * It is necessary to know which database key corresponds to a specific node in the
 * yang specification, used in, for example, cli_set() callbacks in the generated CLI
 * code
 * @param[in]   ys    yang statement (container, leaf, leaf-list, list)
 * @retval      key   string as dbkey, eg "x[] $!y $z"
 * See also dbkey2yang
 */
char *
yang_dbkey_get(yang_stmt *ys)
{
    return ys->ys_dbkey;
}

/*! Set dbspec key of a yang statement, used when generating cli 
 *
 * @param[in]   ys    yang statement (container, leaf, leaf-list, list)
 * @param[in]   val   string (is copied) defining the db key string for a yang_stmt. 
 * @retval      0     on success
 * @retval      -1    on error. clicon_err called
 * @code
 *   yang_stmt *ys_key;
 *   yang_dbkey_set(ys_key, "x[] $!y $z");
 * @endcode
 */
int 
yang_dbkey_set(yang_stmt *ys, char *val)
{
    assert(ys->ys_dbkey==NULL);
    if (ys->ys_dbkey != NULL)
	free(ys->ys_dbkey);
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
 * The tree is traversed depth-first, which at least guarantees that a parent is
 * traversed before a child.
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

/*! Given a dbkey (eg a.b.0) recursively find matching yang specification
 *
 * e.g. a.0 matches the db_spec corresponding to a[].
 * Input args:
 * @param[in] yn     top-of yang tree where to start finding
 * @param[in] dbkey  databse key to match in yang spec tree
 * See also yang_dbkey_get
 */
yang_stmt *
dbkey2yang(yang_node *yn, char *dbkey)
{
    char           **vec;
    int              nvec;
    yang_stmt       *ys;

    if ((vec = clicon_strsplit(dbkey, ".", &nvec, __FUNCTION__)) == NULL){
	clicon_err(OE_YANG, errno, "%s: strsplit", __FUNCTION__); 
	return NULL;
    }
    ys = yang_dbkey_vec(yn, vec, nvec);
    unchunk_group(__FUNCTION__);
    return ys;
}

/*! All the work for yang_xpath */
static yang_node *
yang_xpath_vec(yang_node *yn, char **vec, int nvec)
{
    char            *arg;
    yang_node       *ys;
    yang_node       *yret = NULL;

    if (nvec <= 0)
	goto done;
    arg = vec[0];
    clicon_debug(1, "%s: key=%s arg=%s match=%s len=%d",
	    __FUNCTION__, yang_key2str(yn->yn_keyword), yn->yn_argument, 
	    arg, yn->yn_len);
    if (strcmp(arg, "..") == 0){
	yret = yn->yn_parent;
	goto done;
    }
    if ((ys = yang_find_xpath(yn, arg)) == NULL){
	yret = (yang_node*)ys;
	goto done;
    }
    if (nvec == 1){
	yret = (yang_node*)ys;
	goto done;
    }
    yret = yang_xpath_vec((yang_node*)ys, vec+1, nvec-1);
 done:
    return yret;
}

/*! Given an xpath (eg /a/b/c) find matching yang specification
 * @param[in]  yn     Yang node tree
 * @param[in]  xpath  A limited xpath expression on the type a/b/c
 * @retval     NULL   Error, with clicon_err called
 * @retval     ys     First yang node matching xpath
 * Note: the identifiers in the xpath (eg a, b in a/b) can match the nodes defined in
 * yang_find_xpath: container, leaf,list,leaf-list, modules, sub-modules
 * Note: Absolute paths are not supported.
 * Note: prefix not supported.
 * Example:
 * yn : module m { prefix b; container b { list c { key d; leaf d; }} }
 * xpath = m/b/c, returns the list 'c'.
 */
yang_node *
yang_xpath(yang_node *yn, char *xpath)
{
    char           **vec;
    int              nvec;
    yang_node       *ys;

    if ((vec = clicon_strsplit(xpath, "/", &nvec, __FUNCTION__)) == NULL){
	clicon_err(OE_YANG, errno, "%s: strsplit", __FUNCTION__); 
	return NULL;
    }
    ys = yang_xpath_vec(yn, vec, nvec);
    unchunk_group(__FUNCTION__);
    return ys;
}

/*! Parse argument as CV and save result in yang cv variable
 *
 * Note that some CV:s are parsed directly (eg fraction-digits) while others are parsed 
 * in second pass (ys_populate). The reason being that all information is not 
 * available in the first pass. Prefer ys_populate
 */
cg_var *
ys_parse(yang_stmt *ys, enum cv_type cvtype)
{
    int             cvret;
    char           *reason = NULL;

    assert(ys->ys_cv == NULL); /* Cv:s are parsed in different places, difficult to separate */
    if ((ys->ys_cv = cv_new(cvtype)) == NULL){
	clicon_err(OE_YANG, errno, "%s: cv_new", __FUNCTION__); 
	goto done;
    }
    if ((cvret = cv_parse1(ys->ys_argument, ys->ys_cv, &reason)) < 0){ /* error */
	clicon_err(OE_YANG, errno, "parsing cv");
	ys->ys_cv = NULL;
	goto done;
    }
    if (cvret == 0){ /* parsing failed */
	clicon_err(OE_YANG, errno, "Parsing CV: %s", reason);
	ys->ys_cv = NULL;
	goto done;
    }
    /* cvret == 1 means parsing is OK */
  done:
    if (reason)
	free(reason);
    return ys->ys_cv;
}

/*! First round yang syntactic statement specific checks. No context checks.
 *
 * Specific syntax checks  and variable creation for stand-alone yang statements.
 * That is, siblings, etc, may not be there. Complete checks are made in
 * ys_populate instead.
 *
 * The cv:s created in parse-tree as follows:
 * fraction-digits : Create cv as uint8, check limits [1:8] (must be made in 1st pass)
 *
 * See also ys_populate
 */
int
ys_parse_sub(yang_stmt *ys)
{
    int        retval = -1;
    
    switch (ys->ys_keyword){
    case Y_FRACTION_DIGITS:{
	uint8_t fd;
	if (ys_parse(ys, CGV_UINT8) == NULL) 
	    goto done;
	fd = cv_uint8_get(ys->ys_cv);
	if (fd < 1 || fd > 18){
	    clicon_err(OE_YANG, errno, "%u: Out of range, should be [1:18]", fd);
	    goto done;
	}
	break;
    }
    default:
	break;
    }
    retval = 0;
  done:
    return retval;
}

/*! Return if this leaf is mandatory or not
 * Note: one can cache this value in ys_cvec instead of functionally evaluating it.
 * @retval 1 yang statement is leaf and it has a mandatory sub-stmt with value true
 * @retval 0 The negation of conditions for return value 1.
 */
int
yang_mandatory(yang_stmt *ys)
{
    yang_stmt *ym;

    if (ys->ys_keyword != Y_LEAF)
	return 0;
    if ((ym = yang_find((yang_node*)ys, Y_MANDATORY, NULL)) != NULL){
	if (ym->ys_cv == NULL) /* shouldnt happen */
	    return 0; 
	return cv_bool_get(ym->ys_cv);
    }
    return 0;
}

/*! Return config state of this node
 * config statement is default true. 
 * Note that a node with config=false may not have a sub
 * statement where config=true. And this function does not check the sttaus of a parent.
 * @retval 0 if node has a config sub-statement and it is false
 * @retval 1 node has not config sub-statement or it is true
 */
int
yang_config(yang_stmt *ys)
{
    yang_stmt *ym;

    if ((ym = yang_find((yang_node*)ys, Y_CONFIG, NULL)) != NULL){
	if (ym->ys_cv == NULL) /* shouldnt happen */
	    return 1; 
	return cv_bool_get(ym->ys_cv);
    }
    return 1;
}


/*! Utility function for handling yang parsing and translation to key format
 * @param h          clicon handle
 * @param f          file to print to (if one of print options are enabled)
 * @param printspec  print database (YANG) specification as read from file
 * @param printalt   print alternate specification (KEY)
 */

int
yang_spec_main(clicon_handle h, FILE *f, int printspec, int printalt)
{
    yang_spec      *yspec;
    char           *yang_dir;
    char           *yang_module;
    int             retval = -1;
    dbspec_key *db_spec;

    if ((yspec = yspec_new()) == NULL)
	goto done;
    if ((yang_dir    = clicon_yang_dir(h)) == NULL){
	clicon_err(OE_FATAL, 0, "CLICON_YANG_DIR option not set");
	goto done;
    }
    if ((yang_module = clicon_yang_module_main(h)) == NULL){
	clicon_err(OE_FATAL, 0, "CLICON_YANG_MODULE_MAIN option not set");
	goto done;
    }
    if (yang_parse(h, yang_dir, yang_module, NULL, yspec) < 0)
	goto done;
    clicon_dbspec_yang_set(h, yspec);	
    if (printspec)
	yang_print(f, (yang_node*)yspec, 0);
    if ((db_spec = yang2key(yspec)) == NULL) /* To dbspec */
	goto done;
    clicon_dbspec_key_set(h, db_spec);	
    if (printalt)
	db_spec_dump(f, db_spec);
    if (0){ /* debug for translating back to original, just debug */
	yang_spec *yspec2;
	if ((yspec2 = key2yang(db_spec)) == NULL)
	    goto done;
	clicon_dbspec_yang_set(h, yspec2);
	yang_print(f, (yang_node*)yspec2, 0);
    }
    retval = 0;
  done:
    return retval;
}

/*! Given a yang node, translate the argument string to a cv vector
 *
 * @param[in]  ys         Yang statement 
 * @param[in]  delimiter  Delimiter character (eg ' ' or ',')
 * @retval     NULL  Error
 * @retval     cvec  Vector of strings
 * @code
 *    cvec   *cvv;
 *    cg_var *cv = NULL;
 *    if ((cvv = yang_arg2cvec(ys, " ")) == NULL)
 *       goto err;
 *    while ((cv = cvec_each(cvv, cv)) != NULL) 
 *         ...cv_string_get(cv);
 *    cvec_free(cvv);
 * @endcode
 * Note: must free return value after use w cvec_free
 */
cvec *
yang_arg2cvec(yang_stmt *ys, char *delim)
{
    char  **vec;
    int     i;
    int     nvec;
    cvec   *cvv = NULL;
    cg_var *cv;

    if ((vec = clicon_strsplit(ys->ys_argument, " ", &nvec, __FUNCTION__)) == NULL){
	clicon_err(OE_YANG, errno, "clicon_strsplit");	
	goto done;
    }
    if ((cvv = cvec_new(nvec)) == NULL){
	clicon_err(OE_YANG, errno, "cvec_new");	
	goto done;
    }
    for (i = 0; i < nvec; i++) {
	cv = cvec_i(cvv, i);
	cv_type_set(cv, CGV_STRING);
	if ((cv_string_set(cv, vec[i])) == NULL){
	    clicon_err(OE_YANG, errno, "cv_string_set");	
	    cvv = NULL;
	    goto done;
	}
    }
 done:
    unchunk_group(__FUNCTION__);
    return cvv;
}

/*! Check if yang node yn has key-stmt as child which matches name
 *
 * @param[in]  yn   Yang node with sub-statements (look for a key child)
 * @param[in]  name Check if this name (eg "b") is a key in the yang key statement
 *
 * @retval   -1     Error
 * @retval    0     No match
 * @retval    1     Yes match
 */
int
yang_key_match(yang_node *yn, char *name)
{
    int        retval = -1;
    yang_stmt *ys = NULL;
    int        i;
    cvec      *cvv = NULL;
    cg_var    *cv;

    for (i=0; i<yn->yn_len; i++){
	ys = yn->yn_stmt[i];
	if (ys->ys_keyword == Y_KEY){
	    if ((cvv = yang_arg2cvec(ys, " ")) == NULL)
		goto done;
	    cv = NULL;
	    while ((cv = cvec_each(cvv, cv)) != NULL) {
		if (strcmp(name, cv_string_get(cv)) == 0){
		    retval = 1; /* match */
		    goto done;
		}
	    }
	    cvec_free(cvv);
	    cvv = NULL;
	}
    }
    retval = 0;
 done: 
    if (cvv)
	cvec_free(cvv);
    return retval;
}
