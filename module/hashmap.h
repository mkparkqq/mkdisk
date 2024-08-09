#ifndef _HASHMAP_H_
#define _HASHMAP_H_

#define KEY_LEN_MAX		32

#include "list.h"

#include <stddef.h>

struct hm_item {
	char key[KEY_LEN_MAX];
	void *ptr;
};

struct hashmap {
	size_t bucknum;
	size_t dsize;
	struct list **buckets; 		// list *배열
	size_t collision_count;
};

struct hashmap * init_hashmap(size_t);
/* 
 * opt: 1 overwrite
 * opt: 0 return -1 if a key-value already exists.
 *
 */
int set(struct hashmap *, const char *, void *, int);
void rm_item(struct hashmap *, const char *);
void * find(struct hashmap *, const char *);
size_t count_item(struct hashmap *);
void destruct_hashmap(struct hashmap *);
size_t count_collision(struct hashmap *);

#endif // _HASHMAP_H_
