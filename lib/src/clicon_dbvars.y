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
 * Lvalues
 */

%union 
{
        int number;
        char *string;
}

%token EOL
%token WHITE
%token <string> STRING
%token <string> NAME

%lex-param     {void *_ya} /* Add this argument to parse() and lex() function */
%parse-param   {void *_ya}

%{
#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <inttypes.h>
#include <ctype.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include "clicon_queue.h"
#include "clicon_err.h"
#include "clicon_hash.h"
#include "clicon_handle.h"
#include "clicon_dbspec_key.h"
#include "clicon_lvalue.h"
#include "clicon_dbvars.h"
#include "clicon_dbutil.h"

/* typecast macro */
#define _YA ((clicon_dbvarsparse_t *)_ya)

/* add _yf to error paramaters */
#define YY_(msgid) msgid 

static int debug = 0; 

static int
dbvars_debug(const char *fmt, ...)
{
    int len;
    va_list args;
    
    if (!debug)
	return 0;

    va_start(args, fmt);
    len = vfprintf(stderr, fmt, args);
    va_end (args);

    return len;
}

/*
 * Validate name
 */
static int
validate_name(const char *name)
{

    if (!isalpha(*name) && *name!='_') /* Must beging with [a-zA-Z_] */
	return 0;

    while (*(++name)) 
	if (!isalnum(*name) && *name!='_')
	    return 0;
		
    return 1;
}


static cg_var *
clicon_dbvars_addcgv(clicon_dbvarsparse_t *dvp, char *name, cg_var *cgv)
{
    int unique = 0;
    int wildcard = 0;
    cg_var *retval = NULL;
    cg_var *cv;

    if (name[0] == '!') { /* A unique variable */
	unique = 1;
	name++;
    }

    if (!validate_name(name)) {
	clicon_err(OE_CFG, 0, "Invalid variable name: %s", name);
	return NULL;
    }

    if ((cgv = cv_dup(cgv)) == NULL)
	goto done;
    
    /* A value of '*' means a wildcard */
    if (cv_type_get(cgv) == CGV_STRING && strcmp(cv_string_get(cgv), "*") == 0) {
	wildcard = 1;
	cv_string_set(cgv, NULL); /* free string */
	cv_type_set(cgv, CGV_INT32);
	cv_int32_set(cgv, 0); /* Doesn't matter */
    }

    if ((cv = cvec_add (dvp->dvp_ret->dbv_vec, CGV_INT32)) == NULL)
	goto done;
    if (cv_cp(cv, cgv) != 0)
	goto done;
    if (cv_name_set(cv, name) == NULL)
	goto done;
    if (unique) 
	cv_flag_set(cv, V_UNIQUE);
    if (wildcard) 
	cv_flag_set(cv, V_WILDCARD);
    
    retval = cv;
 done:
    cv_free(cgv);
    return retval;
}

static int
clicon_dbvars_add_typed_var(clicon_dbvarsparse_t *dvp, char *var, char *val, char *typestr)
{
    int retval = -1;
    cg_var *cv = NULL;
    cg_var *new;
    int type;
    int sequence = 0;
    
    if (strcmp (typestr, "sequence") == 0) {
	typestr = "int";
	sequence = 1;
    }

    if ((type = cv_str2type(typestr)) == CGV_ERR) {
	clicon_err(OE_CFG, 0, "Invalid type specification %s", typestr);
	goto done;
    }

    if ((cv = cv_new(type)) == NULL){
	clicon_err(OE_CFG, errno, "cv_new");
	goto done;
    }
    if (cv_parse (val, cv) < 0)
	goto done;
    if (cv_name_set (cv, var) == NULL)
	goto done;

    if ((new = clicon_dbvars_addcgv (dvp, var, cv)) != NULL) {
	retval = 0;
	if (sequence)
	    cv_flag_set(new, V_SEQ);
    }
 done:
    if (cv)
	cv_free(cv);
    return retval;
}
  


static int
clicon_dbvars_add_var(clicon_dbvarsparse_t *dvp, char *name, char *var)
{
    int n;
    cg_var *cgv;
    int unique = 0;

    if (name[0]=='!') {
	unique = 1;
	name++;
    }

    /* A cvec index specified? */
    if (strspn(var, "1234567890") == strlen(var)) {
	n = atoi(var);
	if (n == 0 || n >= cvec_len(dvp->dvp_vars)) {
	    clicon_err(OE_CFG, 0, "Variable index out of range: %s", var);
	    return -1;
	}
	if (clicon_dbvars_addcgv(dvp, name-unique, /* include '!' */ 
				 cvec_i(dvp->dvp_vars, n)) == NULL) {
	    clicon_err(OE_CFG, 0, "addcgv failed");
	    return -1;
	}
    }
    else {
	if ((cgv = cvec_find(dvp->dvp_vars, var)) == NULL) {
	    clicon_err(OE_CFG, 0, "Variable not found in cvec: %s", var);
	    return -1;
	}
	if (clicon_dbvars_addcgv(dvp, name-unique, cgv) == NULL) {
	    clicon_err(OE_CFG, 0, "addcgv failed");
	    return -1;
	}
    }

    return 0;
}


static int
clicon_dbvars_varcb(clicon_dbvarsparse_t *dvp,
		    char *var, char *func, char *argtype, char *argval)
{
    int retval = -1;
    int type;
    cg_var     *cv = NULL;
    cg_var     *arg = NULL;
 
    if (dvp->dvp_valcb == NULL)
	goto done;
    
    /*
     * Prepare arg if any
     */
    if (argval != NULL) {
	
	if (argtype == NULL)
	    argtype = "string";
	
	if ((type = cv_str2type(argtype)) == CGV_ERR) {
	    clicon_err(OE_CFG, 0, "Invalid type: %s", argtype);
	    goto done;
	}
	if ((arg = cv_new(CGV_STRING)) == NULL){
	    clicon_err(OE_CFG, errno, "cv_new");
	    goto done;
	}
	cv_name_set(arg, NULL);
	cv_type_set(arg, type);
	if (cv_parse (argval, arg) < 0) {
	    clicon_err(OE_CFG, errno,
		       "Failed to parse callback argument: %s", argval);
	    goto done;
	}
    }


    /*
     * Prepare result cv
     */
    if ((cv = cv_new(CGV_STRING)) == NULL){
	clicon_err(OE_CFG, errno, "cv_new");
	goto done;
    }

    if ((cv_name_set(cv, var)) == NULL) {
	clicon_err(OE_CFG, errno, "strdup");
	goto done;
    }

    /*
     * Call cb
     */
    if (dvp->dvp_valcb(dvp->dvp_arg, dvp->dvp_vars, cv, func, arg) < 0) {
	clicon_err(OE_CFG, errno, "Function call %s(%s) failed", func, 
		   argval ? argval : "");
	goto done;
    }
    
    if (clicon_dbvars_addcgv (dvp, var, cv))
	retval = 0;

 done:
    if (cv)
	cv_free(cv);
    if (arg)
	cv_free(arg);
    return retval;
}

%}

%%

variables: 
            /* empty */
        |
	variables variable {
	    dbvars_debug("variables->variables variable\n");
	}
	|
	variable {
	    dbvars_debug("variables->variable\n") ;
	}
	;

variable:
	'$' NAME WHITE {
	    dbvars_debug("dbvars: $%s\n", $2);
	    if (_YA->dvp_varidx < 1 || 
		_YA->dvp_varidx >= cvec_len(_YA->dvp_vars)) {
		clicon_err(OE_CFG, 0, "Invalid variable index %d",
			   _YA->dvp_varidx );
		YYERROR;
	    }
	    if (clicon_dbvars_addcgv(_YA, $2, 
			cvec_i(_YA->dvp_vars, _YA->dvp_varidx)) == NULL){	

		YYERROR;
	    }
	    else
		_YA->dvp_varidx++;
	    free($2);
	}
	|
	'$' NAME '=' '$' NAME WHITE {
	    dbvars_debug("dbvars: $%s=$%s\n", $2, $5);
	    if (clicon_dbvars_add_var(_YA, $2, $5) != 0)
		YYERROR;
	    free($2);
	    free($5);
	}	    
	|
	'$' NAME '=' STRING WHITE {
	    dbvars_debug("dbvars: $%s=\"%s\"", $2, $4);
	    if (clicon_dbvars_add_typed_var(_YA, $2, $4, "string") != 0)
		YYERROR;
	    free($2);
	    free($4);
	}
	|
	'$' NAME '=' '(' NAME ')' NAME WHITE {
	    dbvars_debug("dbvars: $%s=(%s)%s\n", $2, $5, $7);
	    if (clicon_dbvars_add_typed_var(_YA, $2, $7, $5) != 0)
		YYERROR;
	    free($2);
	    free($5);
	    free($7);
	}
	|
	'$' NAME '=' '(' NAME ')' STRING WHITE {
	    dbvars_debug("dbvars: $%s=(%s)%s\n", $2, $5, $7);
	    if (clicon_dbvars_add_typed_var(_YA, $2, $7, $5) != 0)
		YYERROR;
	    free($2);
	    free($5);
	    free($7);
	}
	|
	'$' NAME '=' NAME '(' ')' WHITE {
	    dbvars_debug("dbvars: $%s=%s()\n", $2, $4);
	    if (clicon_dbvars_varcb(_YA, $2, $4, NULL, NULL) != 0)
		YYERROR;
	    free($2);
	    free($4);
	}
	|
	'$' NAME '=' NAME '(' STRING ')' WHITE {
	    dbvars_debug("dbvars: $%s=%s()\n", $2, $4);
	    if (clicon_dbvars_varcb(_YA, $2, $4, "string", $6) != 0)
		YYERROR;
	    free($2);
	    free($4);
	    free($6);
	}
	|
	'$' NAME '=' NAME '(' '(' NAME ')' STRING ')' WHITE {
	    dbvars_debug("dbvars: $%s=%s((%s)%s)\n", $2, $4, $7, $9);
	    if (clicon_dbvars_varcb(_YA, $2, $4, $7, $9) != 0)
		YYERROR;
	    free($2);
	    free($4);
	    free($7);
	    free($9);
	}
	|
	'$' NAME '=' NAME '(' '(' NAME ')' NAME ')' WHITE {
	    dbvars_debug("dbvars: $%s=%s((%s)%s)\n", $2, $4, $7, $9);
	    if (clicon_dbvars_varcb(_YA, $2, $4, $7, $9) != 0)
		YYERROR;
	    free($2);
	    free($4);
	    free($7);
	    free($9);
	}
	;

%%
