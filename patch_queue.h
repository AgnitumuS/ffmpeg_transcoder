#ifndef PATCH_QUEUE_H
#define PATCH_QUEUE_H

#define TAG_SIZE 1
#define BLK_TAG(BLK) (*(char *)BLK)
#define BLK_ELEM(BLK) ((char *)BLK + TAG_SIZE)

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <semaphore.h>
#include "debug.h"
#include "contracts.h"

typedef struct queue {
	char *data; 							// array to store the actual data
	size_t queue_size; 				// maximum number of blocks in the queue
  size_t elem_size;           // size of each element
	size_t blk_size; 						// size of each element block
	size_t head; 								// point to the head of the queue
	size_t tail; 								// point to the tail of the queue
	sem_t enqueue_mutex; 				// the mutex to lock enqueue
	sem_t dequeue_mutex; 				// the mutex to lock dequeue
} queue;

/**
 * initialize queue, set attribute of queue, blk_size is size of each block in the queue; each block contains a tag and an element
 * a block is something like this:
 * +++++++++++++++++++++++++++++++++++++++
 * +     +                               +
 * +     +                               +
 * + tag +           element             +
 * +     +                               +
 * +     +                               +
 * +++++++++++++++++++++++++++++++++++++++
 * @param q: the queue to be initialized
 * @param queue_size: the number of blocks in the queue
 * @param elem_size: the size of each element
 */
void initialize_queue(queue *q, size_t queue_size, size_t elem_size);

/**
 * free the data pointer
 * @param q: the queue to be finalized
 */
void finalize_queue(queue *q);

/**
 * get the current index number of head
 * return NULL if queue is empty
 * the return value is not guaranteed because of enqueue and dequeue
 */
void *get_first_elem(queue *q);

/**
 * get the current index number of tail
 * return NULL if queue is empty
 * the return value is not guaranteed because of enqueue and dequeue
 */
void *get_last_elem(queue *q);

/**
 * judge if the element value is contained in the queue
 * the value of extern_elem does not have to be in queue
 * the return value is not guaranteed because of enqueue and dequeue
 */
bool contained_in_queue(queue *q, void *extern_elem);

/**
 * return the pointer to the element in the queue that lies next to elem
 * return NULL if the elem is out of the boundry 
 *   or the next element is out of boundry
 */
void *next_element(queue *q, void *elem);

/**
 * the return value is not guaranteed because of enqueue and dequeue
 */
size_t length_of_queue(queue *q);

/**
 * copy q->blk_size bytes from elem to queue[tail]
 * WARN: without checking the boundry of pointer elem
 */
void enqueue(queue *q, void *elem);

/**
 * copy q->blk_size bytes from queue[tail] to elem
 * WARN: without checking the boundry of pointer elem
 */
void *dequeue(queue *q);
#endif
