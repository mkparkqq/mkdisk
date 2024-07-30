#ifndef _LIST_H_
#define _LIST_H_

#include <stddef.h>
#include <pthread.h>

struct lnode {
	void *ptr;
	struct lnode *next;
	struct lnode *prev;
};

struct list {
	size_t dsize;  		// Size of a data structure pointed by lnode.ptr
	size_t nodecnt;
	struct lnode *head;
	struct lnode *tail;
	pthread_rwlock_t rwlock;
};

struct list *init_list(size_t);
int append(struct list *, void *); // Shallow copy.
void *search_list(struct list *, const void *); // Shallow copy.
void rm_lnode(struct list *list, const void *);
size_t listlen(struct list *);
void destruct_list(struct list *);

#endif // _LIST_H_
