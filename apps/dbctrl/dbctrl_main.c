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

 */

#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clicon/clicon.h>

/* Command line options to be passed to getopt(3) */
#define DBCTRL_OPTS "hDf:s:ZipPd:r:a:m:n:"

/*
 * dump_database
 * Read registry, set machine state
 */
static int
dump_database(char *dbname, char *rxkey, int brief, dbspec_key *dbspec)
{
    int   retval = 0;
    int   npairs;
    struct db_pair *pairs;
    cvec *vr = NULL;
    dbspec_key *ds;
    
    /* Default is match all */
    if (rxkey == NULL)
	rxkey = "^.*$";

    /* Get all keys/values for vector */
    if ((npairs = db_regexp(dbname, rxkey, __FUNCTION__, &pairs, 0)) < 0)
        return -1;
    
    for (npairs--; npairs >= 0; npairs--) {

	fprintf(stdout, "%s\n", pairs[npairs].dp_key);
	if (!brief)
	    fprintf(stdout, "--------------------\n");
	if (brief)
	    continue;
	if(key_isvector_n(pairs[npairs].dp_key) ||
	   key_iskeycontent(pairs[npairs].dp_key)) {
	    printf("\ttype: number\tlen: %d\tdata: %d\n",
		   (int)sizeof(int),
		   *(int *)pairs[npairs].dp_val);
	}
	else{ 
	    if((vr = lvec2cvec(pairs[npairs].dp_val, pairs[npairs].dp_vlen)) == NULL){
		if ((ds = key2spec_key(dbspec, pairs[npairs].dp_key)) == NULL){
		    sanity_check_cvec(pairs[npairs].dp_key, ds, vr);
		    cvec_free (vr);
		}
	    }
	    if(lv_dump(stdout, pairs[npairs].dp_val, pairs[npairs].dp_vlen) < 0){
		retval = -1;
		break;
	    }
	}
	fprintf(stdout, "\n");
    }
    unchunk_group(__FUNCTION__);
    return retval;
}


/*
 * remove_entry
 */
static int
remove_entry(char *dbname, char *key)
{
  return db_del(dbname, key);
}

/*
 * usage
 */
static void
usage(char *argv0)
{
    fprintf(stderr, "usage:%s\n"
	    "where options are\n"
            "\t-h\t\tHelp\n"
            "\t-D\t\tDebug\n"
    	    "\t-f <file>\tCLICON config file\n"
    	    "\t-a <dir>\tSpecify application dir\n"
            "\t-d <dbname>\tDatabase name (default: running_db)\n"
	    "\t-s <file>\tSpecify db spec file\n"
    	    "\t-p\t\tDump database on stdout\n"
    	    "\t-P\t\tDump database on stdout (brief output)\n"
	    "\t-n \"<key> <var=%%T{value}> <var=...>\"\tAdd database entry\n"
            "\t-r <key>\tRemove database entry\n"
	    "\t-m <regexp key>\tMatch regexp key in database\n"
    	    "\t-Z\t\tDelete database\n"
    	    "\t-i\t\tInit database\n",
	    argv0
	    );
    exit(0);
}

int
main(int argc, char **argv)
{
    char             c;
    int              zapdb;
    int              initdb;
    int              dumpdb;
    int              addent;
    int              rment;
    int              matchent;
    char            *matchkey = NULL;
    char            *addstr;
    char             rmkey[MAXPATHLEN];
    int              brief;
    char             dbname[MAXPATHLEN] = {0,};
    clicon_handle    h;
    int              use_syslog;
    char            *dbspec_type;
    dbspec_key  *dbspec = NULL;

    /* In the startup, logs to stderr & debug flag set later */
    clicon_log_init(__PROGRAM__, LOG_INFO, CLICON_LOG_STDERR); 
    /* Create handle */
    if ((h = clicon_handle_init()) == NULL)
	return -1;

    /* Defaults */
    zapdb      = 0;
    initdb     = 0;
    dumpdb     = 0;
    matchent   = 0;
    addent     = 0;
    rment      = 0;
    brief      = 0;
    use_syslog = 0;
    addstr     = NULL;
    memset(rmkey, '\0', sizeof(rmkey));

    /* getopt in two steps, first find config-file before over-riding options. */
    while ((c = getopt(argc, argv, DBCTRL_OPTS)) != -1)
	switch (c) {
	case '?' :
	case 'h' : /* help */
	    usage(argv[0]);
	    break;
	case 'D' : /* debug */
	    debug = 1;	
	    break;
	case 'a': /* Register command line app-dir if any */
	    if (!strlen(optarg))
		usage(argv[0]);
	    clicon_option_str_set(h, "CLICON_APPDIR", optarg);
	    break;
	 case 'f': /* config file */
	    if (!strlen(optarg))
		usage(argv[0]);
	    clicon_option_str_set(h, "CLICON_CONFIGFILE", optarg);
	    break;
	 case 'S': /* Log on syslog */
	     use_syslog = 1;
	     break;
	}
    /* 
     * Logs, error and debug to stderr or syslog, set debug level
     */
    clicon_log_init(__PROGRAM__, debug?LOG_DEBUG:LOG_INFO, 
		    use_syslog?CLICON_LOG_SYSLOG:CLICON_LOG_STDERR); 
    clicon_debug_init(debug, NULL); 


    /* Find appdir. Find and read configfile */
    if (clicon_options_main(h, argc, argv) < 0)
	return -1;

    /* Default use running-db */
    strncpy(dbname, clicon_running_db(h), sizeof(dbname)-1);

    /* Now rest of options */   
    optind = 1;
    while ((c = getopt(argc, argv, DBCTRL_OPTS)) != -1)
	switch (c) {
	case 's':  /* db spec file */
	    if (!strlen(optarg))
		usage(argv[0]);
	    clicon_option_str_set(h, "CLICON_DBSPEC_FILE", optarg);
	    break;
	case 'Z': /* Zap database */
	    zapdb++;
	    break;
	case 'i': /* Init database */
	    initdb++;
	    break;
	case 'p': /* Dump/print database */
	    dumpdb++;
	    break;
	case 'P': /* Dump/print database  brief*/
	    dumpdb++;
	    brief++;
	    break;
	case 'd': /* dbname */
	    if (!optarg || sscanf(optarg, "%s", dbname) != 1)
	        usage(argv[0]);
	    break;
	case 'n': /* add database entry */
	  if (!optarg || !strlen(optarg) || (addstr = strdup(optarg)) == NULL)
	        usage(argv[0]);
	    addent++;
	    break;
	case 'r':
	     if (!optarg || sscanf(optarg, "%s", rmkey) != 1)
		 usage(argv[0]);
	     rment++;
	     break;
	case 'm':
	  if (!optarg || !strlen(optarg) || (matchkey = strdup(optarg)) == NULL)
	        usage(argv[0]);
	    matchent++;
	    break;
	default:
	    usage(argv[0]);
	    break;
	}
    argc -= optind;
    argv += optind;

    dbspec_type = clicon_dbspec_type(h);
    if (strcmp(dbspec_type, "YANG") == 0){ /* Parse YANG syntax */
	if (yang_spec_main(h, stdout, dumpdb, 0) < 0)
	    goto quit;
    }
    else
	if (strcmp(dbspec_type, "KEY") == 0){ /* Parse KEY syntax */
	    if (dbspec_key_main(h, stdout, dumpdb, 0) < 0)
		goto quit;	    
	}
	else{
	    clicon_err(OE_FATAL, 0, "Unknown dbspec format: %s", dbspec_type);
	    goto quit;
	}
    if (dumpdb)
        if (dump_database(dbname, NULL, brief, dbspec) < 0)
	    goto quit;

    if (matchent)
        if (dump_database(dbname, matchkey, brief, dbspec)) {
	    fprintf(stderr, "Match error\n");
	    goto quit;
	}
    if (addent) /* add entry */
	if (db_lv_op(dbspec, dbname, LV_SET, addstr, NULL) < 0){
	    fprintf(stderr, "Failed to add entry\n");
	    goto quit;
	}
    if (rment)
        if (remove_entry(dbname, rmkey) < 0)
	    goto quit;
    if (zapdb) /* remove databases */
	unlink(dbname);
    if (initdb)
	if (db_init(dbname) < 0)
	    goto quit;

  quit:
    db_spec_free(dbspec);
    clicon_handle_exit(h);
    return 0;
}
