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

 * XML XPATH and XSLT functions.
 */
#ifndef _XMLGEN_XSL_H
#define _XMLGEN_XSL_H

/*
 * Prototypes
 */
cxobj *xpath_first(cxobj *xn_top, char *xpath);
cxobj *xpath_each(cxobj *xn_top, char *xpath, cxobj *prev);
cxobj **xpath_vec(cxobj *xn_top, char *xpath, int *xv_len);

#endif /* _XMLGEN_XSL_H */
