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

 */


%start file

%union {
    int intval;
    char *string;
}

%token MY_EOF 
%token DQ           /* Double quote: " */
%token K_UNKNOWN    /* template for error */
%token <string> CHAR

%type <string>    ustring
%type <string>    qstrings
%type <string>    qstring
%type <string>    string
%type <string>    id_arg_str
%type <string>    config_arg_str
%type <string>    integer_value
%type <string>    identifier_ref_arg_str


/* rfc 6020 keywords 
   See also enum rfc_6020 in clicon_yang.h. There, the constants have Y_ prefix instead of K_
 * Wanted to unify these (K_ and Y_) but gave up for several reasons:
 * - Dont want to expose a generated yacc file to the API
 * - Cant use the symbols in this file because yacc needs token definitions
 */
%token K_ANYXML
%token K_ARGUMENT
%token K_AUGMENT
%token K_BASE
%token K_BELONGS_TO
%token K_BIT
%token K_CASE
%token K_CHOICE
%token K_CONFIG
%token K_CONTACT
%token K_CONTAINER
%token K_DEFAULT
%token K_DESCRIPTION
%token K_DEVIATE
%token K_DEVIATION
%token K_ENUM
%token K_ERROR_APP_TAG
%token K_ERROR_MESSAGE
%token K_EXTENSION
%token K_FEATURE
%token K_FRACTION_DIGITS
%token K_GROUPING
%token K_IDENTITY
%token K_IF_FEATURE
%token K_IMPORT
%token K_INCLUDE
%token K_INPUT
%token K_KEY
%token K_LEAF
%token K_LEAF_LIST
%token K_LENGTH
%token K_LIST
%token K_MANDATORY
%token K_MAX_ELEMENTS
%token K_MIN_ELEMENTS
%token K_MODULE
%token K_MUST
%token K_NAMESPACE
%token K_NOTIFICATION
%token K_ORDERED_BY
%token K_ORGANIZATION
%token K_OUTPUT
%token K_PATH
%token K_PATTERN
%token K_POSITION
%token K_PREFIX
%token K_PRESENCE
%token K_RANGE
%token K_REFERENCE
%token K_REFINE
%token K_REQUIRE_INSTANCE
%token K_REVISION
%token K_REVISION_DATE
%token K_RPC
%token K_STATUS
%token K_SUBMODULE
%token K_TYPE
%token K_TYPEDEF
%token K_UNIQUE
%token K_UNITS
%token K_USES
%token K_VALUE
%token K_WHEN
%token K_YANG_VERSION
%token K_YIN_ELEMENT


%lex-param     {void *_yy} /* Add this argument to parse() and lex() function */
%parse-param   {void *_yy}

%{
/* Here starts user C-code */

/* typecast macro */
#define _YY ((struct clicon_yang_yacc_arg *)_yy)

#define _YYERROR(msg) {clicon_debug(2, "YYERROR %s '%s' %d", (msg), clicon_yang_parsetext, _YY->yy_linenum); YYERROR;}

/* add _yy to error paramaters */
#define YY_(msgid) msgid 

#include "clicon_config.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>

#include <cligen/cligen.h>

#include "clicon_queue.h"
#include "clicon_hash.h"
#include "clicon_handle.h"
#include "clicon_dbspec_key.h"
#include "clicon_err.h"
#include "clicon_log.h"
#include "clicon_yang.h"
#include "clicon_yang_parse.h"

extern int clicon_yang_parseget_lineno  (void);

int 
clicon_yang_debug(int d)
{
    debug = d;
    return 0;
}

/* 
   clicon_yang_parseerror
   also called from yacc generated code *
*/
void 
clicon_yang_parseerror(void *_yy, char *s) 
{ 
    clicon_err(OE_YANG, 0, "%s on line %d: %s at or before: '%s'", 
	       _YY->yy_name,
	       _YY->yy_linenum ,
	       s, 
	       clicon_yang_parsetext); 
  return;
}

int
yang_parse_init(struct clicon_yang_yacc_arg *yy, yang_spec *ysp)
{
    return 0;
}


int
yang_parse_exit(struct clicon_yang_yacc_arg *yy)
{
    return 0;
}

int
ystack_pop(struct clicon_yang_yacc_arg *yy)
{
    struct ys_stack *ystack; 

    assert(ystack =  yy->yy_stack);
    yy->yy_stack = ystack->ys_next;
    free(ystack);
    return 0;
}

struct ys_stack *
ystack_push(struct clicon_yang_yacc_arg *yy, yang_node *yn)
{
    struct ys_stack *ystack; 

    if ((ystack = malloc(sizeof(*ystack))) == NULL) {
	clicon_err(OE_YANG, errno, "malloc");
	return NULL;
    }
    memset(ystack, 0, sizeof(*ystack));
    ystack->ys_node = yn;
    ystack->ys_next = yy->yy_stack;
    yy->yy_stack = ystack;
    return ystack;
}

/*! Add a yang statement to existing top-of-stack.
 *
 * Note: consumes 'argument' which assumes it is malloced and not freed by caller
 */
static yang_stmt *
ysp_add(struct clicon_yang_yacc_arg *yy, enum rfc_6020 keyword, char *argument)
{
    struct ys_stack *ystack = yy->yy_stack;
    yang_stmt       *ys = NULL;
    yang_node       *yn;
 
    ystack = yy->yy_stack;
    if (ystack == NULL){
	clicon_err(OE_YANG, errno, "No stack");
	goto err;
    }
    yn = ystack->ys_node;
    if ((ys = ys_new(keyword)) == NULL)
	goto err;
    /* NOTE: does not make a copy of string, ie argument is 'consumed' here */
    ys->ys_argument = argument;
    if (yn_insert(yn, ys) < 0) /* Insert into hierarchy */
	goto err; 
    if (ys_parse_sub(ys) < 0)     /* Check statement-specific syntax */
	goto err2; /* dont free since part of tree */
//  done:
    return ys;
  err:
    if (ys)
	ys_free(ys);
  err2:
    return NULL;
}

/*! combination of ysp_add and ysp_push for sub-modules */
static yang_stmt *
ysp_add_push(struct clicon_yang_yacc_arg *yy, int keyword, char *argument)
{
    yang_stmt *ys;

    if ((ys = ysp_add(yy, keyword, argument)) == NULL)
	return NULL;
    if (ystack_push(yy, (yang_node*)ys) == NULL)
	return NULL;
    return ys;
}

/* identifier-ref-arg-str has a [prefix :] id and prefix_id joins the id with an 
   optional prefix into a single string */
static char*
prefix_id_join(char *prefix, char *id)
{
    char *str;
    int   len;
    
    if (prefix){
	len = strlen(prefix) + strlen(id) + 2;
	if ((str = malloc(len)) == NULL){
	    clicon_err(OE_UNIX, errno, "malloc");
	    return NULL;
	}
	snprintf(str, len, "%s:%s", prefix, id);
	free(prefix);
	free(id);
    }
    else
	str = id;
    return str;
}


%} 
 
%%

/*
   statement = keyword [argument] (";" / "{" *statement "}") 
   The argument is a string
   recursion: right is wrong
   Let subststmt rules contain an empty rule, but not stmt rules
*/

file          : module_stmt MY_EOF
                       { clicon_debug(2,"file->module-stmt"); YYACCEPT; } 
              | submodule_stmt MY_EOF
                       { clicon_debug(2,"file->submodule-stmt"); YYACCEPT; } 
              ;

unknown_stmt  : K_UNKNOWN      { clicon_yang_parseerror(_yy, "unknown statement");clicon_debug(2,"unknown-stmt"); _YYERROR("0"); } 
              ;

/* module */
module_stmt   : K_MODULE id_arg_str 
                        { if ((_YY->yy_module = ysp_add_push(_yy, Y_MODULE, $2)) == NULL) _YYERROR("1");
                        } 
                '{' module_substmts '}' 
                        { if (ystack_pop(_yy) < 0) _YYERROR("2");
			  clicon_debug(2,"module -> id-arg-str { module-substmts }");} 
              ;

module_substmts : module_substmts module_substmt 
                      { clicon_debug(2,"module-substmts -> module-substmts module-substm");} 
              | module_substmt 
                      { clicon_debug(2,"module-substmts ->");} 
              ;

module_substmt : module_header_stmts { clicon_debug(2,"module-substmt -> module-header-stmts");}
               | linkage_stmts       { clicon_debug(2,"module-substmt -> linake-stmts");} 
               | meta_stmts          { clicon_debug(2,"module-substmt -> meta-stmts");} 
               | revision_stmts      { clicon_debug(2,"module-substmt -> revision-stmts");} 
               | body_stmts          { clicon_debug(2,"module-substmt -> body-stmts");} 
               | unknown_stmt        { clicon_debug(2,"module-substmt -> unknown-stmt");} 
               |                     { clicon_debug(2,"module-substmt ->");} 
               ;

/* submodule */
submodule_stmt : K_SUBMODULE id_arg_str '{' submodule_substmts '}' 
                      { if ((_YY->yy_module = ysp_add_push(_yy, Y_SUBMODULE, $2)) == NULL) _YYERROR("3"); 
                        clicon_debug(2,"submodule -> id-arg-str { submodule-stmts }"); 
                      }
              ;

submodule_substmts : submodule_substmts submodule_substmt 
                       { clicon_debug(2,"submodule-stmts -> submodule-substmts submodule-substmt"); }
              | submodule_substmt       
                       { clicon_debug(2,"submodule-stmts -> submodule-substmt"); }
              ;

submodule_substmt : submodule_header_stmts 
                              { clicon_debug(2,"submodule-substmt -> submodule-header-stmts"); }
               | linkage_stmts  { clicon_debug(2,"submodule-substmt -> linake-stmts");} 
               | meta_stmts     { clicon_debug(2,"submodule-substmt -> meta-stmts");} 
               | revision_stmts { clicon_debug(2,"submodule-substmt -> revision-stmts");} 
               | body_stmts     { clicon_debug(2,"submodule-stmt -> body-stmts"); }
               | unknown_stmt   { clicon_debug(2,"submodule-substmt -> unknown-stmt");} 
               |                { clicon_debug(2,"submodule-substmt ->");} 
              ;

/* module-header */
module_header_stmts : module_header_stmts module_header_stmt
                  { clicon_debug(2,"module-header-stmts -> module-header-stmts module-header-stmt"); }
              | module_header_stmt   { clicon_debug(2,"module-header-stmts -> "); }
              ;

module_header_stmt : yang_version_stmt 
                               { clicon_debug(2,"module-header-stmt -> yang-version-stmt"); }
              | namespace_stmt { clicon_debug(2,"module-header-stmt -> namespace-stmt"); }
              | prefix_stmt    { clicon_debug(2,"module-header-stmt -> prefix-stmt"); }
              ;

/* submodule-header */
submodule_header_stmts : submodule_header_stmts submodule_header_stmt
                  { clicon_debug(2,"submodule-header-stmts -> submodule-header-stmts submodule-header-stmt"); }
              | submodule_header_stmt   
                  { clicon_debug(2,"submodule-header-stmts -> submodule-header-stmt"); }
              ;

submodule_header_stmt : yang_version_stmt 
                  { clicon_debug(2,"submodule-header-stmt -> yang-version-stmt"); }
              ;

/* linkage */
linkage_stmts : linkage_stmts linkage_stmt 
                       { clicon_debug(2,"linkage-stmts -> linkage-stmts linkage-stmt"); }
              | linkage_stmt
                       { clicon_debug(2,"linkage-stmts -> linkage-stmt"); }
              ;

linkage_stmt  : import_stmt  { clicon_debug(2,"linkage-stmt -> import-stmt"); }
              | include_stmt { clicon_debug(2,"linkage-stmt -> include-stmt"); }
              ;

/* revision */
revision_stmts : revision_stmts revision_stmt 
                       { clicon_debug(2,"revision-stmts -> revision-stmts revision-stmt"); }
              | revision_stmt
                       { clicon_debug(2,"revision-stmts -> "); }
              ;

revision_stmt : K_REVISION string ';'  /* XXX date-arg-str */
                     { if (ysp_add(_yy, Y_REVISION, $2) == NULL) _YYERROR("4"); 
			 clicon_debug(2,"revision-stmt -> date-arg-str ;"); }
              | K_REVISION string 
                     { if (ysp_add_push(_yy, Y_REVISION, $2) == NULL) _YYERROR("5"); }
                '{' revision_substmts '}'  /* XXX date-arg-str */
                     { if (ystack_pop(_yy) < 0) _YYERROR("6");
		       clicon_debug(2,"revision-stmt -> date-arg-str { revision-substmts  }"); }
              ;

revision_substmts : revision_substmts revision_substmt 
                     { clicon_debug(2,"revision-substmts -> revision-substmts revision-substmt }"); }
              | revision_substmt
                     { clicon_debug(2,"revision-substmts -> }"); }
              ;

revision_substmt : description_stmt { clicon_debug(2,"revision-substmt -> description-stmt"); }
              | reference_stmt      { clicon_debug(2,"revision-substmt -> reference-stmt"); }
              | unknown_stmt        { clicon_debug(2,"revision-substmt -> unknown-stmt");} 
              |                     { clicon_debug(2,"revision-substmt -> "); }
              ;



/* meta */
meta_stmts    : meta_stmts meta_stmt { clicon_debug(2,"meta-stmts -> meta-stmts meta-stmt"); }
              | meta_stmt            { clicon_debug(2,"meta-stmts -> meta-stmt"); }
              ;

meta_stmt     : organization_stmt    { clicon_debug(2,"meta-stmt -> organization-stmt"); }
              | contact_stmt         { clicon_debug(2,"meta-stmt -> contact-stmt"); }
              | description_stmt     { clicon_debug(2,"meta-stmt -> description-stmt"); }
              | reference_stmt       { clicon_debug(2,"meta-stmt -> reference-stmt"); }
              ;

/* body */
body_stmts    : body_stmts body_stmt { clicon_debug(2,"body-stmts -> body-stmts body-stmt"); } 
              | body_stmt            { clicon_debug(2,"body-stmts -> body-stmt");}
              ;

body_stmt     : feature_stmt         { clicon_debug(2,"body-stmt -> feature-stmt");}
              | identity_stmt        { clicon_debug(2,"body-stmt -> identity-stmt");}
              | typedef_stmt         { clicon_debug(2,"body-stmt -> typedef-stmt");}
              | data_def_stmt        { clicon_debug(2,"body-stmt -> data-def-stmt");}

              ;

data_def_stmt : container_stmt       { clicon_debug(2,"data-def-stmt -> container-stmt");}
              | leaf_stmt            { clicon_debug(2,"data-def-stmt -> leaf-stmt");}
              | leaf_list_stmt       { clicon_debug(2,"data-def-stmt -> leaf-list-stmt");}
              | list_stmt            { clicon_debug(2,"data-def-stmt -> list-stmt");}
              ;

/* container */
container_stmt : K_CONTAINER id_arg_str ';'
                           { if (ysp_add(_yy, Y_CONTAINER, $2) == NULL) _YYERROR("7"); 
                             clicon_debug(2,"container-stmt -> CONTAINER id-arg-str ;");}
              | K_CONTAINER id_arg_str 
                          { if (ysp_add_push(_yy, Y_CONTAINER, $2) == NULL) _YYERROR("8"); }
                '{' container_substmts '}'
                           { if (ystack_pop(_yy) < 0) _YYERROR("9");
                             clicon_debug(2,"container-stmt -> CONTAINER id-arg-str { container-substmts }");}
              ;

container_substmts : container_substmts container_substmt 
              | container_substmt 
              ;

container_substmt : if_feature_stmt { clicon_debug(2,"container-substmt -> if-feature-stmt"); }
              | config_stmt         { clicon_debug(2,"container-substmt -> config-stmt"); }
              | status_stmt         { clicon_debug(2,"container-substmt -> status-stmt"); }
              | description_stmt    { clicon_debug(2,"container-substmt -> description-stmt");} 
              | reference_stmt      { clicon_debug(2,"container-substmt -> reference-stmt"); }
              | typedef_stmt        { clicon_debug(2,"container-substmt -> typedef-stmt"); }
              | data_def_stmt   { clicon_debug(2,"container-substmt -> data-def-stmt");} 
              | unknown_stmt        { clicon_debug(2,"container-substmt -> unknown-stmt");} 
              |                     { clicon_debug(2,"container-substmt ->");} 
              ;

/* leaf */
leaf_stmt     : K_LEAF id_arg_str ';'
                         { if (ysp_add(_yy, Y_LEAF, $2) == NULL) _YYERROR("10"); 
			   clicon_debug(2,"leaf-stmt -> LEAF id-arg-str ;");}
              | K_LEAF id_arg_str 
                          { if (ysp_add_push(_yy, Y_LEAF, $2) == NULL) _YYERROR("11"); }
                '{' leaf_substmts '}' 
                           { if (ystack_pop(_yy) < 0) _YYERROR("12");
                             clicon_debug(2,"leaf-stmt -> LEAF id-arg-str { lead-substmts }");}
              ;

leaf_substmts : leaf_substmts leaf_substmt
              | leaf_substmt
              ;

leaf_substmt  : if_feature_stmt      { clicon_debug(2,"leaf-substmt -> if-feature-stmt"); }
              | type_stmt            { clicon_debug(2,"leaf-substmt -> type-stmt"); }
              | units_stmt           { clicon_debug(2,"leaf-substmt -> units-stmt"); }
              | default_stmt         { clicon_debug(2,"leaf-substmt -> default-stmt"); }
              | config_stmt          { clicon_debug(2,"leaf-substmt -> config-stmt"); }
              | mandatory_stmt       { clicon_debug(2,"leaf-substmt -> mandatory-stmt"); }
              | status_stmt          { clicon_debug(2,"leaf-substmt -> status-stmt"); }
              | description_stmt     { clicon_debug(2,"leaf-substmt -> description-stmt"); }
              | reference_stmt       { clicon_debug(2,"leaf-substmt -> reference-stmt"); }
              | unknown_stmt         { clicon_debug(2,"leaf-substmt -> unknown-stmt");} 
              |                      { clicon_debug(2,"leaf-substmt ->"); }
              ;

/* leaf-list */
leaf_list_stmt : K_LEAF_LIST id_arg_str ';'
                         { if (ysp_add(_yy, Y_LEAF_LIST, $2) == NULL) _YYERROR("13"); 
			   clicon_debug(2,"leaf-list-stmt -> LEAF id-arg-str ;");}
              | K_LEAF_LIST id_arg_str 
                          { if (ysp_add_push(_yy, Y_LEAF_LIST, $2) == NULL) _YYERROR("14"); }
                '{' leaf_list_substmts '}'
                           { if (ystack_pop(_yy) < 0) _YYERROR("15");
                             clicon_debug(2,"leaf-list-stmt -> LEAF-LIST id-arg-str { lead-substmts }");}
              ;

leaf_list_substmts : leaf_list_substmts leaf_list_substmt
              | leaf_list_substmt
              ;

leaf_list_substmt : if_feature_stmt  { clicon_debug(2,"leaf-list-substmt -> if-feature-stmt"); }
              | type_stmt            { clicon_debug(2,"leaf-list-substmt -> type-stmt"); }
              | units_stmt           { clicon_debug(2,"leaf-list-substmt -> units-stmt"); }
              | config_stmt          { clicon_debug(2,"leaf-list-substmt -> config-stmt"); }
              | status_stmt          { clicon_debug(2,"leaf-list-substmt -> status-stmt"); }
              | description_stmt     { clicon_debug(2,"leaf-list-substmt -> description-stmt"); }
              | reference_stmt       { clicon_debug(2,"leaf-list-substmt -> reference-stmt"); }
              | unknown_stmt         { clicon_debug(2,"leaf-list-substmt -> unknown-stmt");} 
              |                      { clicon_debug(2,"leaf-list-stmt ->"); }
              ;

/* list */
list_stmt     : K_LIST id_arg_str ';' 
                         { if (ysp_add(_yy, Y_LIST, $2) == NULL) _YYERROR("16"); 
			   clicon_debug(2,"list-stmt -> LIST id-arg-str ;"); }
              | K_LIST id_arg_str 
                          { if (ysp_add_push(_yy, Y_LIST, $2) == NULL) _YYERROR("17"); }
	       '{' list_substmts '}' 
                           { if (ystack_pop(_yy) < 0) _YYERROR("18");
			     clicon_debug(2,"list-stmt -> LIST id-arg-str { list-substmts }"); }
              ;

list_substmts : list_substmts list_substmt 
                      { clicon_debug(2,"list-substmts -> list-substmts list-substmt"); }
              | list_substmt 
                      { clicon_debug(2,"list-substmts -> list-substmt"); }
              ;

list_substmt  : if_feature_stmt      { clicon_debug(2,"list-substmt -> if-feature-stmt"); }
              | key_stmt             { clicon_debug(2,"list-substmt -> key-stmt"); }
              | unique_stmt          { clicon_debug(2,"list-substmt -> unique-stmt"); }
              | config_stmt          { clicon_debug(2,"list-substmt -> config-stmt"); }
              | status_stmt          { clicon_debug(2,"list-substmt -> status-stmt"); }
              | description_stmt     { clicon_debug(2,"list-substmt -> description-stmt"); }
              | reference_stmt       { clicon_debug(2,"list-substmt -> reference-stmt"); }
              | typedef_stmt         { clicon_debug(2,"list-substmt -> typedef-stmt"); }
              | data_def_stmt        { clicon_debug(2,"list-substmt -> data-def-stmt"); }
              | unknown_stmt         { clicon_debug(2,"list-substmt -> unknown-stmt");} 
              |                      { clicon_debug(2,"list-substmt -> "); }
              ;

/* Typedef */
typedef_stmt  : K_TYPEDEF id_arg_str 
                          { if (ysp_add_push(_yy, Y_TYPEDEF, $2) == NULL) _YYERROR("19"); }
	       '{' typedef_substmts '}' 
                           { if (ystack_pop(_yy) < 0) _YYERROR("20");
			     clicon_debug(2,"typedef-stmt -> TYPEDEF id-arg-str { typedef-substmts }"); }
              ;

typedef_substmts : typedef_substmts typedef_substmt 
                      { clicon_debug(2,"typedef-substmts -> typedef-substmts typedef-substmt"); }
              | typedef_substmt 
                      { clicon_debug(2,"typedef-substmts -> typedef-substmt"); }
              ;

typedef_substmt : type_stmt          { clicon_debug(2,"typedef-substmt -> type-stmt"); }
              | units_stmt           { clicon_debug(2,"typedef-substmt -> units-stmt"); }
              | default_stmt         { clicon_debug(2,"typedef-substmt -> default-stmt"); }
              | status_stmt          { clicon_debug(2,"typedef-substmt -> status-stmt"); }
              | description_stmt     { clicon_debug(2,"typedef-substmt -> description-stmt"); }
              | reference_stmt       { clicon_debug(2,"typedef-substmt -> reference-stmt"); }
              | unknown_stmt         { clicon_debug(2,"typedef-substmt -> unknown-stmt");} 
              |                      { clicon_debug(2,"typedef-substmt -> "); }
              ;

/* Type */
type_stmt     : K_TYPE identifier_ref_arg_str ';' 
                         { if (ysp_add(_yy, Y_TYPE, $2) == NULL) _YYERROR("21"); 
			   clicon_debug(2,"type-stmt -> TYPE identifier-ref-arg-str ;");}
              | K_TYPE identifier_ref_arg_str 
                         { if (ysp_add_push(_yy, Y_TYPE, $2) == NULL) _YYERROR("22"); 
			 }
                '{' type_body_stmts '}'
                         { if (ystack_pop(_yy) < 0) _YYERROR("23");
                           clicon_debug(2,"type-stmt -> TYPE identifier-ref-arg-str { type-body-stmts }");}
              ;

/* type-body-stmts is a little special since it is a choice of
   sub-specifications that are all lists. One could model it as a list of 
   type-body-stmts and each individual specification as a simple.
 */
type_body_stmts : type_body_stmts type_body_stmt
                         { clicon_debug(2,"type-body-stmts -> type-body-stmts type-body-stmt"); }
              | type_body_stmt
                         { clicon_debug(2,"type-body-stmts -> type-body-stmt"); }
              ;

type_body_stmt/* numerical-restrictions */ 
              : range_stmt             { clicon_debug(2,"type-body-stmt -> range-stmt"); }
              /* decimal64-specification */ 
              | fraction_digits_stmt   { clicon_debug(2,"type-body-stmt -> fraction-digits-stmt"); }
              /* string-restrictions */ 
              | length_stmt           { clicon_debug(2,"type-body-stmt -> length-stmt"); }
              | pattern_stmt          { clicon_debug(2,"type-body-stmt -> pattern-stmt"); }
              /* enum-specification */ 
              | enum_stmt             { clicon_debug(2,"type-body-stmt -> enum-stmt"); }
              /* leafref-specifications */
              | path_stmt             { clicon_debug(2,"type-body-stmt -> path-stmt"); }
              | require_instance_stmt { clicon_debug(2,"type-body-stmt -> require-instance-stmt"); }
              /* identityref-specification */
              | base_stmt             { clicon_debug(2,"type-body-stmt -> base-stmt"); }
              /* instance-identifier-specification (see require-instance-stmt above */
              /* bits-specification */
              | bit_stmt               { clicon_debug(2,"type-body-stmt -> bit-stmt"); }
              /* union-specification */
              | type_stmt              { clicon_debug(2,"type-body-stmt -> type-stmt"); }
              ;

/* length-stmt */
length_stmt   : K_LENGTH string ';' /* XXX length-arg-str */
                         { if (ysp_add(_yy, Y_LENGTH, $2) == NULL) _YYERROR("24"); 
			   clicon_debug(2,"length-stmt -> LENGTH string ;"); }

              | K_LENGTH string
                          { if (ysp_add_push(_yy, Y_LENGTH, $2) == NULL) _YYERROR("25"); }
	       '{' length_substmts '}' 
                           { if (ystack_pop(_yy) < 0) _YYERROR("26");
			     clicon_debug(2,"length-stmt -> LENGTH string { length-substmts }"); }
              ;

length_substmts : length_substmts length_substmt 
                      { clicon_debug(2,"length-substmts -> length-substmts length-substmt"); }
              | length_substmt 
                      { clicon_debug(2,"length-substmts -> length-substmt"); }
              ;

length_substmt :description_stmt     { clicon_debug(2,"length-substmt -> description-stmt"); }
              | reference_stmt       { clicon_debug(2,"length-substmt -> reference-stmt"); }
              | unknown_stmt         { clicon_debug(2,"length-substmt -> unknown-stmt");} 
              |                      { clicon_debug(2,"length-substmt -> "); }
              ;

/* Pattern */
pattern_stmt  : K_PATTERN string ';' 
                         { if (ysp_add(_yy, Y_PATTERN, $2) == NULL) _YYERROR("27"); 
			   clicon_debug(2,"pattern-stmt -> PATTERN string ;"); }

              | K_PATTERN string
                          { if (ysp_add_push(_yy, Y_PATTERN, $2) == NULL) _YYERROR("28"); }
	       '{' pattern_substmts '}' 
                           { if (ystack_pop(_yy) < 0) _YYERROR("29");
			     clicon_debug(2,"pattern-stmt -> PATTERN string { pattern-substmts }"); }
              ;

pattern_substmts : pattern_substmts pattern_substmt 
                      { clicon_debug(2,"pattern-substmts -> pattern-substmts pattern-substmt"); }
              | pattern_substmt 
                      { clicon_debug(2,"pattern-substmts -> pattern-substmt"); }
              ;

pattern_substmt : reference_stmt       { clicon_debug(2,"pattern-substmt -> reference-stmt"); }

              | unknown_stmt         { clicon_debug(2,"pattern-substmt -> unknown-stmt");} 
              |                      { clicon_debug(2,"pattern-substmt -> "); }
              ;

/* Feature */
feature_stmt  : K_FEATURE id_arg_str ';' 
                         { if (ysp_add(_yy, Y_FEATURE, $2) == NULL) _YYERROR("30"); 
			   clicon_debug(2,"feature-stmt -> FEATURE id-arg-str ;"); }

              | K_FEATURE id_arg_str
                          { if (ysp_add_push(_yy, Y_FEATURE, $2) == NULL) _YYERROR("31"); }
	       '{' feature_substmts '}' 
                           { if (ystack_pop(_yy) < 0) _YYERROR("32");
			     clicon_debug(2,"feature-stmt -> FEATURE id-arg-str { feature-substmts }"); }
              ;

feature_substmts : feature_substmts feature_substmt 
                      { clicon_debug(2,"feature-substmts -> feature-substmts feature-substmt"); }
              | feature_substmt 
                      { clicon_debug(2,"feature-substmts -> feature-substmt"); }
              ;

feature_substmt : if_feature_stmt    { clicon_debug(2,"feature-substmt -> if-feature-stmt"); }
              | status_stmt          { clicon_debug(2,"feature-substmt -> status-stmt"); }
              | description_stmt     { clicon_debug(2,"feature-substmt -> description-stmt"); }
              | reference_stmt       { clicon_debug(2,"feature-substmt -> reference-stmt"); }
              | unknown_stmt         { clicon_debug(2,"feature-substmt -> unknown-stmt");} 
              |                      { clicon_debug(2,"feature-substmt -> "); }
              ;

/* Identity */
identity_stmt  : K_IDENTITY string ';' /* XXX identifier-arg-str */
                         { if (ysp_add(_yy, Y_IDENTITY, $2) == NULL) _YYERROR("33"); 
			   clicon_debug(2,"identity-stmt -> IDENTITY string ;"); }

              | K_IDENTITY string
                          { if (ysp_add_push(_yy, Y_IDENTITY, $2) == NULL) _YYERROR("34"); }
	       '{' identity_substmts '}' 
                           { if (ystack_pop(_yy) < 0) _YYERROR("35");
			     clicon_debug(2,"identity-stmt -> IDENTITY string { identity-substmts }"); }
              ;

identity_substmts : identity_substmts identity_substmt 
                      { clicon_debug(2,"identity-substmts -> identity-substmts identity-substmt"); }
              | identity_substmt 
                      { clicon_debug(2,"identity-substmts -> identity-substmt"); }
              ;

identity_substmt : base_stmt         { clicon_debug(2,"identity-substmt -> base-stmt"); }
              | status_stmt          { clicon_debug(2,"identity-substmt -> status-stmt"); }
              | description_stmt     { clicon_debug(2,"identity-substmt -> description-stmt"); }
              | reference_stmt       { clicon_debug(2,"identity-substmt -> reference-stmt"); }
              | unknown_stmt         { clicon_debug(2,"identity-substmt -> unknown-stmt");} 
              |                      { clicon_debug(2,"identity-substmt -> "); }
              ;

/* range-stmt */
range_stmt   : K_RANGE string ';' /* XXX range-arg-str */
                         { if (ysp_add(_yy, Y_RANGE, $2) == NULL) _YYERROR("36"); 
			   clicon_debug(2,"range-stmt -> RANGE string ;"); }

              | K_RANGE string
	                  { if (ysp_add_push(_yy, Y_RANGE, $2) == NULL) _YYERROR("37"); }
	       '{' range_substmts '}' 
                          { if (ystack_pop(_yy) < 0) _YYERROR("38");
			     clicon_debug(2,"range-stmt -> RANGE string { range-substmts }"); }
              ;

range_substmts : range_substmts range_substmt 
                      { clicon_debug(2,"range-substmts -> range-substmts range-substmt"); }
              | range_substmt 
                      { clicon_debug(2,"range-substmts -> range-substmt"); }
              ;

range_substmt : description_stmt     { clicon_debug(2,"range-substmt -> description-stmt"); }
              | reference_stmt       { clicon_debug(2,"range-substmt -> reference-stmt"); }
              | unknown_stmt         { clicon_debug(2,"range-substmt -> unknown-stmt");} 
              |                      { clicon_debug(2,"range-substmt -> "); }
              ;

/* enum-stmt */
enum_stmt     : K_ENUM string ';'
                         { if (ysp_add(_yy, Y_ENUM, $2) == NULL) _YYERROR("39"); 
			   clicon_debug(2,"enum-stmt -> ENUM string ;"); }

              | K_ENUM string
                         { if (ysp_add_push(_yy, Y_ENUM, $2) == NULL) _YYERROR("40"); }
	       '{' enum_substmts '}' 
                         { if (ystack_pop(_yy) < 0) _YYERROR("41");
			   clicon_debug(2,"enum-stmt -> ENUM string { enum-substmts }"); }
              ;

enum_substmts : enum_substmts enum_substmt 
                      { clicon_debug(2,"enum-substmts -> enum-substmts enum-substmt"); }
              | enum_substmt 
                      { clicon_debug(2,"enum-substmts -> enum-substmt"); }
              ;

enum_substmt  : value_stmt           { clicon_debug(2,"enum-substmt -> value-stmt"); }
              | status_stmt          { clicon_debug(2,"enum-substmt -> status-stmt"); }
              | description_stmt     { clicon_debug(2,"enum-substmt -> description-stmt"); }
              | reference_stmt       { clicon_debug(2,"enum-substmt -> reference-stmt"); }
              | unknown_stmt         { clicon_debug(2,"enum-substmt -> unknown-stmt");} 
              |                      { clicon_debug(2,"enum-substmt -> "); }
              ;

/* bit-stmt */
bit_stmt     : K_BIT string ';'
                         { if (ysp_add(_yy, Y_BIT, $2) == NULL) _YYERROR("41.1"); 
			   clicon_debug(2,"enum-stmt -> BIT string ;"); }

              | K_BIT string
                         { if (ysp_add_push(_yy, Y_ENUM, $2) == NULL) _YYERROR("41.2"); }
	       '{' bit_substmts '}' 
                         { if (ystack_pop(_yy) < 0) _YYERROR("41.3");
			   clicon_debug(2,"enum-stmt -> ENUM string { enum-substmts }"); }
              ;

bit_substmts : bit_substmts bit_substmt 
                      { clicon_debug(2,"bit-substmts -> bit-substmts bit-substmt"); }
              | bit_substmt 
                      { clicon_debug(2,"bit-substmts -> bit-substmt"); }
              ;

bit_substmt   : position_stmt        { clicon_debug(2,"bit-substmt -> positition-stmt"); }
              | status_stmt          { clicon_debug(2,"bit-substmt -> status-stmt"); }
              | description_stmt     { clicon_debug(2,"bit-substmt -> description-stmt"); }
              | reference_stmt       { clicon_debug(2,"bit-substmt -> reference-stmt"); }
              |                      { clicon_debug(2,"bit-substmt -> "); }
              ;

import_stmt   : K_IMPORT id_arg_str 
                          { if (ysp_add_push(_yy, Y_IMPORT, $2) == NULL) _YYERROR("54"); }
                '{' import_substmts '}' 
                        { if (ystack_pop(_yy) < 0) _YYERROR("2");
			  clicon_debug(2,"import-stmt -> IMPORT id-arg-str { import-substmts }");} 
              ;

import_substmts : import_substmts import_substmt 
                      { clicon_debug(2,"import-substmts -> import-substmts import-substm");} 
              | import_substmt 
                      { clicon_debug(2,"import-substmts ->");} 
              ;

import_substmt : prefix_stmt {  clicon_debug(2,"import-stmt -> prefix-stmt"); }
              |  revision_date_stmt {  clicon_debug(2,"import-stmt -> revision-date-stmt"); }
              ;


/* Simple statements */
yang_version_stmt  : K_YANG_VERSION string ';' /* XXX yang-version-arg-str */
                          { if (ysp_add(_yy, Y_YANG_VERSION, $2) == NULL) _YYERROR("42");
                            clicon_debug(2,"yang-version-stmt -> YANG-VERSION string"); }
              ;

fraction_digits_stmt : K_FRACTION_DIGITS string ';' /* XXX: fraction-digits-arg-str */
                          { if (ysp_add(_yy, Y_FRACTION_DIGITS, $2) == NULL) _YYERROR("43"); 
                            clicon_debug(2,"fraction-digits-stmt -> FRACTION-DIGITS string"); }
              ;

if_feature_stmt : K_IF_FEATURE identifier_ref_arg_str ';' 
                          { if (ysp_add(_yy, Y_IF_FEATURE, $2) == NULL) _YYERROR("44"); 
                            clicon_debug(2,"if-feature-stmt -> IF-FEATURE identifier-ref-arg-str"); }
              ;

value_stmt   : K_VALUE integer_value ';' 
                          { if (ysp_add(_yy, Y_VALUE, $2) == NULL) _YYERROR("45"); 
                            clicon_debug(2,"value-stmt -> VALUE integer-value"); }
              ;

position_stmt : K_POSITION integer_value ';' 
                          { if (ysp_add(_yy, Y_POSITION, $2) == NULL) _YYERROR("45"); 
                            clicon_debug(2,"position-stmt -> POSITION integer-value"); }
              ;

status_stmt   : K_STATUS string ';' /* XXX: status-arg-str */
                          { if (ysp_add(_yy, Y_STATUS, $2) == NULL) _YYERROR("46"); 
                            clicon_debug(2,"status-stmt -> STATUS string"); }
              ;

config_stmt   : K_CONFIG config_arg_str ';' 
                          { if (ysp_add(_yy, Y_CONFIG, $2) == NULL) _YYERROR("47"); 
                            clicon_debug(2,"config-stmt -> CONFIG config-arg-str"); }
              ;


base_stmt     : K_BASE identifier_ref_arg_str ';' 
                          { if (ysp_add(_yy, Y_BASE, $2)== NULL) _YYERROR("48"); 
                            clicon_debug(2,"base-stmt -> BASE identifier-ref-arg-str"); }
              ;

path_stmt     : K_PATH string ';' /* XXX: path-arg-str */
                          { if (ysp_add(_yy, Y_PATH, $2)== NULL) _YYERROR("49"); 
                            clicon_debug(2,"path-stmt -> PATH string"); }
              ;

require_instance_stmt : K_REQUIRE_INSTANCE string ';' /* XXX: require-instance-arg-str */
                          { if (ysp_add(_yy, Y_REQUIRE_INSTANCE, $2)== NULL) _YYERROR("50"); 
                            clicon_debug(2,"require-instance-stmt -> REQUIRE-INSTANCE string"); }
              ;

units_stmt    : K_UNITS string ';'
                          { if (ysp_add(_yy, Y_UNITS, $2)== NULL) _YYERROR("51"); 
                            clicon_debug(2,"units-stmt -> UNITS string"); }
              ;

default_stmt  : K_DEFAULT string ';'
                          { if (ysp_add(_yy, Y_DEFAULT, $2)== NULL) _YYERROR("52"); 
                            clicon_debug(2,"default-stmt -> DEFAULT string"); }
              ;

contact_stmt  : K_CONTACT string ';'
                          { if (ysp_add(_yy, Y_CONTACT, $2)== NULL) _YYERROR("53"); 
                            clicon_debug(2,"contact-stmt -> CONTACT string"); }
              ;

revision_date_stmt : K_REVISION_DATE string ';'  /* XXX date-arg-str */
                     { if (ysp_add(_yy, Y_REVISION_DATE, $2) == NULL) _YYERROR("4"); 
			 clicon_debug(2,"revision-date-stmt -> date;"); }
              ;

include_stmt  : K_INCLUDE id_arg_str ';'
                         { if (ysp_add(_yy, Y_INCLUDE, $2)== NULL) _YYERROR("56"); 
                           clicon_debug(2,"include-stmt -> id-arg-str"); }
              | K_INCLUDE id_arg_str '{' revision_date_stmt '}'
                         { if (ysp_add(_yy, Y_INCLUDE, $2)== NULL) _YYERROR("56"); 
                           clicon_debug(2,"include-stmt -> id-arg-str { revision-date-stmt }"); }
              ;

namespace_stmt : K_NAMESPACE string ';'  /* XXX uri-str */
                          { if (ysp_add(_yy, Y_NAMESPACE, $2)== NULL) _YYERROR("57"); 
                            clicon_debug(2,"namespace-stmt -> NAMESPACE string"); }
              ;

prefix_stmt   : K_PREFIX string ';' /* XXX prefix-arg-str */
                          { if (ysp_add(_yy, Y_PREFIX, $2)== NULL) _YYERROR("58"); 
			     clicon_debug(2,"prefix-stmt -> PREFIX string ;");}
              ;

description_stmt: K_DESCRIPTION string ';' 
                         { if (ysp_add(_yy, Y_DESCRIPTION, $2)== NULL) _YYERROR("59"); 
			   clicon_debug(2,"description-stmt -> DESCRIPTION string ;");}
              ;

organization_stmt: K_ORGANIZATION string ';'
                         { if (ysp_add(_yy, Y_ORGANIZATION, $2)== NULL) _YYERROR("60"); 
			   clicon_debug(2,"organization-stmt -> ORGANIZATION string ;");}
              ;

reference_stmt: K_REFERENCE string ';'
                         { if (ysp_add(_yy, Y_REFERENCE, $2)== NULL) _YYERROR("61"); 
			   clicon_debug(2,"reference-stmt -> REFERENCE string ;");}
              ;

mandatory_stmt: K_MANDATORY string ';' 
                         { yang_stmt *ys;
                           if ((ys = ysp_add(_yy, Y_MANDATORY, $2))== NULL) _YYERROR("62"); 
			   clicon_debug(2,"mandatory-stmt -> MANDATORY mandatory-arg-str ;");}
              ;

key_stmt      : K_KEY id_arg_str ';' /* XXX key_arg_str */
                         { if (ysp_add(_yy, Y_KEY, $2)== NULL) _YYERROR("63"); 
			   clicon_debug(2,"key-stmt -> KEY id-arg-str ;");}
              ;

unique_stmt   : K_UNIQUE id_arg_str ';' /* XXX key_arg_str */
                         { if (ysp_add(_yy, Y_KEY, $2)== NULL) _YYERROR("63"); 
			   clicon_debug(2,"key-stmt -> KEY id-arg-str ;");}
              ;

config_arg_str : string { $$=$1; } /* XXX BOOL */
              ;	      

integer_value : string { $$=$1; }
              ;	      

identifier_ref_arg_str : string 
                    {   if (($$=prefix_id_join(NULL, $1)) == NULL) _YYERROR("64");
			clicon_debug(2,"identifier-ref-arg-str -> string"); }
              | string ':' string 
                    {   if (($$=prefix_id_join($1, $3)) == NULL) _YYERROR("65");
			clicon_debug(2,"identifier-ref-arg-str -> prefix : string"); }
              ;

id_arg_str    : string { $$=$1; clicon_debug(2,"id-arg-str -> string"); }
              ;	      

string        : qstrings { $$=$1; clicon_debug(2,"string -> qstrings (%s)", $1); }
              | ustring  { $$=$1; clicon_debug(2,"string -> ustring (%s)", $1); }
              ;	      

/* quoted string */
qstrings      : qstrings '+' qstring
                     {
			 int len = strlen($1);
			 $$ = realloc($1, len + strlen($3) + 1); 
			 sprintf($$+len, "%s", $3);
			 free($3); 
		     }
              | qstring    
                     { $$=$1; clicon_debug(2,"qstrings-> \" qstring \""); } 
              ;

qstring        : DQ ustring DQ  { $$=$2; clicon_debug(2,"string-> \" ustring \""); } 
  ;

/* unquoted string */
ustring       : ustring CHAR 
                     {
			 int len = strlen($1);
			 $$ = realloc($1, len+strlen($2) + 1); 
			 sprintf($$+len, "%s", $2); 
			 free($2);
		     }
              | CHAR 
	             {$$=$1; } 
              ;



%%

