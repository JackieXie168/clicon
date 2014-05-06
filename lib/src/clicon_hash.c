/*
 *  CVS Version: $Id: clicon_hash.c,v 1.8 2013/08/05 14:12:06 olof Exp $
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


/*
 * A simple implementation of a associative array style data store. Keys
 * are always strings while values can be some arbitrary data referenced
 * by void*.
 *
 * XXX: functions such as hash_keys(), hash_value() etc are currently returning
 * pointers to the actual data storage. Should probably make copies.
 *
 * Example usage:
 *
 *  int main()
 *  {
 *    char **s;
 *    int n;
 *    size_t slen;
 *    clicon_hash_t *hash = hash_init();
 *
 *    n = 234;
 *    hash_add (hash, "APA", &n, sizeof(n));
 *    hash_dump(hash, stdout);
 *
 *    puts("----");
 *
 *    hash_add (hash, "BEPA", "hoppla Polle!", strlen("hoppla Polle!")+1);
 *    puts((char *)hash_value(hash, "BEPA", NULL));
 *    hash_dump(hash, stdout);
 *    
 *    puts("----");
 *
 *    n = 33;
 *    hash_add (hash, "CEPA", &n, sizeof(n));
 *    hash_dump(hash, stdout);
 *
 *    puts("----");
 *    
 *    hash_del (hash, "APA");
 *    hash_dump(hash, stdout);
 *
 *    hash_free(hash);
 *
 *    return 0;
 * }
 */

#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

/* clicon */
#include "clicon_queue.h"
#include "clicon_err.h"
#include "clicon_hash.h"

#define HASH_SIZE	1031	/* Number of hash buckets. Should be a prime */ 

/*
 * A very simplistic algorithm to calculate a hash bucket index
 */
static uint32_t
hash_bucket(char *str)
{
    uint32_t n = 0;

    while(*str)
	n += (uint32_t)*str++;
    
    return n % HASH_SIZE;
}

/*
 * Initialize hash table.
 *
 * Arguments:	none
 *
 * Returns: new pointer to hash table.
 */
clicon_hash_t *
hash_init (void)
{
  clicon_hash_t *hash;

  if ((hash = (clicon_hash_t *)malloc (sizeof (clicon_hash_t) * HASH_SIZE)) == NULL){
      clicon_err(OE_UNIX, errno, "malloc: %s", strerror(errno));
      return NULL;
  }
  memset (hash, 0, sizeof(clicon_hash_t)*HASH_SIZE);
  return hash;
}

/*
 * Free hash table.
 *
 * Arguments:
 *	hash	  	- Hash table
 *
 * Returns: void
 */
void
hash_free (clicon_hash_t *hash)
{
    int i;
    clicon_hash_t tmp;
    for (i = 0; i < HASH_SIZE; i++) {
	while (hash[i]) {
	    tmp = hash[i];
	    DELQ(tmp, hash[i], clicon_hash_t);
	    free(tmp->h_key);
	    free(tmp->h_val);
	    free(tmp);
	}
    }
    free(hash);
}


/*
 * Find keys.
 *
 *	key		- Variable name
 *
 * Returns: variable structure on success, NULL on failure
 */
clicon_hash_t
hash_lookup (clicon_hash_t *hash, char *key)
{
    uint32_t bkt;
    clicon_hash_t h;

    bkt = hash_bucket(key);
    h = hash[bkt];
    if (h) {
	do {
	    if (!strcmp (h->h_key, key))
		return h;
	    h = NEXTQ(clicon_hash_t, h);
	} while (h != hash[bkt]);
    }
    
    return NULL;
}

/*
 * Get value of hash
 */
void *
hash_value(clicon_hash_t *hash, char *key, size_t *vlen)
{
    clicon_hash_t h;

    h = hash_lookup(hash, key);
    if (h == NULL)
	return NULL;

    if (vlen)
	*vlen = h->h_vlen;
    return h->h_val;
}


/*
 * Copy value and add hash entry.
 *
 * Arguments:
 *	hash	  	- Hash structure
 *	key		- New variable name
 *	val		- New variable value
 *
 * Returns: new variable on success, NULL on failure
 */
clicon_hash_t
hash_add (clicon_hash_t *hash, char *key, void *val, size_t vlen)
{
    void *newval;
    clicon_hash_t h, new = NULL;
    
    /* If variable exist, don't allocate a new. just replace value */
    h = hash_lookup (hash, key);
    if (h == NULL) {
	if ((new = (clicon_hash_t)malloc (sizeof (*new))) == NULL){
	    clicon_err(OE_UNIX, errno, "malloc: %s", strerror(errno));
	    goto catch;
	}
	memset (new, 0, sizeof (*new));
	
	new->h_key = strdup (key);
	if (new->h_key == NULL){
	    clicon_err(OE_UNIX, errno, "strdup: %s", strerror(errno));
	    goto catch;
	}
	
	h = new;
    }
    
    /* Make copy of lvalue */
    newval = malloc (vlen+3); /* XXX: qdbm needs aligned mallocs? */
    if (newval == NULL){
	clicon_err(OE_UNIX, errno, "malloc: %s", strerror(errno));
	goto catch;
    }
    memcpy (newval, val, vlen);
    
    /* Free old value if existing variable */
    if (h->h_val)
	free (h->h_val);
    h->h_val = newval;
    h->h_vlen =  vlen;

    /* Add to list only if new variable */
    if (new)
	INSQ(h, hash[hash_bucket(key)]);

    return h;

catch:
    if (new) {
	if (new->h_key)
	    free (new->h_key);
	free (new);
    }

    return NULL;
}

/*
 * Delete entry.
 *
 * Arguments:
 *	hash	  	- Hash structure
 *	key		- Variable name
 *
 * Returns: 0 on success, -1 on failure
 */
int
hash_del (clicon_hash_t *hash, char *key)
{
    clicon_hash_t h;

    h = hash_lookup (hash, key);
    if (h == NULL)
	return -1;
    
    DELQ(h, hash[hash_bucket(key)], clicon_hash_t);
  
    free (h->h_key);
    free (h->h_val);
    free (h);

    return 0;
}

char **
hash_keys(clicon_hash_t *hash, size_t *nkeys)
{
    int bkt;
    clicon_hash_t h;
    char **tmp;
    char **keys = NULL;

    *nkeys = 0;
    for (bkt = 0; bkt < HASH_SIZE; bkt++) {
	h = hash[bkt];
	do {
	    if (h == NULL)
		break;
	    tmp = realloc(keys, ((*nkeys)+1) * sizeof(char *));
	    if (tmp == NULL){
		clicon_err(OE_UNIX, errno, "realloc: %s", strerror(errno));
		goto catch;
	    }
	    keys = tmp;
	    keys[*nkeys] = h->h_key;
	    (*nkeys)++;
	    h = NEXTQ(clicon_hash_t, h);
	} while (h != hash[bkt]);
    }

    return keys;

catch:
    if (keys)
	free(keys);
    return NULL;
}

/*
 * Dump contents of hash to FILE pointer.
 *
 * Arguments:
 *	f		- FILE pointer for print output
 *	hash	  	- Hash structure
 *
 * Returns: void
 */
void
hash_dump(clicon_hash_t *hash, FILE *f)
{
    int i;
    char **keys;
    void *val;
    size_t klen;
    size_t vlen;
    
    if (hash == NULL)
	return;
    keys = hash_keys(hash, &klen);
    if (keys == NULL)
	return;
	
    for(i = 0; i < klen; i++) {
	val = hash_value(hash, keys[i], &vlen);
	printf("%s =\t 0x%p , length %zu\n", keys[i], val, vlen);
    }
    free(keys);
}
