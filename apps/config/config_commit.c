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
#include <pwd.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clicon/clicon.h>

#include "clicon_backend_api.h"
#include "config_lib.h"
#include "config_plugin.h"
#include "config_dbdiff.h"
#include "config_dbdep.h"
#include "config_handle.h"
#include "config_commit.h"

/*! A wrapper function for invoking the plugin dependency set/del call
 * for a changed a key value.
 * The routine logs on debug.
 * It also checks whether an error was properly registered using clicon_err().
 * Arguments:
 * @param  h          Clicon handle
 * @param  op         Operation: ADD,DELETE,CHANGE
 * @param  source_db  The database containing the original state
 * @param  target_db  The database containing the wanted state
 * @param  source_key The key in the source db. Only if OP = CO_DELETE, CO_CHANGE
 * @param  target_key The key in the target db. Only if OP = CO_ADD, CO_CHANGE
 * @param  source_vec Cligen variable vector for source key (if defined)
 * @param  target_vec Cligen variable vector for target key (if defined)
 * @param  dp:        plugin dependency information with function and argument pointers.
 *
 * Returns:
 * @retval  0: OK
 * @retval -1: An error occured in the plugin commit function. It is assumed that
 *     clicon_err() has been called there. Here, we interpret the clicon_err
 *     as a 'commit' error and does not handle it fatally.
 */
static int
plugin_commit_callback(clicon_handle h,
		       commit_op op,
		       char *source_db,
		       char *target_db,
		       char *source_key,
		       char *target_key,
		       cvec *source_vec,
		       cvec *target_vec,
		       dbdep_t *dp)
{
    int retval = -1;
    commit_data_t d;

    clicon_debug(2, "commit diff %c%s",
		 (op==CO_ADD)?'+':'-',
		 (op==CO_ADD) ? target_key : source_key);
    clicon_err_reset();

    /* populate commit data */
    memset(&d, 0, sizeof(d));
    d.source_db = source_db;
    d.target_db = target_db;
    d.source_key = source_key;
    d.target_key = target_key;
    d.source_vec = source_vec;
    d.target_vec = target_vec;
    d.arg = dp->dp_arg;


    if (dp->dp_callback(h, op, (commit_data)&d) < 0){
	if (!clicon_errno) 	/* sanity: log if clicon_err() is not called ! */
	    clicon_log(LOG_NOTICE, "%s: key: %c%s: callback does not make clicon_err call on error",
		       __FUNCTION__,
		       (op==CO_ADD)?'+':'-',
		       (op==CO_ADD) ? target_key : source_key);
	goto done;
    }
    retval = 0;
  done:
    return retval;
}

static int
generic_validate_yang(clicon_handle        h,
		      char                *dbname,
		      const struct dbdiff *dd,
		      yang_spec           *yspec)
{
    int             retval = -1;
    int             i, j;
    char           *dbkey;
    yang_stmt      *ys;
    yang_stmt      *ym; /* module */
    yang_stmt      *yleaf;
    cvec           *cvec = NULL;
    cg_var         *cv;
    char           *reason = NULL;

    /* dd->df_ents[].dfe_key1 (running),
       dd->df_ents[].dfe_key2 (candidate) */
    /* Get top-level module */
    if ((ym = yang_find((yang_node*)yspec, Y_MODULE, clicon_dbspec_name(h))) == NULL){
	clicon_err(OE_DB, 0, "No such module: %s", clicon_dbspec_name(h));
	goto done;
    }
    /* Loop through dbkeys that have changed on this commit */
    for (i = 0; i < dd->df_nr; i++) {
	/* Get the dbkey that changed (eg a.b) in a.b $x $y*/
        if (dd->df_ents[i].dfe_vec2 == NULL ||
	        (dbkey = cvec_name_get(dd->df_ents[i].dfe_vec2)) == NULL)
	    continue;
	/* Given changed dbkey, find corresponding yang syntax node
	   ie container or list. Should not be leaf or leaf-lists since they are vars
	*/
	if ((ys = dbkey2yang((yang_node*)ym, dbkey)) == NULL)
	    continue;
	/* Get the list of variables and values for this key, eg $x $y */
	if ((cvec = dbkey2cvec(dbname, dbkey)) == NULL)
	    goto done;
	/* Loop over all leafs and check default and mandatory settings */
	for (j=0; j<ys->ys_len; j++){
	    /* Get the leaf yang-stmt under a container/list, e.g $x */
	    yleaf = ys->ys_stmt[j];
	    if (yleaf->ys_keyword != Y_LEAF)
		continue;
	    if (cvec_find(cvec, yleaf->ys_argument) == NULL){  /* No db-value */
		/* If default value, set that */
		if (!cv_flag(yleaf->ys_cv, V_UNSET)){  /* Default value exists */
		    if (cvec_add_cv(cvec, yleaf->ys_cv) < 0){
			clicon_err(OE_CFG, 0, "cvec_add_cv");
			goto done;
		    }
		    /* Write to database */
		    if (cvec2dbkey(dbname, dbkey, cvec) < 0)
			goto done;
		}
		else
		    if (yang_mandatory(yleaf)){
			clicon_err(OE_CFG, 0,
				   "key %s: Missing mandatory variable: %s",
				   dbkey, yleaf->ys_argument);
			goto done;
		    }
		/* If mandatory a value is required */
	    }
	}
	/* Loop over all actual db/cv:s and check their validity, eg ranges and regexp */	
	cv = NULL;
	while ((cv = cvec_each(cvec, cv))) {
	    if ((yleaf = yang_find_specnode((yang_node*)ys, cv_name_get(cv))) == NULL)
		continue;
	    if (yleaf->ys_keyword != Y_LEAF && yleaf->ys_keyword != Y_LEAF_LIST)
		continue;
	    /* Validate this leaf */
	    if ((ys_cv_validate(cv, yleaf, &reason)) != 1){
		clicon_err(OE_DB, 0,
                          "key %s: validation of %s failed %s",
			   dbkey, yleaf->ys_argument, reason?reason:"");
		if (reason)
		    free(reason);
		goto done;
	    }

	}
	if (cvec){
	    cvec_free(cvec);
	    cvec = NULL;
	}
    } /* for dd */

    retval = 0;
  done:
    if (cvec)
	cvec_free(cvec);
    return retval;
}


/*! Key values are checked for validity independent of user-defined callbacks
 *
 * Key values are checked as follows:
 * 1. If no value and default value defined, add it.
 * 2. If no value and mandatory flag set in spec, report error.
 * 3. Validate value versus spec, and report error if no match. Currently only int ranges and
 *    string regexp checked.
 * See also db_lv_set() where defaults are also filled in. The case here for defaults
 * are if code comes via XML/NETCONF.
 * @param   h       Clicon handle
 * @param   dbname  Name of database, typically candidate
 * @param   dd      dbdiff structure containing a list of diffs of the two db:s.
 */
static int
generic_validate(clicon_handle h, char *dbname, const struct dbdiff *dd)
{
    yang_spec      *yspec;       /* yang spec */
    int             retval = -1;
    char            *dbspec_type;

    if ((dbspec_type = clicon_dbspec_type(h)) == NULL){
	clicon_err(OE_FATAL, 0, "Dbspec type not set");
	goto done;
    }
    if (strcmp(dbspec_type, "KEY") == 0 ||
	strcmp(dbspec_type, "YANG") == 0){     /* KEY or YANG syntax bot validate using YANG */
	if ((yspec = clicon_dbspec_yang(h)) == NULL){
	    clicon_err(OE_FATAL, 0, "No DB_SPEC");
	    goto done;
	}	
	if (generic_validate_yang(h, dbname, dd, yspec) < 0)
	    goto done;
    }
    else{
	clicon_err(OE_FATAL, 0, "Unknown dbspec format: %s", dbspec_type);
	goto done;
    }
    retval = 0;
  done:
    return retval;
}

char *
commitop2txt(commit_op op)
{
    switch (op){
    case CO_DELETE:
	return "DELETE";
	break;
    case CO_CHANGE:
	return "CHANGE";
	break;
    case CO_ADD:
	return "ADD";
	break;
    } 
    return NULL;
}

/*! Transalte from dbdiff operation to commit operation
 * XXX: Maybe they arecan be the same?
 */
static commit_op
dbdiff2commit_op(enum dbdiff_op dop)
{
    commit_op cop = 0;

    switch (dop){
    case DBDIFF_OP_FIRST:
	cop = CO_DELETE;
	break;
    case DBDIFF_OP_BOTH:
	cop = CO_CHANGE;
	break;
    case DBDIFF_OP_SECOND:
	cop = CO_ADD;
	break;
    } 
    return cop;
}


/*! Make user-defined callbacks on each changed keys
 * The order is: deleted keys, changed keys, added keys.
 */
static int
validate_db(clicon_handle h, int nvec, dbdep_dd_t *ddvec,
	    char *running, char *candidate)
{
    int                retval = -1;
    int                i;
    dbdep_t           *dp;
    struct dbdiff_ent *dfe;
    dbdep_dd_t        *dd;
    commit_op          op;

    for (i=0; i < nvec; i++){
        dd = &ddvec[i];
	dp = dd->dd_dep;     /* op, callback, arg */
	if ((dp->dp_type & TRANS_CB_VALIDATE) == 0)
	    continue;
	dfe = dd->dd_dbdiff; /* key1/key2/op */
	op = dbdiff2commit_op(dfe->dfe_op);
	if (plugin_commit_callback(h,
				   op,                      /* oper */
				   running,                 /* db1 */
				   candidate,               /* db2 */
				   dd->dd_mkey1,            /* key1 */
				   dd->dd_mkey2,            /* key2 */
				   dd->dd_dbdiff->dfe_vec1, /* vec1 */
				   dd->dd_dbdiff->dfe_vec2, /* vec2 */
				   dp                       /* callback */
				   ) < 0)
	    goto done;
    }
    retval = 0;
 done:
    return retval;
}

/*
 * candidate_commit
 * Do a diff between candidate and running, and then call plugins to
 * commit the changes.
 * The code reverts changes if the commit fails. But if the revert
 * fails, we just ignore the errors and proceed. Maybe we should
 * do something more drastic?
 * Arguments:
 * running:   The current database. The state of the router corresponds
 *            to these values. Also called db1.
 * candidate: The candidate database. We are aiming to put the router in this
 *            state.   Also called db2.

		       (_dp) [op, callback] (dpe_)
		       +---------------+    +---------------+
		       |    dbdep_t    |--> |  dbdep_ent_t  | [key, var]
		       +---------------+    +---------------+
		       ^ Database dependency description (static callback description)
 from dbdep_commit()   |
 +---------------+     |dd_dep
 |  dbdep_dd_t   |-----+
 +---------------+     |dd_dbdiff (what has changed?)
 (dd_)                 |
		       v
 +---------------+     +---------------+
 |   dbdiff      |---->|    dbdiff_ent |  [key1, key2, add/change/rm] (dfe_)
 +---------------+     +---------------+
 (df_) from dbdiff(),   (dfe_)

*/

int
candidate_commit(clicon_handle h, char *candidate, char *running)
{
    struct dbdiff      df = {0, };
    dbdep_t           *dp;
    struct dbdiff_ent *dfe;
    dbdep_dd_t        *ddvec = NULL;
    dbdep_dd_t        *dd;
    int                nvec;
    int                retval = -1;
    int                i, j;
    int                failed = 0;
    struct stat        sb;
    void              *firsterr = NULL;
    commit_op          op;

    /* Sanity checks that databases exists. */
    if (stat(running, &sb) < 0){
	clicon_err(OE_DB, errno, "%s", running);
	goto done;
    }
    if (stat(candidate, &sb) < 0){
	clicon_err(OE_DB, errno, "%s", candidate);
	goto done;
    }
    memset(&df, 0, sizeof(df));

    /* Find the differences between the two databases and store it in df vector. */
    if (db_diff(running, candidate,
		__FUNCTION__,
		clicon_dbspec_key(h),
		&df
		) < 0)
	goto done;
    /* 1. Get commit processing to dbdiff vector: one entry per key that changed.
       changes are registered as if they exist in the 1st(candidate) or
       2nd(running) dbs.
    */
    if (dbdep_commitvec(h, &df, &nvec, &ddvec) < 0)
	goto done;

    /* 2. Call plugin pre-commit hooks */
    if (plugin_begin_hooks(h, candidate) < 0)
	goto done;

    /* call generic cv_validate() on all new or changed keys. */
    if (generic_validate(h, candidate, &df) < 0)
	goto done;

    /* user-defined callbacks */
    if (validate_db(h, nvec, ddvec, running, candidate) < 0)
	goto done;

    /* Call plugin post-commit hooks */
    if (plugin_complete_hooks(h, candidate) < 0)
	goto done;

    if (clicon_commit_order(h) == 0){
	for (i=0; i < nvec; i++){ /* revert in opposite order */
	    dd = &ddvec[i];
	    dp = dd->dd_dep;     /* op, callback, arg */
	    if ((dp->dp_type & TRANS_CB_COMMIT) == 0)
		continue;
	    dfe = dd->dd_dbdiff; /* key1/key2/op */
	    op = dbdiff2commit_op(dfe->dfe_op);
	    if (plugin_commit_callback(h,
				       op,                      /* oper */
				       running,                 /* db1 */
				       candidate,               /* db2 */
				       dd->dd_mkey1,            /* key1 */
				       dd->dd_mkey2,            /* key2 */
				       dd->dd_dbdiff->dfe_vec1, /* vec1 */
				       dd->dd_dbdiff->dfe_vec2, /* vec2 */
				       dp                       /* callback */
				       ) < 0){
		firsterr = clicon_err_save(); /* save this error */
		failed++;
		break;
	    }
	}
	if (!failed)
	    if (file_cp(candidate, running) < 0){  /* Commit here in case cp fails */
		clicon_err(OE_UNIX, errno, "file_cp");
		failed++;
	    }
	/* Failed operation, start error handling: rollback in opposite order */
	if (failed){
	    for (j=i-1; j>=0; j--){ /* revert in opposite order */
		dd = &ddvec[j];
		dp = dd->dd_dep;     /* op, callback, arg */
		if ((dp->dp_type & TRANS_CB_COMMIT) == 0)
		    continue;
		dfe = dd->dd_dbdiff; /* key1/key2/op */
		op = dbdiff2commit_op(dfe->dfe_op);
		switch (op){ /* reverse operation */
		case CO_ADD:
		    op = CO_DELETE;
		    break;
		case CO_DELETE:
		    op = CO_ADD;
		    break;
		default:
		    break;
		}
		if (plugin_commit_callback(h,
					   op,                  /* oper */
					   candidate,               /* db1 */
					   running,                 /* db2 */
					   dd->dd_mkey2,            /* key1 */
					   dd->dd_mkey1,            /* key2 */
					   dd->dd_dbdiff->dfe_vec2, /* vec1 */
					   dd->dd_dbdiff->dfe_vec1, /* vec2 */
					   dp                       /* callback */
					   ) < 0){
		    /* ignore errors or signal major setback ? */
		    clicon_log(LOG_NOTICE, "Error in rollback, trying to continue");
		    continue; 
		}
	    }
	    goto done;
	} /* error handling */
    }
    else { /* commit_order == 1 or 2 */
	/* Now follows commit rules in order.
	 * 4. For all keys that are not in candidate but in running, delete key
	 * in reverse prio order
	 */
	for (i = nvec-1; i >= 0; i--){
	    dd = &ddvec[i];
	    dp = dd->dd_dep;     /* op, callback, arg */
	    if ((dp->dp_type & TRANS_CB_COMMIT) == 0)
		continue;
	    dfe = dd->dd_dbdiff; /* key1/key2/op */
	    op = dbdiff2commit_op(dfe->dfe_op);
	    /* original mode 2 where CHANGE=DEL/ADD */
	    if (clicon_commit_order(h) == 2 && op == CO_CHANGE)
		op = CO_DELETE;
	    if (op != CO_DELETE)
		continue;
	    if (plugin_commit_callback(h,
				       op,               /* oper */
				       running,                 /* db1 */
				       candidate,               /* db2 */
				       dd->dd_mkey1,            /* key1 */
				       dd->dd_mkey2,            /* key2 */
				       dd->dd_dbdiff->dfe_vec1, /* vec1 */
				       dd->dd_dbdiff->dfe_vec2, /* vec2 */
				       dp                       /* callback */
				       ) < 0){
		firsterr = clicon_err_save(); /* save this error */
		break;
	    }
	}
	/* 5. Failed deletion, add the key value back to running */
	if (i >= 0){ /* failed */
	    for (j=i+1; j<nvec; j++){ /* revert in opposite order */
		dd = &ddvec[j];
		dp = dd->dd_dep;     /* op, callback, arg */
		if ((dp->dp_type & TRANS_CB_COMMIT) == 0)
		    continue;
		dfe = dd->dd_dbdiff; /* key1/key2/op */
		op = dbdiff2commit_op(dfe->dfe_op);
		/* original mode 2 where CHANGE=DEL/ADD */
		if (clicon_commit_order(h) == 2 && op == CO_CHANGE)
		    op = CO_DELETE;
		if (op != CO_DELETE)
		    continue;
		if (plugin_commit_callback(h,
					   op,                  /* oper */
					   candidate,               /* db1 */
					   running,                 /* db2 */
					   dd->dd_mkey2,            /* key1 */
					   dd->dd_mkey1,            /* key2 */
					   dd->dd_dbdiff->dfe_vec2, /* vec1 */
					   dd->dd_dbdiff->dfe_vec1, /* vec2 */
					   dp                       /* callback */
					   ) < 0){
		    /* ignore errors or signal major setback ? */
		    clicon_log(LOG_NOTICE, "Error in rollback, trying to continue");
		    continue; 
		}
	    }
	    goto done;
	}
	/* 
	 * 6. For all added or changed keys
	 */
	for (i=0; i < nvec; i++){
	    dd = &ddvec[i];
	    dp = dd->dd_dep;     /* op, callback, arg */
	    if ((dp->dp_type & TRANS_CB_COMMIT) == 0)
		continue;
	    dfe = dd->dd_dbdiff; /* key1/key2/op */
	    op = dbdiff2commit_op(dfe->dfe_op);
	    if (op != CO_CHANGE && op != CO_ADD)
		continue;
	    /* original mode 2 where CHANGE=DEL/ADD */
	    if (clicon_commit_order(h) == 2 && op == CO_CHANGE)
		op = CO_ADD;
	    if (plugin_commit_callback(h,
				       op,                  /* oper */
				       running,                 /* db1 */
				       candidate,               /* db2 */
				       dd->dd_mkey1,            /* key1 */
				       dd->dd_mkey2,            /* key2 */
				       dd->dd_dbdiff->dfe_vec1, /* vec1 */
				       dd->dd_dbdiff->dfe_vec2, /* vec2 */
				       dp                       /* callback */
				       ) < 0){
		firsterr = clicon_err_save(); /* save this error */
		failed++;
		break;
	    }
	}
	if (!failed) /* Commit here in case cp fails */
	    if (file_cp(candidate, running) < 0){
		clicon_err(OE_UNIX, errno, "file_cp(candidate; running)");
		failed++;
	    }
	/* 10. Failed setting keys in running, first remove the keys set */
	if (failed){ /* failed */
	    for (j=i-1; j>=0; j--){ /* revert in opposite order */
		dd = &ddvec[j];
		dp = dd->dd_dep;     /* op, callback, arg */
		if ((dp->dp_type & TRANS_CB_COMMIT) == 0)
		    continue;
		dfe = dd->dd_dbdiff; /* key1/key2/op */
		op = dbdiff2commit_op(dfe->dfe_op);
		if (op != CO_CHANGE && op != CO_ADD)
		    continue;
		/* original mode 2 where CHANGE=DEL/ADD */
		if (clicon_commit_order(h) == 2 && op == CO_CHANGE)
		    op = CO_ADD;
		if (op == CO_ADD) /* reverse op */
		    op = CO_DELETE;
		if (plugin_commit_callback(h,
					   op,               /* oper */
					   candidate,               /* db1 */
					   running,                 /* db2 */
					   dd->dd_mkey2,            /* key1 */
					   dd->dd_mkey1,            /* key2 */
					   dd->dd_dbdiff->dfe_vec2, /* vec1 */
					   dd->dd_dbdiff->dfe_vec1, /* vec2 */
					   dp                       /* callback */
					   ) < 0){
		    /* ignore errors or signal major setback ? */
		    clicon_log(LOG_NOTICE, "Error in rollback, trying to continue");
		    continue; 
		}
	    }
	    for (j=0; j < nvec; j++){ /* revert in opposite order */
		dd = &ddvec[j];
		dp = dd->dd_dep;     /* op, callback, arg */
		if ((dp->dp_type & TRANS_CB_COMMIT) == 0)
		    continue;
		dfe = dd->dd_dbdiff; /* key1/key2/op */
		op = dbdiff2commit_op(dfe->dfe_op);
		/* original mode 2 where CHANGE=DEL/ADD */
		if (clicon_commit_order(h) == 2 && op == CO_CHANGE)
		    op = CO_DELETE;
		if (op != CO_DELETE)
		    continue;
		op = CO_ADD;
		if (plugin_commit_callback(h,
					   op,                  /* oper */
					   candidate,               /* db1 */
					   running,                 /* db2 */
					   dd->dd_mkey2,            /* key1 */
					   dd->dd_mkey1,            /* key2 */
					   dd->dd_dbdiff->dfe_vec2, /* vec1 */
					   dd->dd_dbdiff->dfe_vec1, /* vec2 */
					   dp                       /* callback */
					   ) < 0){
		    /* ignore errors or signal major setback ? */
		    clicon_log(LOG_NOTICE, "Error in rollback, trying to continue");
		    continue; 
		}
	    }
	    goto done;
	}
    } /* commit_order */
  
    /* Copy running back to candidate in case end functions triggered
       updates in running */
    if (file_cp(running, candidate) < 0){
	/* ignore errors or signal major setback ? */
	clicon_err(OE_UNIX, errno, "file_cp(running, candidate)");
	clicon_log(LOG_NOTICE, "Error in rollback, trying to continue");
	goto done;
    } 
    
	/* Call plugin post-commit hooks */
    plugin_end_hooks(h, candidate);
    
    retval = 0;
 done:
    if (retval < 0) /* Call plugin fail-commit hooks */
	plugin_abort_hooks(h, candidate);
    if (ddvec)
	dbdep_commitvec_free(ddvec, nvec);
    db_diff_free(&df);
    unchunk_group(__FUNCTION__);
    if (firsterr)
	clicon_err_restore(firsterr);
    return retval;
}

 int
 candidate_validate(clicon_handle h, char *candidate, char *running)
 {
     struct dbdiff      df = {0, };
     dbdep_dd_t        *ddvec = NULL;
     int                nvec;
     int                retval = -1;
     struct stat        sb;
     void              *firsterr = NULL;

     /* Sanity checks that databases exists. */
     if (stat(running, &sb) < 0){
	 clicon_err(OE_DB, errno, "%s", running);
	 goto done;
     }
     if (stat(candidate, &sb) < 0){
	 clicon_err(OE_DB, errno, "%s", candidate);
	 goto done;
     }
     memset(&df, 0, sizeof(df));

     /* Find the differences between the two databases and store it in df vector. */
     if (db_diff(running, candidate,
		 __FUNCTION__,
		 clicon_dbspec_key(h),
		 &df
	     ) < 0)
	 goto done;
     /* 1. Get commit processing dbdiff vector (df): one entry per key that
	changed. changes are registered as if they exist in the 1st(candidate)
	or 2nd(running) dbs.
      */
     if (dbdep_commitvec(h, &df, &nvec, &ddvec) < 0)
	 goto done;

     /* 2. Call plugin pre-commit hooks */
     if (plugin_begin_hooks(h, candidate) < 0)
	 goto done;

     /* call generic cv_validate() on all new or changed keys. */
     if (generic_validate(h, candidate, &df) < 0)
	 goto done;

     /* user-defined callbacks */
     if (validate_db(h, nvec, ddvec, running, candidate) < 0)
	 goto done;

     /* Call plugin post-commit hooks */
     if (plugin_complete_hooks(h, candidate) < 0)
	 goto done;

     retval = 0;
   done:
     if (retval < 0) /* Call plugin fail-commit hooks */
	 plugin_abort_hooks(h, candidate);
     if (ddvec)
	 free(ddvec);
     db_diff_free(&df);
     unchunk_group(__FUNCTION__);
     if (firsterr)
	 clicon_err_restore(firsterr);
     return retval;
 }


/*
 * from_client_commit
 * Handle an incoming commit message from a client.
 * XXX: If commit succeeds and snapshot/startup fails, we have strange state:
 *   the commit has succeeded but an error message is returned.
 */
int
from_client_commit(clicon_handle h,
		   int s,
		   struct clicon_msg *msg,
		   const char *label)
{
    int        retval = -1;
    char      *candidate;
    char      *running;
    uint32_t   snapshot;
    uint32_t   startup;
    char      *snapshot_0;
    char      *archive_dir;
    char      *startup_config;

    if (clicon_msg_commit_decode(msg, &candidate, &running,
				&snapshot, &startup, label) < 0)
	goto err;

    if (candidate_commit(h, candidate, running) < 0){
	clicon_debug(1, "Commit %s failed",  candidate);
	retval = 0; /* We ignore errors from commit, but maybe
		       we should fail on fatal errors? */
	goto err;
    }
    clicon_debug(1, "Commit %s",  candidate);
    if (snapshot){
	if ((archive_dir = clicon_archive_dir(h)) == NULL){
	    clicon_err(OE_PLUGIN, 0, "snapshot set and clicon_archive_dir not defined");
	    goto err;
	}
	if (config_snapshot(clicon_dbspec_key(h), running, archive_dir) < 0)
	    goto err;
    }

    if (startup){
	if ((archive_dir = clicon_archive_dir(h)) == NULL){
	    clicon_err(OE_PLUGIN, 0, "startup set but clicon_archive_dir not defined");
	    goto err;
	}
	if ((startup_config = clicon_startup_config(h)) == NULL){
	    clicon_err(OE_PLUGIN, 0, "startup set but startup_config not defined");
	    goto err;
	}
	snapshot_0 = chunk_sprintf(__FUNCTION__, "%s/0", archive_dir);
	if (file_cp(snapshot_0, startup_config) < 0){
	    clicon_err(OE_PROTO, errno, "%s: Error when creating startup",
		    __FUNCTION__);
		goto err;
	}
    }
    retval = 0;
    if (send_msg_ok(s) < 0)
	goto done;
    goto done;
  err:
    /* XXX: more elaborate errstring? */
    if (send_msg_err(s, clicon_errno, clicon_suberrno, "%s", clicon_err_reason) < 0)
	retval = -1;
  done:
    unchunk_group(__FUNCTION__);
    return retval; /* may be zero if we ignoring errors from commit */
} /* from_client_commit */



/*
 * Call backend plugin
 */
int
from_client_validate(clicon_handle h,
		     int s,
		     struct clicon_msg *msg,
		     const char *label)
{
    char *dbname;
    char *running_db;
    int retval = -1;

    if (clicon_msg_validate_decode(msg, &dbname, label) < 0){
	send_msg_err(s, clicon_errno, clicon_suberrno,
		     clicon_err_reason);
	goto err;
    }

    clicon_debug(1, "Validate %s",  dbname);
    if ((running_db = clicon_running_db(h)) == NULL){
	clicon_err(OE_FATAL, 0, "running db not set");
	goto err;
    }
    if (candidate_validate(h, dbname, running_db) < 0){
	clicon_debug(1, "Validate %s failed",  dbname);
	retval = 0; /* We ignore errors from commit, but maybe
		       we should fail on fatal errors? */
	goto err;
    }
    retval = 0;
    if (send_msg_ok(s) < 0)
	goto done;
    goto done;
  err:
    /* XXX: more elaborate errstring? */
    if (send_msg_err(s, clicon_errno, clicon_suberrno, "%s", clicon_err_reason) < 0)
	retval = -1;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
} /* from_client_validate */
