#include "hashmap.h"
#include "list.h"

#include <string.h>
#include <stdlib.h>

static size_t 
hash(const char *key, size_t mod)
{
	size_t sum = 0;
	for (int i = 0; i < strlen(key); i++)
		sum += key[i];
	return (sum % mod);
}

struct hashmap * 
init_hashmap(size_t n)
{
	struct hashmap *map = (struct hashmap *) malloc(sizeof(struct hashmap));
	if (NULL == map)
		return NULL;

	map->bucknum = n;
	map->itemcnt = 0;
	map->buckets = (struct list **) malloc(n * sizeof(struct list *));

	if (NULL == map->buckets) {
		free(map);
		return NULL;
	}

	for (int i = 0; i < n; i++) {
		map->buckets[i] = init_list(sizeof(struct hm_item));
		if (NULL == map->buckets[i]) {
			free(map);
			return NULL;
		}
	}

	return map;
}

static struct hm_item *
search_item_by_key(struct hashmap *map, const char *key)
{
	size_t h = hash(key, map->bucknum);
	struct list *plist = map->buckets[h];

	if (NULL == plist->head) {
		return NULL;
	}

	struct lnode *node = plist->head;
	while (NULL != node) {
		struct hm_item *item = (struct hm_item *) node->ptr;
		if (0 == strcmp(item->key, key))
			return item;
		node = node->next;
	}

	return NULL;
}


int 
set(struct hashmap *map, const char *key, void *pdata, int opt)
{
	int overwrite= 0;
	struct hm_item *item = NULL;

	// Add new ht_item
	item = search_item_by_key(map, key);
	if (NULL == item) {
		size_t h = hash(key, map->bucknum);
		item = (struct hm_item *) malloc(sizeof(struct hm_item));
		if (NULL == item)
			return -1;
		memset(item, 0x00, sizeof(struct hm_item));
		strncpy(item->key, key, KEY_LEN_MAX);
		item->ptr = pdata;
		if (append(map->buckets[h], item) < 0) {
			free(item);
			return -1;
		}
		map->itemcnt++;
	// Overwrite
	} else {
		if (0 == opt)
			return -1;
		item->ptr = pdata;
	}

	return 0;
}

void 
rm_item(struct hashmap *map, const char *key)
{
	struct hm_item *item = search_item_by_key(map, key);
	if (NULL == item)
		return;

	size_t h = hash(key, map->bucknum);
	rm_lnode(map->buckets[h], item);
	map->itemcnt--;
	free(item);
	return;
}

void * 
find(struct hashmap *map, const char *key)
{
	struct hm_item *item = search_item_by_key(map, key);
	if (NULL == item)
		return NULL;
	return item->ptr;
}

size_t 
count_item(struct hashmap *map)
{
	return map->itemcnt;
}

// TODO
void 
destruct_hashmap(struct hashmap *map)
{
	if (NULL == map)
		return;
	for (int i = 0; i < map->bucknum; i++) {
		struct lnode *pnode = map->buckets[i]->head;
		while (NULL != pnode) {
			free(pnode->ptr); 		// Free hm_item
			pnode = pnode->next;
		}
		destruct_list(map->buckets[i]);
	}
	free(map->buckets);
	free(map);
	return;
}

size_t 
count_collision(struct hashmap *map)
{
	return 0;
}

#ifdef _UNIT_TEST_

#include "../test/mk_ctest.h"
#include "list.c"

#define TEST_BUCKET_NUM 		30

static struct mock *g_mocks = NULL;
static char **g_sample_keys = NULL;

static inline int
create_mocks(int n)
{
	g_mocks = (struct mock *) malloc(n * sizeof(struct mock));
	if (NULL == g_mocks)
		return -1;
	for (int i = 0; i < n; i++) {
		snprintf(g_mocks[i].foo, 16, "foo%d", i);
		g_mocks[i].bar = i;
	}
	return 0;
}

static inline int
create_sample_keys(int n)
{
	g_sample_keys = (char **) malloc(n * sizeof(char *));
	if (NULL == g_sample_keys)
		return -1;
	for (int i = 0; i < n; i++) {
		g_sample_keys[i] = (char *) malloc(KEY_LEN_MAX * sizeof(char));
		if (NULL == g_sample_keys)
			return -1;
		snprintf(g_sample_keys[i], KEY_LEN_MAX, "sample_key-%d", i);
	}
	return 0;
}

static void
destruct_sample_keys(int n)
{
	for (int i = 0; i < n; i++)
		free(g_sample_keys[i]);
	free(g_sample_keys);
}


static int
test_set(int c)
{
	struct hashmap *map = init_hashmap(TEST_BUCKET_NUM);
	if (NULL == map)
		return ERR;

	for (int i = 0; i < c; i++) {
		int h = hash(g_sample_keys[i], map->bucknum);
		set(map, g_sample_keys[i], &g_mocks[i], 1);
		struct hm_item *item = search_item_by_key(map, g_sample_keys[i]);
		if (NULL == item)
			goto failed;
		struct mock *m = (struct mock *) item->ptr;
		if (0 != strcmp(m->foo, g_mocks[i].foo) ||
				m->bar != g_mocks[i].bar)
			goto failed;
	}

	destruct_hashmap(map);

	return PASSED;

failed:
	destruct_hashmap(map);
	return FAILED;
}

static int
test_rm_item(int c)
{
	struct hashmap *map = init_hashmap(TEST_BUCKET_NUM);
	if (NULL == map)
		return ERR;

	// Insert * c
	for (int i = 0; i < c; i++)
		set(map, g_sample_keys[i], &g_mocks[i], 1);

	// Delete * c
	for (int i = 0; i < c; i++) {
		// When
		rm_item(map, g_sample_keys[i]);
		// Then
		int h = hash(g_sample_keys[i], map->bucknum);
		struct hm_item *item = search_item_by_key(map, g_sample_keys[i]);
		if (NULL != item) {
			destruct_hashmap(map);
			return FAILED;
		}
	}

	destruct_hashmap(map);

	return PASSED;
}

static int
test_find(int c)
{
	struct hashmap *map = init_hashmap(TEST_BUCKET_NUM);
	if (NULL == map)
		return ERR;

	struct mock *m = NULL;

	// Insert * c
	for (int i = 0; i < c; i++) {
		set(map, g_sample_keys[i], &g_mocks[i], 1);
		m = find(map, g_sample_keys[i]);
		if (NULL == m)
			goto failed;
		if (0 != memcmp(m, &g_mocks[i], sizeof (struct mock)))
			goto failed;
	}

	// Delete * c
	for (int i = 0; i < c; i++) {
		rm_item(map, g_sample_keys[i]);
		m = find(map, g_sample_keys[i]);
		if (NULL != m)
			goto failed;
	}
	destruct_hashmap(map);

	return PASSED;

failed:
	destruct_hashmap(map);
	return FAILED;
}

static int
test_count_item(int c)
{
	struct hashmap *map = init_hashmap(TEST_BUCKET_NUM);
	if (NULL == map)
		return ERR;

	// Insert * c
	for (int i = 0; i < c; i++) {
		set(map, g_sample_keys[i], &g_mocks[i], 1);
		if (i + 1 != count_item(map)) {
			destruct_hashmap(map);
			return FAILED;
		}
	}

	// Delete * c
	for (int i = 0; i < c; i++) {
		rm_item(map, g_sample_keys[i]);
		if (c - 1 - i != count_item(map)) {
			destruct_hashmap(map);
			return FAILED;
		}
	}
	destruct_hashmap(map);

	return PASSED;
}

static int
test_overwriting(int c)
{
	struct hashmap *map = init_hashmap(TEST_BUCKET_NUM);
	if (NULL == map)
		return ERR;

	struct mock *m = NULL;

	// Insert * c
	for (int i = 0; i < c; i++)
		set(map, g_sample_keys[i], &g_mocks[i], 1);

	// Overwrite * c
	struct mock *new_mocks = (struct mock *) malloc(c * sizeof(struct mock));
	if (NULL == new_mocks)
		goto failed;
	memset(new_mocks, 0x00, c * sizeof(struct mock));
	for (int i = 0; i < c; i++) {
		snprintf(new_mocks[i].foo, 16, "DB-1-2-1_%d", i);
		new_mocks[i].bar = 2 * i;
		set(map, g_sample_keys[i], &new_mocks[i], 1);
		if (c != map->itemcnt)
			goto failed;
	}
	for (int i = 0; i < c; i++) {
		m = find(map, g_sample_keys[i]);
		if (NULL == m)
			goto failed;
		if (0 != memcmp(m, &new_mocks[i], sizeof (struct mock)))
			goto failed;
	}

	// No-overwriting option
	for (int i = 0; i < c; i++) {
		if (0 == set(map, g_sample_keys[i], &new_mocks[i], 0))
			goto failed;
	}

	// Delete * c
	for (int i = 0; i < c; i++) {
		rm_item(map, g_sample_keys[i]);
		m = find(map, g_sample_keys[i]);
		if (NULL != m)
			goto failed;
	}
	free(new_mocks);
	destruct_hashmap(map);

	return PASSED;

failed:
	if (NULL != new_mocks)
		free(new_mocks);
	destruct_hashmap(map);
	return FAILED;
}

int 
main(int argc, const char *argv[])
{
	int c = 100;
	if (2 == argc)
		c = atoi(argv[1]);

	if (create_mocks(c) < 0)
		return 1;
	if (create_sample_keys(c) < 0)
		return 1;

	UNIT_TEST("set", test_set, c);
	UNIT_TEST("rm_item", test_rm_item, c);
	UNIT_TEST("find", test_find, c);
	UNIT_TEST("count_item", test_count_item, c);
	UNIT_TEST("overwriting", test_overwriting, c);

	free(g_mocks);
	destruct_sample_keys(c);

	return 0;
}

#endif // _UNIT_TEST_
