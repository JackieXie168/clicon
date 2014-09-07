/*

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
 * Operation:  XXXX NO LONGER VALID
 * 1. If not startup-config exists, then create an empty startup-config.
 * 2. Parse startup-config into current-db
 * 3. Initialize router with current-db. 
 * XXX: What about if we re-start osr_config?
 * 4. Wait for commit events from clients (eg cli instances)
 * 5. For every such event, make diff and exec the differences.
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
#include <ifaddrs.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/types.h>
#include <grp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clicon/clicon.h>

#include "clicon_backend_api.h"
#include "config_lib.h"
#include "config_socket.h"
#include "config_client.h"
#include "config_commit.h"
#include "config_plugin.h"
#include "config_dbdiff.h"
#include "config_dbdep.h"
#include "config_handle.h"

/* Command line options to be passed to getopt(3) */
#define BACKEND_OPTS "hD:f:a:d:s:Fzu:P:1IRc::rg:"

static int
config_terminate(clicon_handle h)
{
    dbspec_key *dbspec;
    yang_spec      *yspec;
    char           *pidfile = clicon_backend_pidfile(h);
    char           *sockpath = clicon_sock(h);

    if ((dbspec = clicon_dbspec_key(h)) != NULL)
	db_spec_free(dbspec);
    if ((yspec = clicon_dbspec_yang(h)) != NULL)
	yspec_free(yspec);
    plugin_finish(h);
    if (pidfile)
	unlink(pidfile);   
    if (sockpath)
	unlink(sockpath);   

    backend_handle_exit(h);

    return 0;
}

/*
  config_sig_term
  Unlink pidfile and quit
*/
static void
config_sig_term(int arg)
{
    static int i=0;
    if (i++ == 0)
	clicon_log(LOG_NOTICE, "%s: %u Signal %d", 
		   __PROGRAM__, getpid(), arg);
//    exit(0); /* XXX: should not exit here, but it hangs sometimes */
}

/*
 * usage
 */
static void
usage(char *argv0, clicon_handle h)
{
    char *conffile = clicon_configfile(h);
    char *appdir   = clicon_appdir(h);
    char *plgdir   = clicon_backend_dir(h);
    char *dbspec   = clicon_dbspec_file(h);
    char *confsock = clicon_sock(h);
    char *confpid  = clicon_backend_pidfile(h);
    char *startup  = clicon_startup_config(h);
    char *group    = clicon_sock_group(h);

    fprintf(stderr, "usage:%s\n"
	    "where options are\n"
            "    -h\t\thelp\n"
    	    "    -D <level>\tdebug\n"
    	    "    -f <file>\tCLICON config file (default: %s)\n"
    	    "    -a <dir>\tSpecify application dir (default: %s)\n"
	    "    -d <dir>\tSpecify backend plugin directory (default: %s)\n"
	    "    -s <file>\tSpecify db spec file (default: %s)\n"
    	    "    -z\t\tKill other config daemon and exit\n"
    	    "    -F\t\tforeground\n"
    	    "    -1\t\tonce (dont wait for events)\n"
    	    "    -u <path>\tconfig UNIX domain path (default: %s)\n"
    	    "    -P <file>\tPid filename (default: %s)\n"
    	    "    -I\t\tInitialize running state database\n"
    	    "    -R\t\tCall plugin_reset() in plugins to reset system state\n"
	    "    -c [<file>]\tLoad specified application config. Default is\n"
	    "              \t\"CLICON_STARTUP_CONFIG\" = %s\n"
	    "    -r\t\tReplace application config. Used with -c\n"
	    "    -g <group>\tClient membership required to this group (default: %s)\n",
	    argv0,
	    conffile ? conffile : "none",
	    appdir ? appdir : "none",
	    plgdir ? plgdir : "none",
	    dbspec ? dbspec : "none",
	    confsock ? confsock : "none",
	    confpid ? confpid : "none",
	    startup ? startup : "none",
	    group ? group : "none"
	    );
    exit(0);
}

static int
zapold(clicon_handle h)
{
    char *pidfile = clicon_backend_pidfile(h);
    char *sockpath = clicon_sock(h);
    struct stat sb;

    (void)pidfile_check(pidfile, 1);
    fprintf(stderr, "Zap\n");
    if (lstat(pidfile, &sb) == 0)
	unlink(pidfile);   
    if (lstat(sockpath, &sb) == 0)
	unlink(sockpath);   
    return 0;
}

static int
rundb_main(clicon_handle h, char *running_db)
{
    if (unlink(running_db) != 0 && errno != ENOENT) {
	clicon_err(OE_UNIX, errno, "unlink");
	return -1;
    }
    if (db_init(running_db) < 0)
	return -1;
    
    return 0;
}

/*
 * appconf_main
 * initiziaze running-config from file appconf.
 * if replace is set, clean running first (in case it is non-empty)
 */
static int
appconf_main(clicon_handle h, char *appconf, char *running_db, int replace)
{
    char *tmp;
    int retval = -1;

    if ((tmp = clicon_tmpfile(__FUNCTION__)) == NULL)
	return -1;
    
    if (replace) {
	if (db_init(tmp) < 0)
	    goto catch;
    }
    else {
	if (file_cp(running_db, tmp) < 0){
	    clicon_err(OE_UNIX, errno, "file copy");
	    goto catch;
	}
    }

    if (load_xml_to_db(appconf, clicon_dbspec_key(h), tmp) < 0) 
	goto catch;
    
    if (candidate_commit(h, tmp, running_db) < 0)
	goto catch;

    retval = 0;
    
catch:
    unlink(tmp);
    unchunk_group(__FUNCTION__);

    return retval;
}


/*! Create backend server socket and register callback
 */
static int
server_socket(clicon_handle h)
{
    int ss;

    /* Open control socket */
    if ((ss = config_socket_init(h)) < 0)
	return -1;
    /* ss is a server socket that the clients connect to. The callback
       therefore accepts clients on ss */
    if (event_reg_fd(ss, config_accept_client, h, "server socket") < 0) {
	close(ss);
	return -1;
    }
    return ss;
}

static int
config_log_cb(int level, char *msg, void *arg)
{
    /* backend_notify() will go through all clients and see if any has registered "CLICON",
       and if so make a clicon_proto notify message to those clients.   */
    return backend_notify(arg, "CLICON", level, msg);
}

/*
 * cf cli_main.c: spec_main_cli()
 */
static int
dbspec_main_config(clicon_handle h, int printspec, int printalt)
{
    char            *dbspec_type;
    int              retval = -1;

    if ((dbspec_type = clicon_dbspec_type(h)) == NULL){
	clicon_err(OE_FATAL, 0, "Dbspec type not set");
	goto quit;
    }
    if (strcmp(dbspec_type, "YANG") == 0){ /* Parse YANG syntax */
	if (yang_spec_main(h, stdout, printspec, printalt) < 0)
	    goto quit;
    }
    else
	if (strcmp(dbspec_type, "KEY") == 0){ /* Parse KEY syntax */
	    if (dbspec_key_main(h, stdout, printspec, printalt) < 0)
		goto quit;	    
	}
	else{
	    clicon_err(OE_FATAL, 0, "Unknown dbspec format: %s", dbspec_type);
	    goto quit;
	}

    retval = 0;
  quit:
    return retval;
}


int
main(int argc, char **argv)
{
    char          c;
    int           zap;
    int           foreground;
    int           once;
    int           init_rundb;
    char         *running_db;
    char         *candidate_db;
    int           reset_state;
    int           replace_config = 0;
    char         *app_config = NULL;
    char         *config_group;
    char         *argv0 = argv[0];
    char         *tmp;
    struct stat   st;
    clicon_handle h;
    int           help = 0;

    /* In the startup, logs to stderr & syslog and debug flag set later */
    clicon_log_init(__PROGRAM__, LOG_INFO, CLICON_LOG_STDERR|CLICON_LOG_SYSLOG); 
    /* Initiate CLICON handle */
    if ((h = backend_handle_init()) == NULL)
	goto done;
    if (config_plugin_init(h) != 0) 
	goto done;

    foreground = 0;
    once = 0;
    zap = 0;
    init_rundb = 0;
    reset_state = 0;

    /*
     * Command-line options for appdir, config-file, debug and help
     */
    opterr = 0;
    optind = 1;
    while ((c = getopt(argc, argv, BACKEND_OPTS)) != -1)
	switch (c) {
	case '?':
	case 'h':
	    /* Defer the call to usage() to later. Reason is that for helpful
	       text messages, default dirs, etc, are not set until later.
	       But this measn that we need to check if 'help' is set before 
	       exiting, and then call usage() before exit.
	    */
	    help = 1; 
	    break;
	case 'D' : /* debug */
	    if (sscanf(optarg, "%d", &debug) != 1)
		usage(argv[0], h);
	    break;
	case 'a': /* Register command line app-dir if any */
	    if (!strlen(optarg))
		usage(argv[0], h);
	    clicon_option_str_set(h, "CLICON_APPDIR", optarg);
	    break;
	case 'f': /* config file */
	    if (!strlen(optarg))
		usage(argv[0], h);
	    clicon_option_str_set(h, "CLICON_CONFIGFILE", optarg);
	    break;
	}
    /* 
     * Syslogs also to stderr, but later turn stderr off in daemon mode. 
     * error only to syslog. debug to syslog
     */
    clicon_log_init(__PROGRAM__, debug?LOG_DEBUG:LOG_INFO, CLICON_LOG_STDERR|CLICON_LOG_SYSLOG); 
    clicon_debug_init(debug, NULL);

    /* Find appdir. Find and read configfile */
    if (clicon_options_main(h, argc, argv) < 0){
	if (help)
	    usage(argv[0], h);
	return -1;
    }

    /* Now run through the operational args */
    opterr = 1;
    optind = 1;
    while ((c = getopt(argc, argv, BACKEND_OPTS)) != -1)
	switch (c) {
	case 'D' : /* debug */
	case 'a' : /* appdir */
	case 'f': /* config file */
	    break; /* see above */
	case 'd':  /* Plugin directory */
	    if (!strlen(optarg))
		usage(argv[0], h);
	    clicon_option_str_set(h, "CLICON_BACKEND_DIR", optarg);
	    break;
	case 's':  /* db spec file */
	    if (!strlen(optarg))
		usage(argv[0], h);
	    clicon_option_str_set(h, "CLICON_DBSPEC_FILE", optarg);
	    break;
	case 'F' : /* foreground */
	    foreground = 1;
	    break;
	case '1' : /* Quit after reading database once - dont wait for events */
	    once = 1;
	    break;
	case 'z': /* Zap other process */
	    zap++;
	    break;
	 case 'u': /* config unix domain path */
	    if (!strlen(optarg))
		usage(argv[0], h);
	    clicon_option_str_set(h, "CLICON_SOCK", optarg);
	     break;
	 case 'P': /* pidfile */
	     clicon_option_str_set(h, "CLICON_BACKEND_PIDFILE", optarg);
	     break;
	 case 'I': /* Initiate running db */
	     init_rundb++;
	     break;
	 case 'R': /* Reset system state */
	     reset_state++;
	     break;
	 case 'c': /* Load application config */
	     app_config = optarg ? optarg : clicon_startup_config(h);
	     if (app_config == NULL) {
		 fprintf(stderr, "Option \"CLICON_STARTUP_CONFIG\" not set\n");
		 return -1;
	     }
	     break;
	 case 'r': /* Replace application config */
	     replace_config = 1;
	     break;
	 case 'g': /* config socket group */
	     clicon_option_str_set(h, "CLICON_SOCK_GROUP", optarg);
	     break;
	default:
	    usage(argv[0], h);
	    break;
	}

    argc -= optind;
    argv += optind;

    /* Defer: Wait to the last minute to print help message */
    if (help)
	usage(argv[0], h);

    /* Zap: just kill old demon */
    if (zap) {
	zapold(h);
	exit(0);
    }
    /* Sanity check: config group exists */
    if ((config_group = clicon_sock_group(h)) == NULL)
	return -1;

    if (group_name2gid(config_group, NULL) < 0){
	clicon_log(LOG_ERR, "'%s' does not seem to be a valid user group.\n" 
		"The config demon requires a valid group to create a server UNIX socket\n"
		"Define a valid CLICON_SOCK_GROUP in %s or via the -g option\n", 
		config_group, clicon_configfile(h));
	return -1;
    }

    /* Parse db spec file */
    if (dbspec_main_config(h, 0, 0) < 0)
	goto done;
    if ((running_db = clicon_running_db(h)) == NULL){
	clicon_err(OE_FATAL, 0, "running db not set");
	goto done;
    }
    if ((candidate_db = clicon_candidate_db(h)) == NULL){
	clicon_err(OE_FATAL, 0, "candidate db not set");
	goto done;
    }
    /* Init running db */
    if (init_rundb || (stat(running_db, &st) && errno == ENOENT))
	if (rundb_main(h, running_db) < 0)
	    goto done;

    /* Initialize plugins 
       (also calls plugin_init() and plugin_start(argc,argv) in each plugin */
    if (plugin_initiate(h) != 0) 
	goto done;
    
    /* Request plugins to reset system state */
    if (reset_state)
	if (plugin_reset_state(h) < 0)  
	    goto done;

    /* Call plugin_start */
    tmp = *(argv-1);
    *(argv-1) = argv0;
    if (plugin_start_hooks(h, argc+1, argv-1) < 0) 
	goto done;
    *(argv-1) = tmp;


    /* Have we specified a config file to load? */
    if (app_config)
	if (appconf_main(h, app_config, running_db, replace_config) < 0)
	    goto done;
    /* Initiate the shared candidate. Maybe we should not do this? */
    if (file_cp(running_db, candidate_db) < 0){
	clicon_err(OE_UNIX, errno, "FATAL: file_cp");
	goto done;
    }
    /* XXX Hack for now. Change mode so that we all can write. Security issue*/
    chmod(candidate_db, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);

    if (once)
	goto done;

    /* Daemonize and initiate logging. Note error is initiated here to make
       demonized errors OK. Before this stage, errors are logged on stderr 
       also */
    if (foreground==0){
	clicon_log_init(__PROGRAM__, debug?LOG_DEBUG:LOG_INFO, CLICON_LOG_SYSLOG); 
	if (daemon(0, 0) < 0){
	    fprintf(stderr, "config: daemon");
	    exit(0);
	}
    }

    /* Daemon already running? */
    if (pidfile_check(clicon_backend_pidfile(h), 0) != 0) 
	goto done;

    /* Register log notifications */
    if (clicon_log_register_callback(config_log_cb, h) < 0)
	return -1;
    clicon_log(LOG_NOTICE, "%s: %u Started", __PROGRAM__, getpid());
    if (set_signal(SIGTERM, config_sig_term, NULL) < 0){
	clicon_err(OE_DEMON, errno, "Setting signal");
	return -1;
    }
    if (set_signal(SIGINT, config_sig_term, NULL) < 0){
	clicon_err(OE_DEMON, errno, "Setting signal");
	return -1;
    }
	
    /* Initialize server socket */
    if (server_socket(h) < 0)
	return -1;

    if (debug)
	clicon_option_dump(h, debug);

    if (event_loop() < 0)
	goto done;
  done:
    clicon_log(LOG_NOTICE, "%s: %u Terminated\n", __PROGRAM__, getpid());
    config_terminate(h); /* Cannot use h after this */

    return 0;
}
