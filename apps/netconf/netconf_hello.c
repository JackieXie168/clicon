/*
 *  CVS Version: $Id: netconf_hello.c,v 1.12 2013/08/01 09:15:46 olof Exp $
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
 *  Code for handling netconf hello messages
 *****************************************************************************/
/*
 Capabilities are advertised in messages sent by each peer during
   session establishment.  When the NETCONF session is opened, each peer
   (both client and server) MUST send a <hello> element containing a
   list of that peer's capabilities.  Each peer MUST send at least the
   base NETCONF capability, "urn:ietf:params:netconf:base:1.0".
<hello> 
    <capabilities> 
        <capability>URI</capability> 
    </capabilities> 
    
</hello> 
 */


#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/param.h>
#include <assert.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clicon/clicon.h>

#include "netconf_lib.h"
#include "netconf_hello.h"

static int
netconf_hello(cxobj *xn)
{
    cxobj *x;

    x = NULL;
    while ((x = xpath_each(xn, "//capability", x)) != NULL) {
	//fprintf(stderr, "cap: %s\n", xml_body(x));
    }
    return 0;
}

int
netconf_hello_dispatch(cxobj *xn)
{
    cxobj *xp;
    int retval = -1;

    if ((xp = xpath_first(xn, "//hello")) != NULL)
	retval = netconf_hello(xp);
    return retval;
}

/*
 * netconf_create_hello
 * create capability string (once)
 */
int
netconf_create_hello(cbuf *xf,            /* msg buffer */
		     int session_id)
{
    int retval = 0;

    add_preamble(xf);
    cprintf(xf, "<hello>");
    cprintf(xf, "<capabilities>");
    cprintf(xf, "<capability>urn:ietf:params:xml:ns:netconf:base:1.0</capability>\n");
    cprintf(xf, "<capability>urn:ietf:params:xml:ns:netconf:capability:candidate:1:0</capability>\n");
    cprintf(xf, "<capability>urn:ietf:params:xml:ns:netconf:capability:validate:1.0</capability>\n");
   cprintf(xf, "<capability>urn:ietf:params:netconf:capability:xpath:1.0</capability>\n");
   cprintf(xf, "<capability>urn:ietf:params:netconf:capability:notification:1.0</capability>\n");


//    cprintf(xf, "<capability>urn:rnr:rnrapi:1:0</capability>");
    cprintf(xf, "</capabilities>");
    cprintf(xf, "<session-id>%lu</session-id>", 42+session_id);
    cprintf(xf, "</hello>");
    add_postamble(xf);
    return retval;
}
