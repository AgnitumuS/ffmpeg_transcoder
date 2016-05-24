#ifndef PATCH_QUEUE_H
#define PATCH_QUEUE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "debug.h"

typedef struct queue {
	char *data; 							// array to store the actual data
	size_t queue_size; 				// maximum number of blocks in the queue
	size_t blk_size; 						// size of each element block
	size_t head; 								// point to the head of the queue
	size_t tail; 								// point to the tail of the queue
} queue;

void initialize_queue(queue *q, size_t queue_size, size_t blk_size) {
	q->data = (char *)malloc(sizeof(char) * blk_size * queue_size);
	q->queue_size = queue_size;
	q->blk_size = blk_size;
	q->head = 0;
	q->tail = 0;
}

void finalize_queue(queue *q) {
	free(q->data);
}

void *get_head(queue *q) {
	return q->data + q->head * q->blk_size;
}

void *get_tail(queue *q) {
	return q->data + q->tail * q->blk_size;
}

bool is_full_queue(queue *q) {
	return (q->head - q->tail + q->queue_size) % q->queue_size == 1;
}

bool is_empty_queue(queue *q) {
	return q->tail == q->head;
}

/**
 * not thread safe
 */
size_t length_of_queue(queue *q) {
	return (q->tail - q->head + q->queue_size) % q->queue_size;
}

/**
 * copy q->blk_size bytes from elem to queue[tail]
 * WARN: without checking the boundry of pointer elem
 */
void enqueue(queue *q, void *elem) {
	if (!is_full_queue(q)) {
		memcpy(get_tail(q), elem, q->blk_size);
		q->tail = (q->tail + 1) % q->queue_size;
	} else {
		// do something when queue is full...
		ERR_LOG("queue is full\n");
	}
}

void *dequeue(queue *q) {
	char *elem = (char *)malloc(sizeof(char) * q->blk_size);
	if (!is_empty_queue(q)) {
		memcpy(elem, get_head(q), q->blk_size);
		q->head = (q->head + 1) % q->queue_size;
		return elem;
	} else {
		ERR_LOG("queue is empty\n");
		free(elem);
		return 0;
	}
}
#endif
