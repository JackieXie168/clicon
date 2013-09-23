/*
 *  CVS Version: $Id: clicon_proto_encode.h,v 1.11 2013/08/01 09:15:46 olof Exp $
 *
  Copyright (C) 2009-2013 Olof Hagsand and Benny Holmgren
  Olof Hagsand
 *
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
 * Protocol to communicate with OSR config daemon
 */

#ifndef _CLICON_PROTO_ENCODE_H_
#define _CLICON_PROTO_ENCODE_H_

/*
 * Prototypes
 */ 
struct clicon_msg *
clicon_msg_commit_encode(char *dbsrc, char *dbdst, 
			uint32_t snapshot, uint32_t startup,
			const char *label); 

int
clicon_msg_commit_decode(struct clicon_msg *msg, 
			char **dbsrc, char **dbdst, 
			uint32_t *snapshot, uint32_t *startup,
			const char *label);

struct clicon_msg *
clicon_msg_validate_encode(char *db,
			  const char *label);

int
clicon_msg_validate_decode(struct clicon_msg *msg, char **db,
			const char *label);

struct clicon_msg *
clicon_msg_change_encode(char *db, uint32_t op, char *key, 
			char *lvec, uint32_t lvec_len, 
			const char *label);

int
clicon_msg_change_decode(struct clicon_msg *msg, 
			char **db, uint32_t *op, char **key, 
			char **lvec, uint32_t *lvec_len, 
			const char *label);

struct clicon_msg *
clicon_msg_save_encode(char *db, uint32_t snapshot, char *filename, 
		      const char *label);

int
clicon_msg_save_decode(struct clicon_msg *msg, 
		      char **db, uint32_t *snapshot, char **filename, 
		      const char *label);

struct clicon_msg *
clicon_msg_load_encode(int replace, char *db, char *filename, 
		       const char *label);

int
clicon_msg_load_decode(struct clicon_msg *msg, 
		       int *replace, char **db, char **filename, 
		       const char *label);
struct clicon_msg *
clicon_msg_initdb_encode(char *filename_src, const char *label);

int
clicon_msg_initdb_decode(struct clicon_msg *msg, char **filename_src,
		    const char *label);

struct clicon_msg *
clicon_msg_rm_encode(char *filename_src, const char *label);

int
clicon_msg_rm_decode(struct clicon_msg *msg, char **filename_src,
		    const char *label);

struct clicon_msg *
clicon_msg_copy_encode(char *filename_src, char *filename_dst,
		      const char *label);

int
clicon_msg_copy_decode(struct clicon_msg *msg, 
		      char **filename_src, char **filename_dst, 
		      const char *label);

struct clicon_msg *
clicon_msg_lock_encode(char *db, const char *label);

int
clicon_msg_lock_decode(struct clicon_msg *msg, char **db, const char *label);

struct clicon_msg *
clicon_msg_unlock_encode(char *db, const char *label);

int
clicon_msg_unlock_decode(struct clicon_msg *msg, char **db, const char *label);

struct clicon_msg *
clicon_msg_kill_encode(uint32_t session_id, const char *label);

int
clicon_msg_kill_decode(struct clicon_msg *msg, uint32_t *session_id, 
		      const char *label);

struct clicon_msg *
clicon_msg_debug_encode(uint32_t level, const char *label);

int
clicon_msg_debug_decode(struct clicon_msg *msg, uint32_t *level, 
		      const char *label);

struct clicon_msg *
clicon_msg_call_encode(uint16_t op, char *plugin, char *func,
		      uint16_t arglen, void *arg,
		      const char *label);

int
clicon_msg_call_decode(struct clicon_msg *msg, 
		      struct clicon_msg_call_req **req,
		      const char *label);

struct clicon_msg *clicon_msg_err_encode(uint32_t err, uint32_t suberr, 
					 char *reason, const char *label);

int clicon_msg_err_decode(struct clicon_msg *msg, uint32_t *err, uint32_t *suberr,
			  char **reason, const char *label);

#endif  /* _CLICON_PROTO_ENCODE_H_ */
