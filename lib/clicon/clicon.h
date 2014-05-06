/*
 *  CVS Version: $Id: clicon.h,v 1.28 2013/08/31 06:38:22 benny Exp $
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

 * Meta-include file that includes all sub-files in control-lib
 * Note: this include files is for external purposes. Do not include this
 * file in clicon lib-routines.
 */

/* This include file requires the following include file dependencies */
#include <stdio.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>

#include <clicon/clicon_sig.h>

#include <clicon/clicon_log.h>
#include <clicon/clicon_err.h>
#include <clicon/clicon_queue.h>
#include <clicon/clicon_hash.h>
#include <clicon/clicon_handle.h>
#include <clicon/clicon_db.h>
#include <clicon/clicon_spec.h>
#include <clicon/clicon_lvalue.h>
#include <clicon/clicon_dbutil.h>
#include <clicon/clicon_dbmatch.h>
#include <clicon/clicon_chunk.h>
#include <clicon/clicon_event.h>
#include <clicon/clicon_lvmap.h>
#include <clicon/clicon_string.h>
#include <clicon/clicon_file.h>
#include <clicon/clicon_proto.h>
#include <clicon/clicon_proto_encode.h>
#include <clicon/clicon_proto_client.h>
#include <clicon/clicon_proc.h>
#include <clicon/clicon_options.h>
#include <clicon/clicon_xml.h>
#include <clicon/clicon_plugin.h>
#include <clicon/clicon_dbvars.h>
#include <clicon/clicon_db2txt.h>
#include <clicon/clicon_plugin.h>

#include <clicon/xmlgen_xf.h>
#include <clicon/xmlgen_xml.h>
#include <clicon/xmlgen_xsl.h>

/*
 * Global variables generated by Makefile
 */
extern const char CLICON_BUILDSTR[];
extern const char CLICON_VERSION[]; 
