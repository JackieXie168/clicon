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
 * Protocol to communicate between clients (eg clicon_cli, clicon_netconf) 
 * and server (clicon_backend)
 */

#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <syslog.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include "clicon_err.h"
#include "clicon_log.h"
#include "clicon_queue.h"
#include "clicon_chunk.h"
#include "clicon_sig.h"
#include "clicon_hash.h"
#include "clicon_handle.h"
#include "clicon_dbspec_key.h"
#include "clicon_lvalue.h"
#include "clicon_proto.h"
#include "clicon_proto_encode.h"

/* Generic encode/decode functions for exactly one C-string (str)
 */
static struct clicon_msg *
clicon_msg_1str_encode(char *str, enum clicon_msg_type op, const char *label)
{
    struct clicon_msg *msg;
    int hdrlen = sizeof(*msg);
    int len;
    int p;

    assert(str);
    p = 0;
    len = sizeof(*msg) + strlen(str) + 1;
    if ((msg = (struct clicon_msg *)chunk(len, label)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk", __FUNCTION__);
	return NULL;
    }
    memset(msg, 0, len);
    /* hdr */
    msg->op_type = op;
    msg->op_len = len;
    /* body */
    strncpy(msg->op_body+p, str, len-p-hdrlen);
    p += strlen(str)+1;
    return msg;
}

static int
clicon_msg_1str_decode(struct clicon_msg *msg, 
		      char **str, 
		      const char *label)
{
    int p;

    p = 0;
    /* body */
    if ((*str = chunk_sprintf(label, "%s", msg->op_body+p)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk_sprintf", 
		__FUNCTION__);
	return -1;
    }
    p += strlen(*str)+1;
    return 0;
}


struct clicon_msg *
clicon_msg_commit_encode(char *dbsrc, char *dbdst, 
			uint32_t snapshot, uint32_t startup,
			const char *label)
{
    struct clicon_msg *msg;
    int len;
    int hdrlen = sizeof(*msg);
    int p;
    uint32_t tmp;

    clicon_debug(2, "%s: snapshot: %d startup: %d dbsrc: %s dbdst: %s", 
	    __FUNCTION__, 
	    snapshot, startup, dbsrc, dbdst);
    p = 0;
    len = sizeof(*msg) + 2*sizeof(uint32_t) + strlen(dbsrc) + 1 + 
	strlen(dbdst) + 1;
    if ((msg = (struct clicon_msg *)chunk(len, label)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk", __FUNCTION__);
	return NULL;
    }
    memset(msg, 0, len);
    /* hdr */
    msg->op_type = CLICON_MSG_COMMIT;
    msg->op_len = len;
    /* body */
    tmp = htonl(snapshot);
    memcpy(msg->op_body+p, &tmp, sizeof(uint32_t));
    p += sizeof(uint32_t);
    tmp = htonl(startup);
    memcpy(msg->op_body+p, &tmp, sizeof(uint32_t));
    p += sizeof(uint32_t);
    strncpy(msg->op_body+p, dbsrc, len-p-hdrlen);
    p += strlen(dbsrc)+1;
    strncpy(msg->op_body+p, dbdst, len-p-hdrlen);
    p += strlen(dbdst)+1;
    return msg;
}

int
clicon_msg_commit_decode(struct clicon_msg *msg, 
			char **dbsrc, char **dbdst, 
			uint32_t *snapshot, uint32_t *startup,
			const char *label)
{
    int p;
    uint32_t tmp;

    p = 0;
    /* body */
    memcpy(&tmp, msg->op_body+p, sizeof(uint32_t));
    *snapshot = ntohl(tmp);
    p += sizeof(uint32_t);
    memcpy(&tmp, msg->op_body+p, sizeof(uint32_t));
    *startup = ntohl(tmp);
    p += sizeof(uint32_t);
    if ((*dbsrc = chunk_sprintf(label, "%s", msg->op_body+p)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk_sprintf", 
		__FUNCTION__);
	return -1;
    }
    p += strlen(*dbsrc)+1;
    if ((*dbdst = chunk_sprintf(label, "%s", msg->op_body+p)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk_sprintf", 
		__FUNCTION__);
	return -1;
    }
    p += strlen(*dbdst)+1;
    clicon_debug(2, "%s: snapshot: %d startup: %d dbsrc: %s dbdst: %s", 
	    __FUNCTION__, 
	    *snapshot, *startup, *dbsrc, *dbdst);
    return 0;
}

struct clicon_msg *
clicon_msg_validate_encode(char *db, const char *label)
{
    struct clicon_msg *msg;
    int len;
    int hdrlen = sizeof(*msg);

    clicon_debug(2, "%s: db: %s", __FUNCTION__, db);
    len = sizeof(*msg) + strlen(db) + 1;
    if ((msg = (struct clicon_msg *)chunk(len, label)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk", __FUNCTION__);
	return NULL;
    }
    memset(msg, 0, len);
    /* hdr */
    msg->op_type = CLICON_MSG_VALIDATE;
    msg->op_len = len;
    /* body */
    strncpy(msg->op_body, db, len-hdrlen);
    return msg;
}

int
clicon_msg_validate_decode(struct clicon_msg *msg, char **db, const char *label)
{
    /* body */
    if ((*db = chunk_sprintf(label, "%s", msg->op_body)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk_sprintf", __FUNCTION__);
	return -1;
    }
    clicon_debug(2, "%s: db: %s", __FUNCTION__, *db);
    return 0;
}


struct clicon_msg *
clicon_msg_change_encode(char *db, uint32_t op,	char *key, 
			char *lvec, uint32_t lvec_len, 
			const char *label)
{
    struct clicon_msg *msg;
    int len;
    int hdrlen = sizeof(*msg);
    int p;
    uint32_t tmp;

    clicon_debug(2, "%s: op: %d lvec_len: %d db: %s key: '%s'", 
	    __FUNCTION__, 
	    op, lvec_len, db, key);
#if 1
    if (debug)
	lv_dump(stderr, lvec, lvec_len);
#endif
    p = 0;
    len = sizeof(*msg) + 2*sizeof(uint32_t) + strlen(db) + 1 + 
	strlen(key) + 1 + lvec_len;
    if ((msg = (struct clicon_msg *)chunk(len, label)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk", __FUNCTION__);
	return NULL;
    }
    memset(msg, 0, len);
    /* hdr */
    msg->op_type = CLICON_MSG_CHANGE;
    msg->op_len = len;

    /* body */
    tmp = htonl(op);
    memcpy(msg->op_body+p, &tmp, sizeof(uint32_t));
    p += sizeof(uint32_t);

    tmp = htonl(lvec_len);
    memcpy(msg->op_body+p, &tmp, sizeof(uint32_t));
    p += sizeof(uint32_t);
    strncpy(msg->op_body+p, db, len-p-hdrlen);
    p += strlen(db)+1;
    strncpy(msg->op_body+p, key, len-p-hdrlen);
    p += strlen(key)+1;
    memcpy(msg->op_body+p, lvec, lvec_len);
    p += lvec_len;
    return msg;
}

int
clicon_msg_change_decode(struct clicon_msg *msg, 
			char **db, uint32_t *op, char **key, 
			char **lvec, uint32_t *lvec_len, 
			const char *label)
{
    int p;
    uint32_t tmp;

    p = 0;
    /* body */
    memcpy(&tmp, msg->op_body+p, sizeof(uint32_t));
    *op = ntohl(tmp);
    p += sizeof(uint32_t);

    memcpy(&tmp, msg->op_body+p, sizeof(uint32_t));
    *lvec_len = ntohl(tmp);
    p += sizeof(uint32_t);

    if ((*db = chunk_sprintf(label, "%s", msg->op_body+p)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk_sprintf", 
		__FUNCTION__);
	return -1;
    }
    p += strlen(*db)+1;
    if ((*key = chunk_sprintf(label, "%s", msg->op_body+p)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk_sprintf", 
		__FUNCTION__);
	return -1;
    }
    p += strlen(*key)+1;

    if (*lvec_len){
	if ((*lvec = chunk(*lvec_len, label)) == NULL){
	    clicon_err(OE_PROTO, errno, "%s: chunk", 
		       __FUNCTION__);
	    return -1;
	}
	memcpy(*lvec, msg->op_body+p, *lvec_len);
	p += *lvec_len;
    }
    else
	*lvec = NULL;
    clicon_debug(2, "%s: op: %d lvec_len: %d db: %s key: '%s'", 
	    __FUNCTION__, 
	    *op, *lvec_len, *db, *key);
    return 0;
}

struct clicon_msg *
clicon_msg_save_encode(char *db, uint32_t snapshot, char *filename, 
		      const char *label)
{
    struct clicon_msg *msg;
    int len;
    int hdrlen = sizeof(*msg);
    int p;
    uint32_t tmp;

    clicon_debug(2, "%s: snapshot: %d db: %s filename: %s", 
	    __FUNCTION__, 
	    snapshot, db, filename);
    p = 0;
    hdrlen = sizeof(*msg);
    len = sizeof(*msg) + sizeof(uint32_t) + strlen(db) + 1;
    if (!snapshot)
	len += 	strlen(filename) + 1;
    if ((msg = (struct clicon_msg *)chunk(len, label)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk", __FUNCTION__);
	return NULL;
    }
    memset(msg, 0, len);
    /* hdr */
    msg->op_type = CLICON_MSG_SAVE;
    msg->op_len = len;
    /* body */
    tmp = htonl(snapshot);
    memcpy(msg->op_body+p, &tmp, sizeof(uint32_t));
    p += sizeof(uint32_t);

    strncpy(msg->op_body+p, db, len-p-hdrlen);
    p += strlen(db)+1;
    if (!snapshot){
	strncpy(msg->op_body+p, filename, len-p-hdrlen);
	p += strlen(filename)+1;
    }
    return msg;
}

int
clicon_msg_save_decode(struct clicon_msg *msg, 
		      char **db, uint32_t *snapshot, char **filename, 
		      const char *label)
{
    int p;
    uint32_t tmp;

    p = 0;
    /* body */
    memcpy(&tmp, msg->op_body+p, sizeof(uint32_t));
    *snapshot = ntohl(tmp);
    p += sizeof(uint32_t);

    if ((*db = chunk_sprintf(label, "%s", msg->op_body+p)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk_sprintf", 
		__FUNCTION__);
	return -1;
    }
    p += strlen(*db)+1;
    if (*snapshot == 0){
	if ((*filename = chunk_sprintf(label, "%s", msg->op_body+p)) == NULL){
	    clicon_err(OE_PROTO, errno, "%s: chunk_sprintf", 
		    __FUNCTION__);
	    return -1;
	}
	p += strlen(*filename)+1;
    }
    clicon_debug(2, "%s: snapshot: %d db: %s filename: %s", 
	    __FUNCTION__, 
	    *snapshot, *db, *filename);
    return 0;
}

struct clicon_msg *
clicon_msg_load_encode(int replace, char *db, char *filename, const char *label)
{
    struct clicon_msg *msg;
    int hdrlen = sizeof(*msg);
    int len;
    uint32_t tmp;
    int p;

    clicon_debug(2, "%s: replace: %d db: %s filename: %s", 
	    __FUNCTION__, 
	       replace, db, filename);
    p = 0;
    len = sizeof(*msg) + sizeof(uint32_t) + strlen(db) + 1 + strlen(filename) + 1;
    if ((msg = (struct clicon_msg *)chunk(len, label)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk", __FUNCTION__);
	return NULL;
    }
    memset(msg, 0, len);
    /* hdr */
    msg->op_type = CLICON_MSG_LOAD;
    msg->op_len = len;
    /* body */
    tmp = htonl(replace);
    memcpy(msg->op_body+p, &tmp, sizeof(uint32_t));
    p += sizeof(uint32_t);

    strncpy(msg->op_body+p, db, len-p-hdrlen);
    p += strlen(db)+1;
    strncpy(msg->op_body+p, filename, len-p-hdrlen);
    p += strlen(filename)+1;
    return msg;
}

int
clicon_msg_load_decode(struct clicon_msg *msg, 
		       int *replace,
		       char **db, 
		       char **filename, 
		       const char *label)
{
    int p;
    uint32_t tmp;

    p = 0;
    /* body */
    memcpy(&tmp, msg->op_body+p, sizeof(uint32_t));
    *replace = ntohl(tmp);
    p += sizeof(uint32_t);
    if ((*db = chunk_sprintf(label, "%s", msg->op_body+p)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk_sprintf", 
		__FUNCTION__);
	return -1;
    }
    p += strlen(*db)+1;
    if ((*filename = chunk_sprintf(label, "%s", msg->op_body+p)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk_sprintf", 
		__FUNCTION__);
	return -1;
    }
    p += strlen(*filename)+1;
    clicon_debug(2, "%s: %d db: %s filename: %s", 
	    __FUNCTION__, 
	    msg->op_type,
	    *db, *filename);
    return 0;
}

struct clicon_msg *
clicon_msg_initdb_encode(char *filename, const char *label)
{
    clicon_debug(2, "%s: db: %s", __FUNCTION__, filename);
    return clicon_msg_1str_encode(filename, CLICON_MSG_INITDB, label);
}

int
clicon_msg_initdb_decode(struct clicon_msg *msg, 
		      char **filename, 
		      const char *label)
{
    int retval;

    retval = clicon_msg_1str_decode(msg, filename, label);
    clicon_debug(2, "%s: db: %s",  __FUNCTION__, *filename);
    return retval;
}

struct clicon_msg *
clicon_msg_rm_encode(char *filename, const char *label)
{
    clicon_debug(2, "%s: db: %s", __FUNCTION__, filename);
    return clicon_msg_1str_encode(filename, CLICON_MSG_RM, label);
}

int
clicon_msg_rm_decode(struct clicon_msg *msg, 
		      char **filename, 
		      const char *label)
{
    int retval;

    retval = clicon_msg_1str_decode(msg, filename, label);
    clicon_debug(2, "%s: db: %s",  __FUNCTION__, *filename);
    return retval;
}


struct clicon_msg *
clicon_msg_copy_encode(char *filename_src, char *filename_dst, 
		      const char *label)
{
    struct clicon_msg *msg;
    int hdrlen = sizeof(*msg);
    int len;
    int p;

    clicon_debug(2, "%s: filename_src: %s filename_dst: %s", 
	    __FUNCTION__, 
	    filename_src, filename_dst);
    p = 0;
    len = sizeof(*msg) + strlen(filename_src) + 1 + strlen(filename_dst) + 1;
    if ((msg = (struct clicon_msg *)chunk(len, label)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk", __FUNCTION__);
	return NULL;
    }
    memset(msg, 0, len);
    /* hdr */
    msg->op_type = CLICON_MSG_COPY;
    msg->op_len = len;
    /* body */
    strncpy(msg->op_body+p, filename_src, len-p-hdrlen);
    p += strlen(filename_src)+1;
    strncpy(msg->op_body+p, filename_dst, len-p-hdrlen);
    p += strlen(filename_dst)+1;
    return msg;
}

int
clicon_msg_copy_decode(struct clicon_msg *msg, 
		      char **filename_src, char **filename_dst, 
		      const char *label)
{
    int p;

    p = 0;
    /* body */
    if ((*filename_src = chunk_sprintf(label, "%s", msg->op_body+p)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk_sprintf", 
		__FUNCTION__);
	return -1;
    }
    p += strlen(*filename_src)+1;

    if ((*filename_dst = chunk_sprintf(label, "%s", msg->op_body+p)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk_sprintf", 
		__FUNCTION__);
	return -1;
    }
    p += strlen(*filename_dst)+1;
    clicon_debug(2, "%s: filename_src: %s filename_dst: %s", 
	    __FUNCTION__, 
	    *filename_src, *filename_dst);
    return 0;
}

struct clicon_msg *
clicon_msg_lock_encode(char *db, const char *label)
{
    clicon_debug(2, "%s: db: %s", __FUNCTION__, db);
    return clicon_msg_1str_encode(db, CLICON_MSG_LOCK, label);
}

int
clicon_msg_lock_decode(struct clicon_msg *msg, 
		      char **db, 
		      const char *label)
{
    int retval;

    retval = clicon_msg_1str_decode(msg, db, label);
    clicon_debug(2, "%s: db: %s",  __FUNCTION__, *db);
    return retval;
}

struct clicon_msg *
clicon_msg_unlock_encode(char *db, const char *label)
{
    clicon_debug(2, "%s: db: %s", __FUNCTION__, db);
    return clicon_msg_1str_encode(db, CLICON_MSG_UNLOCK, label);
}

int
clicon_msg_unlock_decode(struct clicon_msg *msg, 
		      char **db, 
		      const char *label)
{
    int retval;

    retval = clicon_msg_1str_decode(msg, db, label);
    clicon_debug(2, "%s: db: %s",  __FUNCTION__, *db);
    return retval;
}

struct clicon_msg *
clicon_msg_kill_encode(uint32_t session_id, const char *label)
{
    struct clicon_msg *msg;
    int len;
    int p;
    uint32_t tmp;

    clicon_debug(2, "%s: %d", __FUNCTION__, session_id);
    p = 0;
    len = sizeof(*msg) + sizeof(uint32_t);
    if ((msg = (struct clicon_msg *)chunk(len, label)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk", __FUNCTION__);
	return NULL;
    }
    memset(msg, 0, len);
    /* hdr */
    msg->op_type = CLICON_MSG_KILL;
    msg->op_len = len;
    /* body */
    tmp = htonl(session_id);
    memcpy(msg->op_body+p, &tmp, sizeof(uint32_t));
    p += sizeof(uint32_t);
    return msg;

}

int
clicon_msg_kill_decode(struct clicon_msg *msg, 
		      uint32_t *session_id, 
		      const char *label)
{
    int p;
    uint32_t tmp;

    p = 0;
    /* body */
    memcpy(&tmp, msg->op_body+p, sizeof(uint32_t));
    *session_id = ntohl(tmp);
    p += sizeof(uint32_t);
    clicon_debug(2, "%s: session-id: %u",  __FUNCTION__, *session_id);
    return 0;
}

struct clicon_msg *
clicon_msg_debug_encode(uint32_t level, const char *label)
{
    struct clicon_msg *msg;
    int len;
    int p;
    uint32_t tmp;

    clicon_debug(2, "%s: %d", __FUNCTION__, label);
    p = 0;
    len = sizeof(*msg) + sizeof(uint32_t);
    if ((msg = (struct clicon_msg *)chunk(len, label)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk", __FUNCTION__);
	return NULL;
    }
    memset(msg, 0, len);
    /* hdr */
    msg->op_type = CLICON_MSG_DEBUG;
    msg->op_len = len;
    /* body */
    tmp = htonl(level);
    memcpy(msg->op_body+p, &tmp, sizeof(uint32_t));
    p += sizeof(uint32_t);
    return msg;

}

int
clicon_msg_debug_decode(struct clicon_msg *msg, 
		      uint32_t *level, 
		      const char *label)
{
    int p;
    uint32_t tmp;

    p = 0;
    /* body */
    memcpy(&tmp, msg->op_body+p, sizeof(uint32_t));
    *level = ntohl(tmp);
    p += sizeof(uint32_t);
    clicon_debug(2, "%s: session-id: %u",  __FUNCTION__, *level);
    return 0;
}


struct clicon_msg *
clicon_msg_call_encode(uint16_t op, 
		       char *plugin, 
		       char *func,
		       uint16_t arglen, 
		       void *arg,
		       const char *label)
{
    struct clicon_msg *msg;
    struct clicon_msg_call_req *req;
    int hdrlen = sizeof(*msg);
    int len;
    
    clicon_debug(2, "%s: %d plugin: %s func: %s arglen: %d", 
	    __FUNCTION__, op, plugin, func, arglen);
    len =
	hdrlen +
	sizeof(struct clicon_msg_call_req) +
	strlen(plugin) + 1 + 
	strlen(func) + 1 + 
	arglen;
    if ((msg = (struct clicon_msg *)chunk(len, label)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk", __FUNCTION__);
	return NULL;
    }
    memset(msg, 0, len);
    /* hdr */
    msg->op_type = CLICON_MSG_CALL;
    msg->op_len = len;
    /* req */
    req = (struct clicon_msg_call_req *)msg->op_body;
    req->cr_len = htons(len - hdrlen);
    req->cr_op = htons(op);
    req->cr_plugin = req->cr_data;
    strncpy(req->cr_plugin, plugin, strlen(plugin));
    req->cr_func = req->cr_plugin + strlen(req->cr_plugin) + 1;
    strncpy(req->cr_func, func, strlen(func));
    req->cr_arglen = htons(arglen);
    req->cr_arg = req->cr_func + strlen(req->cr_func) + 1;
    memcpy(req->cr_arg, arg, arglen);
    
    return msg;
    
}

int
clicon_msg_call_decode(struct clicon_msg *msg, 
		       struct clicon_msg_call_req **req,
		       const char *label)
{
    uint16_t len;
    struct clicon_msg_call_req *r;

    r = (struct clicon_msg_call_req *)msg->op_body;
    len = ntohs(r->cr_len);
    if ((*req = chunk(len, label)) == NULL) {
	clicon_err(OE_PROTO, errno, "%s: chunk", __FUNCTION__);
	return -1;
    }
    memcpy(*req, r, len);
    (*req)->cr_len = ntohs(r->cr_len);
    (*req)->cr_op = ntohs(r->cr_op);
    (*req)->cr_arglen = ntohs(r->cr_arglen);
    (*req)->cr_plugin = (*req)->cr_data;
    (*req)->cr_func = (*req)->cr_plugin + strlen((*req)->cr_plugin) +1;
    (*req)->cr_arg = (*req)->cr_func + strlen((*req)->cr_func) +1;

    return 0;
}

struct clicon_msg *
clicon_msg_subscription_encode(char *stream, const char *label)
{
    clicon_debug(2, "%s: stream: %s", __FUNCTION__, stream);
    return clicon_msg_1str_encode(stream, CLICON_MSG_SUBSCRIPTION, label);
}

int
clicon_msg_subscription_decode(struct clicon_msg *msg, 
			       char **stream, 
			       const char *label)
{
    int retval;

    retval = clicon_msg_1str_decode(msg, stream, label);
    clicon_debug(2, "%s: stream: %s",  __FUNCTION__, *stream);
    return retval;
}

struct clicon_msg *
clicon_msg_notify_encode(int level, char *event, const char *label)
{
    struct clicon_msg *msg;
    int len;
    int hdrlen = sizeof(*msg);
    int p;
    int tmp;

    clicon_debug(2, "%s: %d %s", __FUNCTION__, level, event);
    p = 0;
    hdrlen = sizeof(*msg);
    len = sizeof(*msg) + sizeof(uint32_t) + strlen(event) + 1;
    if ((msg = (struct clicon_msg *)chunk(len, label)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk", __FUNCTION__);
	return NULL;
    }
    memset(msg, 0, len);
    /* hdr */
    msg->op_type = CLICON_MSG_NOTIFY;
    msg->op_len = len;
    /* body */
    tmp = htonl(level);
    memcpy(msg->op_body+p, &tmp, sizeof(int));
    p += sizeof(int);
    strncpy(msg->op_body+p, event, len-p-hdrlen);
    p += strlen(event)+1;

    return msg;
}

int
clicon_msg_notify_decode(struct clicon_msg *msg, 
			 int *level, char **event,
			 const char *label)
{
    int p;
    int tmp;

    p = 0;
    /* body */
    memcpy(&tmp, msg->op_body+p, sizeof(int));
    *level = ntohl(tmp);
    p += sizeof(int);
    if ((*event = chunk_sprintf(label, "%s", msg->op_body+p)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk_sprintf", 
		__FUNCTION__);
	return -1;
    }
    p += strlen(*event)+1;
    clicon_debug(2, "%s: %d %s", __FUNCTION__, *level, *event);
    return 0;
}

struct clicon_msg *
clicon_msg_err_encode(uint32_t err, uint32_t suberr, char *reason, const char *label)
{
    struct clicon_msg *msg;
    int len;
    int hdrlen = sizeof(*msg);
    int p;
    uint32_t tmp;

    clicon_debug(2, "%s: %d %d %s", __FUNCTION__, err, suberr, reason);
    p = 0;
    hdrlen = sizeof(*msg);
    len = sizeof(*msg) + 2*sizeof(uint32_t) + strlen(reason) + 1;
    if ((msg = (struct clicon_msg *)chunk(len, label)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk", __FUNCTION__);
	return NULL;
    }
    memset(msg, 0, len);
    /* hdr */
    msg->op_type = CLICON_MSG_ERR;
    msg->op_len = len;
    /* body */
    tmp = htonl(err);
    memcpy(msg->op_body+p, &tmp, sizeof(uint32_t));
    p += sizeof(uint32_t);
    tmp = htonl(suberr);
    memcpy(msg->op_body+p, &tmp, sizeof(uint32_t));
    p += sizeof(uint32_t);
    strncpy(msg->op_body+p, reason, len-p-hdrlen);
    p += strlen(reason)+1;

    return msg;

}

int
clicon_msg_err_decode(struct clicon_msg *msg, 
		      uint32_t *err, uint32_t *suberr, char **reason,
		      const char *label)
{
    int p;
    uint32_t tmp;

    p = 0;
    /* body */
    memcpy(&tmp, msg->op_body+p, sizeof(uint32_t));
    *err = ntohl(tmp);
    p += sizeof(uint32_t);
    memcpy(&tmp, msg->op_body+p, sizeof(uint32_t));
    *suberr = ntohl(tmp);
    p += sizeof(uint32_t);
    if ((*reason = chunk_sprintf(label, "%s", msg->op_body+p)) == NULL){
	clicon_err(OE_PROTO, errno, "%s: chunk_sprintf", 
		__FUNCTION__);
	return -1;
    }
    p += strlen(*reason)+1;
    clicon_debug(2, "%s: %d %d %s", 
	    __FUNCTION__, 
	    *err, *suberr, *reason);
    return 0;
}

