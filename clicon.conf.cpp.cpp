# Copyright (C) 2009-2015 Olof Hagsand and Benny Holmgren
#
# This file is part of CLICON.
#
# CLICON is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# CLICON is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with CLICON; see the file COPYING.  If not, see
# <http://www.gnu.org/licenses/>.
#
# CLICON options - Default values
# The origin of this file is run a _first_ time through a pre-processor at 
# clicon make install time causing autoconf constants (such as "prefix" and 
# "localstatedir") to be replaced with their installed values.
# It should be run a _second_ time as a part of installation of the application,
# in case clicon.mk is included in the application include file, and 
# "$(APPNAME).conf" rule is accessed.
# 
# See clicon_tutorial for more documentation

# Location of configuration-file for default values (this file)
CLICON_CONFIGFILE      sysconfdir/APPNAME.conf

# Database specification file syntax, Parse-tree or key. YANG(default) or KEY(db keys)
# CLICON_DBSPEC_TYPE      YANG

# Database specification file. Syntax either CLI or KEY given by CLICON_DBSPEC_TYPE
CLICON_DBSPEC_FILE     prefix/share/APPNAME/datamodel.spec

# Save values as XML in database instead of lvec:s.
# This is optimized for yang specified applications 
# But not compatible with key-based application (eg Rost)
# CLICON_DB_XML            0

# Location of YANG module and submodule files. Only if CLICON_DBSPEC_TYPE is YANG
CLICON_YANG_DIR        prefix/share/APPNAME/yang

# Option used to construct initial yang file:
#     <module>[@<revision>]
# This option is only relevant if CLICON_DBSPEC_TYPE is YANG
# CLICON_YANG_MODULE_MAIN clicon                

# Option used to construct initial yang file:
#     <module>[@<revision>]
# This option is only relevant if CLICON_DBSPEC_TYPE is YANG
CLICON_YANG_MODULE_REVISION

# Candidate qdbm database
CLICON_CANDIDATE_DB    localstatedir/APPNAME/candidate_db

# Running qdbm database
CLICON_RUNNING_DB      localstatedir/APPNAME/running_db

# Location of backend .so plugins
CLICON_BACKEND_DIR     libdir/APPNAME/backend

# Location of netconf (frontend) .so plugins
CLICON_NETCONF_DIR    libdir/APPNAME/netconf 

# Location of cli frontend .so plugins
CLICON_CLI_DIR        libdir/APPNAME/cli

# Location of frontend .cli cligen spec files
CLICON_CLISPEC_DIR    libdir/APPNAME/clispec

# Directory where to save configuration commit history (in XML). Snapshots
# are saved chronologically
CLICON_ARCHIVE_DIR      localstatedir/APPNAME/archive

# XXX Name of startup configuration file (in XML)
CLICON_STARTUP_CONFIG   localstatedir/APPNAME/startup-config

# Address family for communicating with clicon_backend (UNIX|IPv4|IPv6)
CLICON_SOCK_FAMILY  UNIX

# If family above is AF_UNIX: Unix socket for communicating with clicon_backend
# If family above is AF_INET: IPv4 address
CLICON_SOCK         localstatedir/APPNAME/APPNAME.sock

# Inet socket port for communicating with clicon_backend (only IPv4|IPv6)
CLICON_SOCK_PORT    4535

# Process-id file
CLICON_BACKEND_PIDFILE  localstatedir/APPNAME/APPNAME.pidfile

# Group membership to access clicon_config unix socket
# CLICON_SOCK_GROUP       clicon

# Set if all configuration changes are committed directly, commit command unnecessary
# CLICON_AUTOCOMMIT       0

# Clicon backend callback order. There are two dimensions: operation (del/add/change)
# and priority
# 0: all callbacks in (rising) priority order
# 1: first delete operations in declining prio order; then add/change in prio order
# 2: like (1) but CHANGE is replaced by (DEL;ADD)
# CLICON_COMMIT_ORDER 0

# Name of master plugin (both frontend and backend). Master plugin has special 
# callbacks for frontends. See clicon user manual for more info.
# CLICON_MASTER_PLUGIN    master

# Startup CLI mode. This should match the CLICON_MODE in your startup clispec file
# CLICON_CLI_MODE         base

# Generate code for CLI completion of existing db symbols. Add name="myspec" in 
# datamodel spec and reference as @myspec.
# CLICON_CLI_GENMODEL     1

# Generate code for CLI completion of existing db symbols
# CLICON_CLI_GENMODEL_COMPLETION 0

# How to generate and show CLI syntax: VARS|ALL
# CLICON_CLI_GENMODEL_TYPE   VARS

# Comment character in CLI
# CLICON_CLI_COMMENT      #

# Dont include keys in cvec in cli vars callbacks, ie a & k in 'a <b> k <c>' ignored
# CLICON_CLI_VARONLY      1



