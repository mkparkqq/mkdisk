#include "list.h"

#include <stdlib.h>
#include <string.h>

struct list *
init_list(size_t dsz)
{
	struct list *plist = (struct list *) malloc(sizeof(struct list));
	if (NULL == plist)
		return NULL;
	plist->nodecnt = 0;
	plist->dsize = dsz; // lnode->ptr이 가리키는 공간의 크기
	plist->head = NULL;
	plist->tail = NULL;
	pthread_rwlock_init(&plist->rwlock, NULL);
	return plist;
}

int 
append(struct list *plist, void *pdata)
{
	pthread_rwlock_wrlock(&plist->rwlock);

	struct lnode *node = (struct lnode *) malloc(sizeof(struct lnode));
	if (NULL == node) {
		pthread_rwlock_unlock(&plist->rwlock);
		return -1;
	}

	node->ptr = pdata;
	node->next = NULL;
	if (NULL == plist->head) {
		node->prev = NULL;
		plist->head = node;
	} else {
		node->prev = plist->tail;
		plist->tail->next = node;
	}
	plist->tail = node;
	plist->nodecnt++;

	pthread_rwlock_unlock(&plist->rwlock);

	return 0;
}

static struct lnode * 
search_lnode(struct list* plist, const void *target)
{
	if (NULL == plist->head) {
		return NULL;
	}
	struct lnode *node = plist->head;
	while (NULL != node) {
		if (0 == memcmp(node->ptr, target, plist->dsize))
			return node;
		node = node->next;
	}
	return NULL;
}

void *
search_list(struct list *plist, const void *target)
{
	pthread_rwlock_rdlock(&plist->rwlock);

	struct lnode *node = search_lnode(plist, target);
	if (NULL == node) {
		pthread_rwlock_unlock(&plist->rwlock);
		return NULL;
	}

	pthread_rwlock_unlock(&plist->rwlock);

	return node->ptr;
}

void 
rm_lnode(struct list *plist, const void *target)
{
	pthread_rwlock_wrlock(&plist->rwlock);

	struct lnode *node = search_lnode(plist, target);
	if (NULL == node) {
		pthread_rwlock_unlock(&plist->rwlock);
		return;
	}

	if (plist->nodecnt == 1) {
		plist->head = NULL;
		plist->tail = NULL;
	} else if (plist->tail == node) {
		node->prev->next = NULL;
		plist->tail = node->prev;
	} else if (plist->head == node) {
		plist->head = node->next;
		node->next->prev = NULL;
	} else {
		struct lnode *tmp = node->next;
		node->prev->next = node->next;
		node->next->prev = node->prev;
	}
	free(node);
	plist->nodecnt--;

	pthread_rwlock_unlock(&plist->rwlock);

	return;
}

size_t 
listlen(struct list *plist)
{
	pthread_rwlock_rdlock(&plist->rwlock);
	int len = plist->nodecnt;
	pthread_rwlock_unlock(&plist->rwlock);

	return len;
}

void 
destruct_list(struct list *plist)
{
	if (NULL == plist)
		return;

	while (NULL != plist->head) {
		struct lnode *next = plist->head->next;
		free(plist->head);
		plist->head = next;
	}
	free(plist);
	plist = NULL;
	return;
}


#ifdef _UNIT_TEST_

#include "../test/mk_ctest.h"
#include <stdlib.h>

static int
test_append(int c){
	struct list *plist = init_list(sizeof(struct mock));
	if (NULL == plist) 
		return ERR;
	struct mock *mocks = (struct mock *) malloc(c * sizeof(struct mock));
	if (NULL == mocks) {
		free(plist);
		return ERR;
	}
	int result = 0;
	for (int i = 0; i < c; i++) {
		snprintf(mocks[i].foo, 16, "foo %d", i);
		mocks[i].bar = i;
		if(append(plist, &mocks[i]) < 0) {
			destruct_list(plist);
			free(mocks);
			return ERR;
		}
	}

	struct lnode *cursor = plist->head;
	for (int i = 0; i < c; i++) {
		if (0 != memcmp(cursor->ptr, &mocks[i], sizeof(struct mock))) goto failed;
		cursor = cursor->next;
	}
	free(mocks);
	destruct_list(plist);
	return PASSED;

failed:
	free(mocks);
	destruct_list(plist);
	return FAILED;
}

static int
test_search_list(int c)
{
	struct list *plist = init_list(sizeof(struct mock));
	if (NULL == plist) 
		return ERR;
	struct mock *mocks = (struct mock *) malloc(c * sizeof(struct mock));
	if (NULL == mocks) {
		free(plist);
		return ERR;
	}
	for (int k = 1; k <= c; k++) {
		// Append * c
		for (int i = 0; i < k; i++) {
			snprintf(mocks[i].foo, 16, "foo %d", i);
			mocks[i].bar = i;
			if(append(plist, &mocks[i]) < 0) {
				destruct_list(plist);
				free(mocks);
				return ERR;
			}
		}
		// Search * c
		struct mock *data;
		for (int i = k - 1; i >= 0; i--) {
			data = search_list(plist, &mocks[i]);
			if (NULL == data)
				goto failed;
			if (0 != memcmp(data, &mocks[i], sizeof(struct mock)))
				goto failed;
		}
	}
	destruct_list(plist);
	free(mocks);
	return PASSED;

failed:
	free(mocks);
	destruct_list(plist);
	return FAILED;
}

static int
test_rm_lnode(int c){
	struct list *plist = init_list(sizeof(struct mock));
	if (NULL == plist) 
		return ERR;
	struct mock *mocks = (struct mock *) malloc(c * sizeof(struct mock));
	if (NULL == mocks) {
		free(plist);
		return ERR;
	}
	int result = 0;
	for (int i = 0; i < c; i++) {
		snprintf(mocks[i].foo, 16, "foo %d", i);
		mocks[i].bar = i;
		if(append(plist, &mocks[i]) < 0) {
			destruct_list(plist);
			free(mocks);
			return ERR;
		}
	}
	for (int i = 0; i < c; i++) {
		rm_lnode(plist, &mocks[i]);
		if (NULL != search_list(plist, &mocks[i])) {
			destruct_list(plist);
			free(mocks);
			return FAILED;
		}
	}
	free(mocks);
	destruct_list(plist);
	return PASSED;
}

static int
test_listlen(int c){
	struct list *plist = init_list(sizeof(struct mock));
	if (NULL == plist) 
		return ERR;
	struct mock *mocks = (struct mock *) malloc(c * sizeof(struct mock));
	if (NULL == mocks)
		goto failed;
	int result = 0;
	for (int i = 0; i < c; i++) {
		snprintf(mocks[i].foo, 16, "foo %d", i);
		mocks[i].bar = i;
		if(append(plist, &mocks[i]) < 0) {
			destruct_list(plist);
			free(mocks);
			return ERR;
		}
		if (i + 1 != listlen(plist))
			goto failed;
	}

	free(mocks);
	destruct_list(plist);
	return PASSED;

failed:
	destruct_list(plist);
	return FAILED;
}

static int
test_edgecase(int c){
	return FAILED;
}

#ifndef _HASHMAP_H_
int
main(int argc, const char *argv[])
{
	int c = 100;
	if (2 == argc)
		c = atoi(argv[1]);

	UNIT_TEST ("append", test_append, c);
	UNIT_TEST("search_list", test_search_list, c);
	UNIT_TEST("rm_lnode", test_rm_lnode, c);
	UNIT_TEST("listlen", test_listlen, c);
	// UNIT_TEST("Edge cases", test_edgecase, c);

	return 0;
}

#endif //_HASHMAP_H_

#endif // _UNIT_TEST_
