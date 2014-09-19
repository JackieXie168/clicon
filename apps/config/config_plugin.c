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

 * 
 */

#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#define __USE_GNU /* strverscmp */
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <netinet/in.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clicon/clicon.h>

#include "clicon_backend_api.h"
#include "config_plugin.h"
#include "config_dbdiff.h"
#include "config_dbdep.h"


/* A plugin object object */
struct plugin {
    char	       p_name[PATH_MAX];       /* Plugin name */
    void	      *p_handle;	       /* Dynamic object handle */
    plgstart_t	      *p_start;		       /* Start */
    plgexit_t         *p_exit;		       /* Exit */
    plgreset_t	      *p_reset;		       /* Reset state */
    trans_begin_t     *p_begin;	               /* Pre commit hook */
    trans_complete_t  *p_complete;	       /* Validation complete */
    trans_end_t       *p_end;	               /* Post commit hook */
    trans_abort_t     *p_abort;	  
};
/* Plugins */
static int nplugins = 0;
static struct plugin *plugins = NULL;


/*
 * config_find_plugin
 * Find a plugin by name and return the dlsym handl
 * Used by libclicon code to find callback funcctions in plugins.
 */
static void *
config_find_plugin(clicon_handle h, char *plugin)
{
    int i;

    for (i = 0; i < nplugins; i++)
	if (strcmp(plugins[i].p_name, plugin) == 0)
	    return plugins[i].p_handle;
    
    return NULL;
}



/*
 * config_plugin_init
 *
 * initialize plugin code (not the plugins themselves)
 */
int
config_plugin_init(clicon_handle h)
{
    find_plugin_t *fp = config_find_plugin;
    clicon_hash_t *data = clicon_data(h);

    /* Register CLICON_FIND_PLUGIN in data hash */
    if (hash_add(data, "CLICON_FIND_PLUGIN", &fp, sizeof(fp)) == NULL) {
	clicon_err(OE_UNIX, errno, "failed to register CLICON_FIND_PLUGIN");
	return -1;
    }
	
    return 0;
}

/* 
 * Unload a plugin
 */
static void
plugin_unload(clicon_handle h, struct plugin *plg)
{
    char *error;

    /* Call exit function is it exists */
    if (plg->p_exit)
	plg->p_exit(h);
    
    dlerror();    /* Clear any existing error */
    if (dlclose(plg->p_handle) != 0) {
	error = (char*)dlerror();
	clicon_err(OE_UNIX, 0, "dlclose: %s", error?error:"Unknown error");
	/* Just report */
    }
    else 
	clicon_debug(1, "Plugin '%s' unloaded.", plg->p_name);
}


/*
 * Load a dynamic plugin object and call it's init-function
 */
static struct plugin *
plugin_load (clicon_handle h, char *file, int dlflags, const char *label)
{
    char *error;
    void *handle;
    char *name;
    struct plugin *new;
    int (*initfun)(void *);

    dlerror();    /* Clear any existing error */
    if ((handle = dlopen (file, dlflags)) == NULL) {
        error = (char*)dlerror();
	clicon_err(OE_UNIX, 0, "dlopen: %s", error?error:"Unknown error");
        return NULL;
    }
    
    initfun = dlsym(handle, "plugin_init");
    if ((error = (char*)dlerror()) != NULL) {
	clicon_err(OE_UNIX, 0, "dlsym: %s", error);
        return NULL;
    }

    if (initfun(h) != 0) {
	dlclose(handle);
	if (!clicon_errno) 	/* sanity: log if clicon_err() is not called ! */
	    clicon_err(OE_DB, 0, "Unknown error: %s: plugin_init does not make clicon_err call on error",
		       file);
        return NULL;
    }

    if ((new = chunk(sizeof(*new), label)) == NULL) {
	clicon_err(OE_UNIX, errno, "dhunk: %s", strerror(errno));
	dlclose(handle);
	return NULL;
    }
    memset(new, 0, sizeof(*new));
    name = strrchr(file, '/') ? strrchr(file, '/')+1 : file;
    snprintf(new->p_name, sizeof(new->p_name), "%*s",
	     (int)strlen(name)-2, name);
    new->p_handle = handle;
    new->p_start  = dlsym(handle, "plugin_start");
    new->p_exit   = dlsym(handle, "plugin_exit");
    new->p_reset  = dlsym(handle, "plugin_reset");
    new->p_begin  = dlsym(handle, "transaction_begin"); 
    new->p_complete = dlsym(handle, "transaction_complete");
    new->p_end    = dlsym(handle, "transaction_end"); 
    new->p_abort  = dlsym(handle, "transaction_abort");
    clicon_debug(2, "Plugin '%s' loaded.\n", name);

    return new;
}

/*
 * Request plugins to reset system state
 * The system 'state' should be the same as the contents of running_db
 */
int
plugin_reset_state(clicon_handle h, char *dbname)
{ 
    int i;

    for (i = 0; i < nplugins; i++)  {
	if (plugins[i].p_reset) {
	    clicon_debug(1, "Calling plugin_reset() for %s\n",
			 plugins[i].p_name);
	    if (((plugins[i].p_reset)(h, dbname)) < 0) {
		clicon_err(OE_FATAL, 0, "plugin_reset() failed for %s\n",
			   plugins[i].p_name);
		return -1;
	    }
	}
    }
    return 0;
}

/*
 * Call plugin_start in all plugins
 */
int
plugin_start_hooks(clicon_handle h, int argc, char **argv)
{
    int i;

    for (i = 0; i < nplugins; i++)  {
	if (plugins[i].p_start) {
	    optind = 0;
	    if (((plugins[i].p_start)(h, argc, argv)) < 0) {
		clicon_err(OE_FATAL, 0, "plugin_start() failed for %s\n",
			   plugins[i].p_name);
		return -1;
	    }
	}
    }
	
    return 0;
}

/* 
 * Append plugin to list
 */
static int
plugin_append(struct plugin *p)
{
    struct plugin *new;
    
    if ((new = rechunk(plugins, (nplugins+1) * sizeof (*p), NULL)) == NULL) {
	clicon_err(OE_UNIX, errno, "chunk");
	return -1;
    }
    
    memset (&new[nplugins], 0, sizeof(new[nplugins]));
    memcpy (&new[nplugins], p, sizeof(new[nplugins]));
    plugins = new;
    nplugins++;

    return 0;
}

static int
plugin_load_dir(clicon_handle h, const char *dir)
{
    int i;
    int np=0;
    int ndp;
    int retval = -1;
    struct stat st;
    char *filename;
    struct dirent *dp;
    struct plugin *new;
    struct plugin *p = NULL;
    char *master;

    /* Format master plugin path */
    master = chunk_sprintf(__FUNCTION__, "%s.so",  clicon_master_plugin(h));
    if (master == NULL) {
	clicon_err(OE_PLUGIN, errno, "chunk_sprintf master plugin");
	goto quit;
    }

    /* Allocate plugin group object */
    /* Get plugin objects names from plugin directory */
    if((ndp = clicon_file_dirent(dir, &dp, "(.so)$", S_IFREG, __FUNCTION__))<0)
	goto quit;
    
    /* reset num plugins */
    np = 0;

    /* Master plugin must be loaded first if it exists. */
    filename = chunk_sprintf(__FUNCTION__, "%s/%s", dir, master);
    if (filename == NULL) {
	clicon_err(OE_UNIX, errno, "chunk");
	goto quit;
    }
    if (stat(filename, &st) == 0) {
	clicon_debug(1, "Loading master plugin '%.*s' ...", 
		     (int)strlen(filename), filename);

	new = plugin_load(h, filename, RTLD_NOW|RTLD_GLOBAL, __FUNCTION__);
	if (new == NULL)
	    goto quit;
	if (plugin_append(new) < 0)
	    goto quit;
    }  

    /* Now load the rest */
    for (i = 0; i < ndp; i++) {
	if (strcmp(dp[i].d_name, master) == 0)
	    continue; /* Skip master now */
	filename = chunk_sprintf(__FUNCTION__, "%s/%s", dir, dp[i].d_name);
	clicon_debug(1, "Loading plugin '%.*s' ...",  (int)strlen(filename), filename);
	if (filename == NULL) {
	    clicon_err(OE_UNIX, errno, "chunk");
	    goto quit;
	}
	new = plugin_load (h, filename, RTLD_NOW, __FUNCTION__);
	if (new == NULL) 
	    goto quit;
	if (plugin_append(new) < 0)
	    goto quit;
    }
    
    /* All good. */
    retval = 0;
    
quit:
    if (retval != 0) {
	if (p) {
	    while (--np >= 0)
		plugin_unload (h, &p[np]);
	    unchunk(p);
	}
    }
    unchunk_group(__FUNCTION__);
    return retval;
}


/*
 * Load a plugin group.
 */
int
plugin_initiate(clicon_handle h)
{
    char *dir;

    /* First load CLICON system plugins */
    if (plugin_load_dir(h, CLICON_BACKEND_SYSDIR) < 0)
	return -1;

    /* Then load application plugins */
    if ((dir = clicon_backend_dir(h)) == NULL)
	return -1;
    if (plugin_load_dir(h, dir) < 0)
	return -1;
    
    return 0;
}


void
plugin_finish(clicon_handle h)
{
    int i;

    dbdeps_free(h);
    for (i = 0; i < nplugins; i++) 
	plugin_unload(h, &plugins[i]);
    if (plugins)
	unchunk(plugins);
    nplugins = 0;
}
    

/*
 * plugin_begin_hooks
 * Call plugin pre-commit hooks in plugins before a commit.
 * XXX We should only call plugins which have commit dependencies?
 */
int
plugin_begin_hooks(clicon_handle h, char *candidate)
{
    int i;
    int retval = 0;

    for (i = 0; i < nplugins; i++)  
	if (plugins[i].p_begin) 
	    if ((retval = (plugins[i].p_begin)(h)) < 0)
		break;
    return retval;
}

/*
 * Call transaction_complete() in all plugins after validation (and before commit)
 * Return -1 if validation fails. 
 * XXX We should only call plugins which have commit dependencies?
 */
int
plugin_complete_hooks(clicon_handle h, char *dbname)
{
    int i;
    int retval = 0;
    
    for (i = 0; i < nplugins; i++)  
	if (plugins[i].p_complete) 
	    if ((retval = (plugins[i].p_complete)(h)) < 0)
		break;
    return retval;
}


/*
 * Call plugin_post_commit() in all plugins after a successful commit.
 * transaction_end
 * XXX We should only call plugins which have commit dependencies?
 */
int
plugin_end_hooks(clicon_handle h, char *candidate)
{
    int i;
    int retval = 0;
    
    for (i = 0; i < nplugins; i++)  
	if (plugins[i].p_end) 
	    if ((retval = (plugins[i].p_end)(h)) < 0)
		break;
    return retval;
}

/*
 * Call plugin commit failed hooks in plugins
 * XXX We should only call plugins which have commit dependencies?
 */
int
plugin_abort_hooks(clicon_handle h, char *candidate)
{
    int i;
    int retval = 0;

    for (i = 0; i < nplugins; i++)  
	if (plugins[i].p_abort) 
	    (plugins[i].p_abort)(h); /* dont abort on error */
    return retval;
}

/*
 * Call from frontend to function 'func' in plugin 'plugin'. 
 * Plugin function is supposed to populate 'retlen' and 'retarg' where
 * 'retarg' is malloc:ed data if non-NULL.
 * 
 */
int
plugin_downcall(clicon_handle h, struct clicon_msg_call_req *req,
		uint16_t *retlen,  void **retarg)
{
    int i;
    int retval = -1;
    downcall_cb funcp;
    char name[PATH_MAX];
    char *error;

    for (i = 0; i < nplugins; i++)  {
	strncpy(name, plugins[i].p_name, sizeof(name)-1);
	if (!strcmp(name+strlen(name)-3, ".so"))
	    name[strlen(name)-3] = '\0';
	/* If no plugin is given or the plugin-name matches */
	if (req->cr_plugin == NULL || strlen(req->cr_plugin)==0 ||
	    strcmp(name, req->cr_plugin) == 0) {
	    funcp = dlsym(plugins[i].p_handle, req->cr_func);
	    if ((error = (char*)dlerror()) != NULL) {
		clicon_err(OE_PROTO, ENOENT,
			"Function does not exist: %s()", req->cr_func);
		return -1;
	    }
	    retval = funcp(h, req->cr_op, req->cr_arglen,  req->cr_arg, retlen, retarg);
	    goto done;
	}
    }
    clicon_err(OE_PROTO, ENOENT,"%s: %s(): Plugin does not exist: %s",
	       __FUNCTION__, req->cr_func, req->cr_plugin);
    return -1;
    
done:
    return retval;
}
