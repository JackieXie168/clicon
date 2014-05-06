/*
 * CVS Version: $Id: cli_plugin.h,v 1.37 2013/09/05 20:14:50 olof Exp $
 *
  CVS Version: $Id: cli_plugin.h,v 1.37 2013/09/05 20:14:50 olof Exp $ 

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

#ifndef _CLI_PLUGIN_H_
#define _CLI_PLUGIN_H_

#include <stdio.h>
#include <inttypes.h>
#include <netinet/in.h>

/* clicon generic callback pointer */
typedef void (clicon_callback_t)(clicon_handle h);

/* clicon_set value callback */
typedef int (cli_valcb_t)(cvec *vars, cg_var *cgv, cg_var *arg);

/* specific to cli. For common see clicon_plugin.h */
/* Hook to get prompt format before each getline */
typedef char *(cli_prompthook_t)(clicon_handle, char *mode);

/* Ctrl-Z hook from getline() */   
typedef int (cli_susphook_t)(clicon_handle, char *, int, int *);

/* CLIgen parse failure hook. Retry other mode? */
typedef char *(cli_parsehook_t)(clicon_handle, char *, char *);

struct cli_syntax_mode {
    qelem_t csm_qelem;                                     /* List header */
    char csm_name[256];                               /* Syntax mode name */
    char csm_prompt[CLI_PROMPT_LEN];                   /* Prompt for mode */
    int csm_nsyntax;             /* Num syntax specs registered by plugin */
    parse_tree csm_pt;                               /* CLIgen parse tree */

};

/* A plugin list object */
struct cli_plugin {
    qelem_t cp_qelem;                                     /* List header */
    char cp_name[256];                                    /* Plugin name */
    void *cp_handle;                            /* Dynamic object handle */
};

/* Plugin group object */
struct cli_syntax_group {
    char cpg_name[256];        
    char cpg_dir[MAXPATHLEN];                          /* Plugin group dir */
    char cpg_cnklbl[128];                             /* Plugin group name */
    int cpg_nplugins;                                 /* Number of plugins */
    struct cli_plugin *cpg_plugins;                     /* List of plugins */
    int cpg_nmodes;                              /* Number of syntax modes */
    struct cli_syntax_mode *cpg_active_mode; /* Current active syntax mode */
    struct cli_syntax_mode *cpg_modes;             /* List of syntax modes */
    cli_prompthook_t *cpg_prompt_hook;                      /* Prompt hook */
    cli_parsehook_t *cpg_parse_hook;                    /* Parse mode hook */
    cli_susphook_t *cpg_susp_hook;           /* Ctrl-Z hook from getline() */
};


expand_cb *expand_str2fn(char *name, void *handle, char **error);

int cli_plugin_start(clicon_handle, int argc, char **argv);

int cli_plugin_init(clicon_handle h);

int clicon_eval(clicon_handle h, char *cmd, cg_obj *match_obj, cvec *vr);

int clicon_parse(clicon_handle h, char *cmd, char **mode, int *result);

int cli_plugin_unload_oldgroup(clicon_handle h);

char *clicon_cliread(clicon_handle h);

int cli_plugin_finish(clicon_handle h);

#endif  /* _CLI_PLUGIN_H_ */
