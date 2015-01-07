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

  * Note, this is a CLICON API file, only exprorted function prototypes should appear here
 */

#ifndef _CLICON_CLI_API_H_
#define _CLICON_CLI_API_H_

/*
 * Constants
 */
/* Max prompt length */
#define CLI_PROMPT_LEN 64
#define CLI_DEFAULT_PROMPT	">"

/*
 * Types
 */
//typedef void *cli_handle; /* clicon cli handle, see struct cli_handle */
enum candidate_db_type{
    CANDIDATE_DB_NONE,    /* No candidate */
    CANDIDATE_DB_PRIVATE, /* Create a private candidate_db */
    CANDIDATE_DB_SHARED,  /* Share the candidate with everyone else */
    CANDIDATE_DB_CURRENT  /* Dont create candidate, use current directly */
};


/* 
 * Function Declarations 
 */
/* cli_plugin.c */
int cli_set_syntax_mode(clicon_handle h, const char *mode);
char *cli_syntax_mode(clicon_handle h);
int cli_syntax_load(clicon_handle h);
int cli_handler_err(FILE *fd);
int cli_set_prompt(clicon_handle h, const char *mode, const char *prompt);
char *cli_prompt(char *fmt);
int cli_exec(clicon_handle h, char *cmd, char **mode, int *result);
int cli_ptpush(clicon_handle h, char *mode, char *string, char *op);
int cli_ptpop(clicon_handle h, char *mode, char *op);
clicon_dbvars_t *cli_set_parse(clicon_handle h, dbspec_key *spec, char *db, cvec *vars, const char *fmt);

/* cli_handle.c */
char cli_set_comment(clicon_handle h, char c);
char cli_comment(clicon_handle h);
int cli_set_exiting(clicon_handle h, int exiting);
int cli_exiting(clicon_handle h);
int cli_set_usedaemon(clicon_handle h, int usedaemon);
int cli_usedaemon(clicon_handle h);
clicon_handle cli_handle_init(void);
int cli_handle_exit(clicon_handle h);
cligen_handle cli_cligen(clicon_handle h);
enum candidate_db_type cli_candidate_type(clicon_handle h);
int cli_set_candidate_type(clicon_handle h, enum candidate_db_type type);

/* cli_common.c */
int init_candidate_db(clicon_handle h, enum candidate_db_type type);
int exit_candidate_db(clicon_handle h);
#define cli_output cligen_output
void cli_output_formatted (char *str);
void cli_fmt_output_cb(char *fmt, ...);
int cli_run (clicon_handle h, cvec *vars, cg_var *arg);
int cli_mset (clicon_handle h, cvec *vars, cg_var *arg);
int cli_set (clicon_handle h, cvec *vars, cg_var *arg);
int cli_mmerge (clicon_handle h, cvec *vars, cg_var *arg);
int cli_merge (clicon_handle h, cvec *vars, cg_var *arg);
int cli_del(clicon_handle h, cvec *vars, cg_var *argv);
int cli_show_lvmap(char *dbname, struct lvmap *lmap);
int cli_debug(clicon_handle h, cvec *vars, cg_var *argv);
int cli_record(clicon_handle h, cvec *vars, cg_var *argv);
int isrecording(void);
int record_command(char *str);
int cli_set_mode(clicon_handle h, cvec *vars, cg_var *argv);
int cli_start_shell(clicon_handle h, cvec *vars, cg_var *argv);
int cli_quit(clicon_handle h, cvec *vars, cg_var *arg);
int cli_commit(clicon_handle h, cvec *vars, cg_var *arg);
int cli_validate(clicon_handle h, cvec *vars, cg_var *arg);
int cli_downcall(clicon_handle h, uint16_t op, char *plugin, char *func,
	     void *param, uint16_t paramlen, 
	     char **ret, uint16_t *retlen,
	     const void *label
    );
int expand_dbvar(void *h, char *name, cvec *vars, cg_var *arg, 
		 int *nr, char ***commands, char ***helptexts);
int expand_dbvar_auto(void *h, char *name, cvec *vars, cg_var *arg, 
		  int *nr, char ***commands, char ***helptexts);
int expand_db_variable(clicon_handle h, char *dbname, char *basekey, char *variable, int *nr, char ***commands);
int expand_db_symbol(clicon_handle h, char *symbol, int element, int *nr, char ***commands);
int expand_dir(char *dir, int *nr, char ***commands, mode_t flags, int detail);
int compare_dbs(clicon_handle h, cvec *vars, cg_var *arg);
int cli_show_diff(clicon_handle h, char *db, char *db2, struct lvmap *lmap);

int load_config_file(clicon_handle h, cvec *vars, cg_var *arg);
int save_config_file(clicon_handle h, cvec *vars, cg_var *arg);
int delete_all(clicon_handle h, cvec *vars, cg_var *arg);
int discard_changes(clicon_handle h, cvec *vars, cg_var *arg);
int show_conf_as_xml(clicon_handle h, cvec *vars, cg_var *arg);
int show_conf_as_netconf(clicon_handle h, cvec *vars, cg_var *arg);
int show_conf_as_text(clicon_handle h, cvec *vars, cg_var *arg);
int show_conf_as_cli(clicon_handle h, cvec *vars, cg_var *arg);
int show_conf_as_csv(clicon_handle h, cvec *vars, cg_var *arg);
int cli_notification_register(clicon_handle h, char *stream, int status, 
			      int (*fn)(int, void*), void *arg);

#endif /* _CLICON_CLI_API_H_ */
