#ifndef _QUEUE_H_
#define _QUEUE_H_

#include <stddef.h>
#include <pthread.h>

#define QUEUE_EMPTY			1
#define QUEUE_NOT_EMPTY 	0

struct queue {
	void *arr;
	size_t esz; 		// Element's size.
	size_t count;
	size_t capacity;
	// indices
	size_t head;
	size_t tail;
	pthread_rwlock_t rwlock;
};

/**
 * @brief Create a new queue instance.
 *
 * @param capacity Default capacity of the queue.
 * @param esz An entry size of the queue.
 *
 * @returns  A pointer to the queue.
 */
struct queue *init_queue(size_t capacity, size_t esz);
int enqueue(struct queue *q, const void *entry);   			// Deep copy
int dequeue(struct queue *q, void *entry);					// Deep copy
int is_empty(struct queue *q);
void destruct_queue(struct queue *q);

#endif // _QUEUE_H_
