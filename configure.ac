# To rebuild the `configure' script from this, execute the command
# 	autoconf
# in the directory containing this script.
#
#
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
#

AC_INIT(lib/clicon/clicon.h)

# Default CFLAGS unless set by environment.
: ${CFLAGS="-O2"}

AC_CONFIG_HEADER(include/clicon_config.h)
# clicon versions spread to Makefile's (.so files) and variable in build.c
AC_SUBST(CLICON_VERSION)
AC_SUBST(CLICON_VERSION_MAJOR)
AC_SUBST(CLICON_VERSION_MINOR)
AC_SUBST(CLIGEN_VERSION) # Bind to specific CLIgen version

CLICON_VERSION_MAJOR="3"
CLICON_VERSION_MINOR="0"
CLICON_VERSION_PATCH="1"
CLICON_VERSION="${CLICON_VERSION_MAJOR}.${CLICON_VERSION_MINOR}.${CLICON_VERSION_PATCH}"
# Fix to specific version (eg 3.5) or head (3) 
CLIGEN_VERSION="3.5" 

AC_MSG_RESULT(CLICON version is ${CLICON_VERSION}_PRE1)

AC_CANONICAL_TARGET
AC_SUBST(CC)
AC_SUBST(CFLAGS)
AC_SUBST(LDFLAGS)
AC_SUBST(INCLUDES)
AC_SUBST(CPPFLAGS)
AC_SUBST(LIBS)
AC_SUBST(OBJ_SUFFIX)
AC_SUBST(AR_SUFFIX)
AC_SUBST(SH_SUFFIX)
AC_SUBST(EXE_SUFFIX)
AC_SUBST(AR)
AC_SUBST(RANLIB)
AC_SUBST(APPDIR) # Clicon application director. Eg /hello. 
AC_SUBST(CLICON__LIBDIR) # Clicon application director. Eg /hello. 

# Some stuff installed in /usr/local/. Such as qdbm
LIBS="-L /usr/local/lib"

#
AC_PROG_CC()
AC_PROG_CPP

CPPFLAGS="-DHAVE_CONFIG_H ${CPPFLAGS}"

AC_MSG_RESULT(compiler is $CC)	

CFLAGS="${CFLAGS} -Wall" 

AC_MSG_RESULT(CPPFLAGS is $CPPFLAGS)	
AC_MSG_RESULT(CFLAGS is $CFLAGS)	

AC_PROG_YACC
AC_PROG_LEX
if test "$LEX" = ":"; then
   AC_MSG_ERROR(CLICON does not find lex or flex.)
fi
if test "$YACC" != "bison -y"; then
   AC_MSG_ERROR(CLICON does not find bison.  There are several problems with yacc and byacc. Please install bison.)
fi

if test "$prefix" = "NONE"; then
  APPDIR=${ac_default_prefix}/share/clicon
  CPPFLAGS="-I${ac_default_prefix}/include ${CPPFLAGS}"
  LDFLAGS="-L${ac_default_prefix}/lib ${LDFLAGS}"
else
  APPDIR=${prefix}/share/clicon
  CPPFLAGS="-I${prefix}/include ${CPPFLAGS}"
  LDFLAGS="-L${prefix}/lib ${LDFLAGS}"
fi
AC_ARG_WITH(appdir,
[  --with-appdir=DIR       Set application dir (default:$datarootdir/clicon)],
   APPDIR=$withval  
)
AH_TEMPLATE([APPDIR], 
[In-compiled clicon application dir, overruled by -a or env-variable])
AC_DEFINE_UNQUOTED(APPDIR, "$APPDIR")
AC_MSG_RESULT(APPDIR is $APPDIR)	


# Check Python
HAVE_PYTHON=none
AC_ARG_ENABLE(python,AS_HELP_STRING([--enable-python],[build the python plugin support]),[enable_python=yes])
if test  "$enable_python" = "yes"; then
  AM_PATH_PYTHON([2.7], [HAVE_PYTHON="python"])
  AC_ARG_VAR([PYTHON_INCLUDE], [Include flags for python, bypassing python-config])
  AC_ARG_VAR([PYTHON_CONFIG], [Path to python-config])
  AS_IF([test -z "$PYTHON_INCLUDE"], [
    AS_IF([test -z "$PYTHON_CONFIG"], [
      AC_PATH_PROGS([PYTHON_CONFIG],
                    [python$PYTHON_VERSION-config python-config],
                    [no],
                    [`dirname $PYTHON`])
      AS_IF([test "$PYTHON_CONFIG" = no], [AC_MSG_ERROR([cannot find python-config for $PYTHON.])])
    ])
    AC_MSG_CHECKING([python includes])
    PYTHON_INCLUDES=`$PYTHON_CONFIG --includes`
    AC_MSG_RESULT([$PYTHON_INCLUDES])
    AC_MSG_CHECKING([python libs])
    PYTHON_LIBS=`$PYTHON_CONFIG --libs`
    AC_MSG_RESULT([$PYTHON_LIBS])
  ])
fi
AC_SUBST(HAVE_PYTHON)
AC_SUBST(PYTHON_LIBS)
AC_SUBST(PYTHON_INCLUDES)


LDFLAGS=""
AC_CHECK_LIB(m, main)
EXE_SUFFIX=""
OBJ_SUFFIX=".o"
AR_SUFFIX=".a"
SH_SUFFIX=".so"
AR="ar"

# This is for cligen
AC_CHECK_HEADERS(cligen/cligen.h,, AC_MSG_ERROR(cligen missing. Try: git clone https://github.com/olofhagsand/cligen.git))

AC_CHECK_LIB(:libcligen.so.${CLIGEN_VERSION}, cligen_init,, AC_MSG_ERROR([CLIgen${CLIGEN_VERSION} missing. Try: git clone https://github.com/olofhagsand/cligen.git]))

# This is for qdbm.
# Problem: depot.h may be in qdbm/depot.h. 
AC_CHECK_HEADERS(depot.h,,[AC_CHECK_HEADERS(qdbm/depot.h,,AC_MSG_ERROR(libqdbm-dev required))])
AC_CHECK_LIB(qdbm, dpopen,, AC_MSG_ERROR(libqdbm-dev required))

AC_CHECK_LIB(crypt, crypt)
AC_CHECK_HEADERS(crypt.h)

# user credentials for unix sockets
AC_CHECK_HEADERS([sys/ucred.h],[],[],
     [[# include <sys/param.h>]]
)

# This is for Linux vlan code
AC_CHECK_HEADERS(linux/if_vlan.h)

AC_CHECK_LIB(socket, socket)
AC_CHECK_LIB(nsl, xdr_char)
AC_CHECK_LIB(dl, dlopen)

AC_CHECK_FUNCS(inet_aton sigaction sigvec strlcpy strsep strndup alphasort versionsort strverscmp)

# Check if extra keys inserted for database lists containing content. Eg A.n.foo = 3
# means A.3 $!a=foo exists

AC_ARG_ENABLE(keycontent, [  --disable-keycontent    Disable reverse lookup content keys],[
  if test "$enableval" = no; then
    ac_enable_keycontent=no
  else
    ac_enable_keycontent=yes
  fi
  ],[ ac_enable_keycontent=yes])

AH_TEMPLATE([DB_KEYCONTENT], 
[ Check if extra keys inserted for database lists containing content. 
  Eg A.n.foo = 3 means A.3 $!a=foo exists])
if test "$ac_enable_keycontent" = "yes"; then
   AC_DEFINE(DB_KEYCONTENT)
fi

AH_BOTTOM([#include <clicon_custom.h>])

AC_OUTPUT(Makefile
	  lib/Makefile
	  lib/src/Makefile 
	  lib/clicon/Makefile 
	  apps/Makefile 
	  apps/cli/Makefile 
	  apps/config/Makefile 
	  apps/netconf/Makefile
	  apps/dbctrl/Makefile
	  lang/Makefile
	  lang/python/Makefile
	  lang/python/lib/Makefile
	  lang/python/backend/Makefile
	  lang/python/cli/Makefile
	  include/Makefile
	  etc/Makefile
	  etc/cliconrc
	  examples/Makefile
	  examples/hello/Makefile
	  examples/hello/clicon.conf
	  examples/ntp/Makefile
	  examples/ntp/clicon.conf
	  examples/datamodel/Makefile
	  examples/datamodel/clicon.conf
	  examples/routing/Makefile
	  examples/clicon_yang/Makefile
	  doc/Makefile 
)

