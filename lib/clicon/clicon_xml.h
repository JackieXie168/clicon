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

 * XML support functions.
 */
#ifndef _CLICON_XML_H
#define _CLICON_XML_H

/*
 * Types
 */
enum cxobj_type {CX_ERROR=-1, CX_ELMNT, CX_ATTR, CX_BODY};
#define CX_ANY CX_ERROR /* catch all and error is same */

typedef struct xml cxobj; /* struct defined in clicon_xml.c */

/*
 * Prototypes
 */
char     *xml_name(cxobj *xn);
int       xml_name_set(cxobj *xn, char *name);
char     *xml_namespace(cxobj *xn);
int       xml_namespace_set(cxobj *xn, char *name);
cxobj    *xml_parent(cxobj *xn);

int       xml_parent_set(cxobj *xn, cxobj *parent);
char     *xml_value(cxobj *xn);
int       xml_value_set(cxobj *xn, char *val);
char     *xml_value_append(cxobj *xn, char *val);
enum cxobj_type xml_type(cxobj *xn);
int       xml_type_set(cxobj *xn, enum cxobj_type type);
int       xml_index(cxobj *xn);
int       xml_index_set(cxobj *xn, int index);

int       xml_child_nr(cxobj *xn);
cxobj    *xml_child_i(cxobj *xn, int i);
cxobj    *xml_child_each(cxobj *xparent, cxobj *xprev,  enum cxobj_type type);

cxobj    *xml_new(char *name, cxobj *xn_parent);
cxobj    *xml_find(cxobj *xn_parent, char *name);

char     *xml_body(cxobj *xn);
char     *xml_find_value(cxobj *xn_parent, char *name);
char     *xml_find_body(cxobj *xn, char *name);

int       xml_free(cxobj *xn);
int       xml_prune(cxobj *xp, cxobj *xc, int freeit);
int       clicon_xml2file(FILE *f, cxobj *xn, int level, int prettyprint);
int       clicon_xml2cbuf(cbuf *xf, cxobj *xn, int level, int prettyprint);
int       clicon_xml_parse_file(int fd, cxobj **xml_top, char *endtag);
int       clicon_xml_parse_string(char **str, cxobj **xml_top);

int       xml_copy(cxobj *x0, cxobj *x1);
cxobj    *xml_dup(cxobj *x0);
int       xml_addsub(cxobj *xp, cxobj *xc);
cxobj    *xml_insert(cxobj *xt, char *tag);

#endif /* _CLICON_XML_H */
