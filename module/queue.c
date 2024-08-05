#include "queue.h"

#include <string.h>
#include <stdlib.h>

struct queue *
init_queue(size_t capacity, size_t esz)
{
	struct queue *q = (struct queue *)malloc(sizeof(struct queue));
	if (NULL == q)
		return NULL;
	q->arr = malloc(esz * capacity);
	if (NULL == q->arr) {
		free(q);
		return NULL;
	}
	q->esz = esz;
	q->count = 0;
	q->capacity = capacity;
	q->head = 0;
	q->tail = 0;
	pthread_rwlock_init(&q->rwlock, NULL);

	return q;
}

int 
is_empty(struct queue *q)
{
	pthread_rwlock_rdlock(&q->rwlock);
	int ret = (0 == q->count);
	pthread_rwlock_unlock(&q->rwlock);

	return ret;
}

int
enqueue(struct queue *q, const void *entry){
	pthread_rwlock_wrlock(&q->rwlock);
	if (q->count == q->capacity) {
		pthread_rwlock_unlock(&q->rwlock);
		return -1;
	}
	size_t next = (0 == q->count) ? 0 : (q->tail + 1) % q->capacity;
	memcpy(q->arr + (next * q->esz), entry, q->esz);
	q->count++;
	q->tail = next;
	pthread_rwlock_unlock(&q->rwlock);
	return 0;
}

int
dequeue(struct queue *q, void *entry)
{
	pthread_rwlock_wrlock(&q->rwlock);
	if (0 == q->count) {
		pthread_rwlock_unlock(&q->rwlock);
		return -1;
	}
	if (entry != NULL)
		memcpy(entry, q->arr + (q->esz * q->head), q->esz);
	q->count--;
	q->head = (q->head + 1) % q->capacity;
	if (0 == q->count) {
		q->head = 0;
		q->tail = 0;
	}
	pthread_rwlock_unlock(&q->rwlock);
	return 0;
}

void 
destruct_queue(struct queue *q)
{
	free(q->arr);
	pthread_rwlock_destroy(&q->rwlock);
	free(q);
	q = NULL;
}

#ifdef _UNIT_TEST_
#include "../test/mk_ctest.h"
#include <stdlib.h>

/*
 * Target function
 * enqueue
 */
static int
test_enqueue(int c)
{
	struct mock st;
	struct queue *tq = init_queue(c, sizeof(struct mock));
	if (NULL == tq) {
		printf("\033[31m[malloc]\033[0m");
		return ERR;
	}
	for (int i = 0; i < c; i++) {
		st.bar = i;
		enqueue(tq, &st);
		struct mock *tmp = (struct mock *)(tq->esz * i + tq->arr);
		if (tq->count != i + 1 || tmp->bar != st.bar) {
			destruct_queue(tq);
			return FAILED;
		}
	}
	destruct_queue(tq);
	return PASSED;
}

/*
 * Target function
 * dequeue
 */
static int
test_dequeue(int c)
{
	struct queue *tq = NULL;
	tq = init_queue(c, sizeof(struct mock));
	if (NULL == tq) {
		printf("\033[31m[malloc]\033[0m");
		return ERR;
	}
	for (int i = 0; i < c; i++) {
		struct mock st;
		st.bar = i;
		enqueue(tq, &st);
	}

	struct mock st;
	for (int i = 0; i < c; i++) {
		if (dequeue(tq, &st) < 0)
			goto failed;
		if (i != st.bar)
			goto failed;
		if ((i + 1) % c != tq->head)
			goto failed;
		if (c - i - 1 != tq->count)
			goto failed;
	}

	destruct_queue(tq);
	return PASSED;

failed:
	destruct_queue(tq);
	return FAILED;
}

/*
 * Target function
 * is_empty
 */
static int
test_is_empty(int c)
{
	struct mock st;
	struct queue *tq = init_queue(c, sizeof(struct mock));
	if (NULL == tq) {
		printf("\033[31m[malloc]\033[0m");
		return FAILED;
	}
	// Case1: queue is empty.
	if (QUEUE_EMPTY != is_empty(tq)) {
		destruct_queue(tq);
		return FAILED;
	}
	for (int i = 0; i < c; i++) {
		// Case2: Insert * c
		for (int i = 0; i < c; i++) {
			enqueue(tq, &st);
			if (QUEUE_NOT_EMPTY != is_empty(tq)) {
				destruct_queue(tq);
				return FAILED;
			}
		}
		// Case3: Pop * c
		for (int i = 0; i < c; i++) {
			if (QUEUE_NOT_EMPTY != is_empty(tq)) {
				destruct_queue(tq);
				return FAILED;
			}
			dequeue(tq, NULL); // Pop
		}
		if (QUEUE_EMPTY != is_empty(tq)) {
			destruct_queue(tq);
			return FAILED;
		}
	}
	destruct_queue(tq);
	return PASSED;
}

/*
 * Test for the condition tail <= head.
 */
static int
test_edgecase(int c)
{
	// Given
	struct queue *tq = NULL;
	tq = init_queue(c, sizeof(struct mock));
	if (NULL == tq) {
		printf("\033[31m[malloc]\033[0m");
		return FAILED;
	}

	struct mock st;
	// When
	for (int k = 0; k < c - 1; k++) {
		// Insert * c 
		for (int i = 0; i < c; i++) {
			st.bar = i;
			enqueue(tq, &st);
		}
		// Pop * k
		for (int i = 0; i < k; i++)
			dequeue(tq, NULL);
		// Insert * k
		for (int i = 0; i < k; i++){
			st.bar = i;
			enqueue(tq, &st);
		}
	    // Then
		for (int i = 0 ; i < c; i++) {
			if (dequeue(tq, &st) < 0)
				goto failed;
			// Check FIFO
			if (st.bar != ((k + i) % c))
				goto failed;
		}
	}
	destruct_queue(tq);
	return PASSED;

failed:
	destruct_queue(tq);
	return FAILED;
}


int
main(int argc, const char *argv[])
{
	int c = 100;
	if (2 == argc)
		c = atoi(argv[1]);

	UNIT_TEST ("enqueue", test_enqueue, c);

	UNIT_TEST("dequeue", test_dequeue, c);

	UNIT_TEST("is_empty", test_is_empty, c);

	UNIT_TEST("Edge cases", test_edgecase, c);

	return 0;
}

#endif // _UNIT_TEST_
