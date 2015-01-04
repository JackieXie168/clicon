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

 * XML parser
 */
%union {
  char *string;
}

%start topxml

%token <string> NAME CHAR 
%token VER ENC
%token BSLASH ESLASH 
%token BTEXT ETEXT
%token BCOMMENT ECOMMENT 


%type <string> val aid

%lex-param     {void *_ya} /* Add this argument to parse() and lex() function */
%parse-param   {void *_ya}

%{

/* typecast macro */
#define _YA ((struct xml_parse_yacc_arg *)_ya)

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include "clicon_err.h"
#include "clicon_log.h"
#include "clicon_xml.h"
#include "clicon_xml_parse.h"

void 
clicon_xml_parseerror(void *_ya, char *s) 
{ 
  clicon_err(OE_XML, 0, "line %d: %s: at or before: %s", 
	      _YA->ya_linenum, s, clicon_xml_parsetext); 
  return;
}


static int
xml_attr_new(struct xml_parse_yacc_arg *ya, 
	     cxobj *xn, 
	     char *name, 
	     char *val)
{
    cxobj *xa; 

    if ((xa = xml_new(name, xn)) == NULL)
	return -1;
    xml_type_set(xa, CX_ATTR);
    if (xml_value_set(xa, val) < 0)
	return -1;
    return 0;
}

/* note that we dont handle escaped characters correctly 
   there may also be some leakage here on NULL return
 */
static int
xml_parse_content(struct xml_parse_yacc_arg *ya, char *str)
{
    int   sz;
    char  s0;
    cxobj *xn = ya->ya_xelement;
    cxobj *xp = ya->ya_xparent;
    int    retval = -1;

    ya->ya_xelement = NULL; /* init */
    s0 = str[0];
    if (xn != NULL){
	sz = strlen(xml_value(xn));
	if (s0 == ' ' || s0 == '\n' || s0 == '\t'){
	    str[0] = ' ';
	    if (xml_value(xn)[sz-1] == ' ')
		goto ok;
	}
    }
    else{
	if (s0 == ' ' || s0 == '\n' || s0 == '\t')
	    goto ok;
	if ((xn = xml_new("body", xp)) == NULL)
	    goto done; 
	xml_type_set(xn, CX_BODY);
	sz = 0;
    }
    if (xml_value_append(xn, str)==NULL)
	goto done; 
    ya->ya_xelement = xn;
  ok:
    retval = 0;
  done:
    return retval;
}

static int
xml_parse_version(struct xml_parse_yacc_arg *ya, char *ver)
{
    if(strcmp(ver, "1.0")){
	clicon_err(OE_XML, errno, "Wrong XML version %s expected 1.0\n", ver);
	free(ver);
	return -1;
    }
    free(ver);
    return 0;
}


static int
xml_parse_id(struct xml_parse_yacc_arg *ya, char *name, char *namespace)
{
    if ((ya->ya_xelement=xml_new(name, ya->ya_xparent)) == NULL) {
	if (namespace)
	    free(namespace);
	free(name);
	return -1;
    }
    xml_namespace_set(ya->ya_xelement, namespace);
    if (namespace)
	free(namespace);
    free(name);
    return 0;
}

static int
xml_parse_endslash_pre(struct xml_parse_yacc_arg *ya)
{
    ya->ya_xparent = ya->ya_xelement;
    ya->ya_xelement = NULL;
    return 0;
}

static int
xml_parse_endslash_mid(struct xml_parse_yacc_arg *ya)
{
    if (ya->ya_xelement != NULL)
	ya->ya_xelement = xml_parent(ya->ya_xelement);
    else
	ya->ya_xelement = ya->ya_xparent;
    ya->ya_xparent = xml_parent(ya->ya_xelement);
    return 0;
}

static int
xml_parse_endslash_post(struct xml_parse_yacc_arg *ya)
{
    ya->ya_xelement = NULL;
    return 0;
}

static int
xml_parse_bslash1(struct xml_parse_yacc_arg *ya, char *name)
{
    int retval = -1;

    if (strcmp(xml_name(ya->ya_xelement), name)){
	clicon_err(OE_XML, 0, "Sanity check failed: %s vs %s", 
		xml_name(ya->ya_xelement), name);
	goto done;
    }
    if (xml_namespace(ya->ya_xelement)!=NULL){
	clicon_err(OE_XML, 0, "Sanity check failed: %s:%s vs %s\n", 
		xml_namespace(ya->ya_xelement), xml_name(ya->ya_xelement), name);
	goto done;
    }
    retval = 0;
  done:
    free(name);
    return retval;
}

static int
xml_parse_bslash2(struct xml_parse_yacc_arg *ya, char *namespace, char *name)
{
    int retval = -1;

    if (strcmp(xml_name(ya->ya_xelement), name)){
	clicon_err(OE_XML, 0, "Sanity check failed: %s:%s vs %s:%s\n", 
		xml_namespace(ya->ya_xelement), 
		xml_name(ya->ya_xelement), 
		namespace, 
		name);
	goto done;
    }
    if (xml_namespace(ya->ya_xelement)==NULL ||
	strcmp(xml_namespace(ya->ya_xelement), namespace)){
	clicon_err(OE_XML, 0, "Sanity check failed: %s:%s vs %s:%s\n", 
		xml_namespace(ya->ya_xelement), 
		xml_name(ya->ya_xelement), 
		namespace, 
		name);
	goto done;
    }
    retval = 0;
  done:
    free(name);
    free(namespace);
    return retval;
}


static char*
xml_parse_ida(struct xml_parse_yacc_arg *ya, char *namespace, char *name)
{
    char *str;
    int len = strlen(namespace)+strlen(name)+2;

    if ((str=malloc(len)) == NULL)
	return NULL;
    snprintf(str, len, "%s:%s", namespace, name);
    free(namespace);
    free(name);
    return str;
}

static int
xml_parse_attr(struct xml_parse_yacc_arg *ya, char *id, char *val)
{
    int retval = -1;

    if (xml_attr_new(ya, ya->ya_xelement, id, val) < 0) 
	goto done;
    retval = 0;
  done:
    free(id); 
    free(val);
    return retval;
}

%} 
 
%%

topxml      : list
                    { clicon_debug(2, "topxml->list ACCEPT"); 
                      YYACCEPT; }
            | dcl list
	            { clicon_debug(2, "topxml->dcl list ACCEPT"); 
                      YYACCEPT; }
            ;

dcl         : BTEXT info encode ETEXT { clicon_debug(2, "dcl->info encode"); }
            ;

info        : VER '=' '\"' CHAR '\"' 
                 { if (xml_parse_version(_YA, $4) <0) YYABORT; }
            | VER '=' '\'' CHAR '\'' 
         	 { if (xml_parse_version(_YA, $4) <0) YYABORT; }
            |
            ;

encode      : ENC '=' '\"' CHAR '\"' {free($4);}
            | ENC '=' '\'' CHAR '\'' {free($4);}
            ;

emnt     : '<' id  attrs emnt1 
                   { clicon_debug(2, "emnt -> < id attrs emnt1"); }
	      ;

id          : NAME            { if (xml_parse_id(_YA, $1, NULL) < 0) YYABORT; 
                                clicon_debug(2, "id -> NAME");}
            | NAME ':' NAME   { if (xml_parse_id(_YA, $3, $1) < 0) YYABORT; 
                                clicon_debug(2, "id -> NAME : NAME");}
            ;

emnt1    :  ESLASH          {_YA->ya_xelement = NULL; 
                               clicon_debug(2, "emnt1 -> />");} 
            | '>'              { xml_parse_endslash_pre(_YA); }
              list             { xml_parse_endslash_mid(_YA); }
              etg             { xml_parse_endslash_post(_YA); 
                               clicon_debug(2, "emnt1 -> > list etg");} 
            ;

etg         : BSLASH NAME '>'          
                       { if (xml_parse_bslash1(_YA, $2) < 0) YYABORT; 
                         clicon_debug(2, "etg -> < </ NAME >"); }
            | BSLASH NAME ':' NAME '>' 
                       { if (xml_parse_bslash2(_YA, $2, $4) < 0) YYABORT; 
			 clicon_debug(2, "etg -> < </ NAME:NAME >"); }
            ;

list        : list content { clicon_debug(2, "list -> list content"); }
            | content      { clicon_debug(2, "list -> content"); }
            ;

content     : emnt         { clicon_debug(2, "content -> emnt"); }
            | comment      { clicon_debug(2, "content -> comment"); }
            | CHAR         { if (xml_parse_content(_YA, $1) < 0) YYABORT;  
                             clicon_debug(2, "content -> CHAR", $1); }
            ;

comment     : BCOMMENT ECOMMENT
            ;


attrs       : attrs att
            |
            ;


aid         : NAME   {$$ = $1;}
            | NAME ':' NAME  
                     { if (($$ = xml_parse_ida(_YA, $1, $3)) == NULL) YYABORT; }
            ;

att         : aid '=' val { if (xml_parse_attr(_YA, $1, $3) < 0) YYABORT; }
            ;

val         : '\"' CHAR '\"'   { $$=$2; /* $2 must be consumed */}
            | '\"'  '\"'       { $$=strdup(""); /* $2 must be consumed */}
            ;

%%

