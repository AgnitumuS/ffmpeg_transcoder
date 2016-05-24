#include "patch_queue.h"

void initialize_queue(queue *q, size_t queue_size, size_t elem_size) {
	q->queue_size = queue_size;
  q->elem_size = elem_size;
	q->blk_size = elem_size + TAG_SIZE;
	q->head = 0;
	q->tail = 0;
	q->data = (char *)malloc(sizeof(char) * q->blk_size * q->queue_size);
  memset(q->data, 0, sizeof(char) * q->blk_size * q->queue_size);
  sem_init(&q->enqueue_mutex, 0, 1);
  sem_init(&q->dequeue_mutex, 0, 1);
}

void finalize_queue(queue *q) {
	free(q->data);
}

/**
 * get the address of the head block in the queue
 * return NULL if queue is empty
 * TODO: hidden
 */
void *get_head_blk(queue *q) {
  char *ret = q->data + q->head * q->blk_size;
	return ret;
}

/**
 * get the address of the tail block in the queue
 * return NULL if queue is empty
 * TODO: hidden
 */
void *get_tail_blk(queue *q) {
  char *ret = q->data + q->tail * q->blk_size; 
	return ret;
}

/**
 * get blk by index
 * TODO: hidden
 */
void *get_blk_at(queue *q, size_t index) {
	REQUIRES(index >= 0 && index < q->queue_size);
	return q->data + index * q->blk_size;
}

void *get_first_elem(queue *q) {
	char *ret = 0;
	if (length_of_queue(q) != 0) {
		ret = BLK_ELEM(get_blk_at(q, q->head));
	}
	return ret;
}

void *get_last_elem(queue *q) {
	char *ret = 0;
	if (length_of_queue(q) != 0) {
		int last = (q->tail - 1 + q->queue_size) % q->queue_size;
		ret = BLK_ELEM(get_blk_at(q, last));
	}
	return ret;
}

/**
 * compare the extern_elem and intern_elem byte by byte
 * TODO: hidden
 */
bool compare_elements(queue *q, void *extern_elem, void *intern_elem) {
	char *ext = (char *)extern_elem;
	char *inn = (char *)intern_elem;
	size_t len = q->elem_size;
	size_t i;
	for (i = 0; i < len; i++) {
		if (*(ext+i) != *(inn+i))
			return false;
	}
	return true;
}

bool contained_in_queue(queue *q, void *extern_elem) {
	char *head_elem = get_first_elem(q);
	char *tail_elem = get_last_elem(q);
	if (!head_elem || !tail_elem)
		return false;
	bool ret = false;
	// case 1: extern_elem is in the queue
	if ((char *)extern_elem >= BLK_ELEM(q->data) && (char *)extern_elem <= BLK_ELEM(q->data + (q->queue_size - 1) * q->blk_size)) {
		if (q->head <= q->tail) {
			ret = (head_elem <= (char *)extern_elem) && (tail_elem >= (char *)extern_elem);
		} else {
			ret = ((char *)extern_elem >= tail_elem) || ((char *)extern_elem <= head_elem);
		}
	}
	// case 2: extern_elem is not in the queue
	else {
//		ERR_LOG("contained debug\n");
		char *intern_elem = get_first_elem(q);
		while (intern_elem) {
//			ERR_LOG("contained debug %p first element: %p last element: %p blk_size: %lu\n", intern_elem, get_first_elem(q), get_last_elem(q), q->blk_size);
			if (compare_elements(q, extern_elem, intern_elem)) {
				ret = true;
				break;
			}
			intern_elem = next_element(q, intern_elem);
		}
	}
	return ret;
}

void *next_element(queue *q, void *elem) {
	if (elem == 0)
		return get_first_elem(q);
	if (elem == get_last_elem(q))
		return 0;
	else {
		REQUIRES((char *)elem >= q->data && (char *)elem <= (q->data + q->blk_size * (q->queue_size - 1)));
		size_t index = ((char *)elem - q->data) / q->blk_size;
		index = ((index + 1) % q->queue_size) * q->blk_size;
		return BLK_ELEM(q->data) + index;
	}
}

size_t length_of_queue(queue *q) {
	return q->tail - q->head;
}

void enqueue(queue *q, void *elem) {
  sem_wait(&q->enqueue_mutex);
  char *tail_blk = get_tail_blk(q);
  while (BLK_TAG(tail_blk)) {
//    ERR_LOG("q->data: %p tail_blk: %p BLK_TAG: %d enqueue wait...\n", q->data, tail_blk, BLK_TAG(tail_blk));
    ERR_LOG("enqueue wait...\n");
  }
  BLK_TAG(tail_blk) = 1;
	q->tail = (q->tail + 1) % q->queue_size;
  sem_post(&q->enqueue_mutex);
	memcpy(BLK_ELEM(tail_blk), elem, q->elem_size);
}

void *dequeue(queue *q) {
  sem_wait(&q->dequeue_mutex);
  char *head_blk = get_head_blk(q);
  while (!BLK_TAG(head_blk)) {
    ERR_LOG("dequeue wait...\n");
  }
  BLK_TAG(head_blk) = 0;
	q->head = (q->head + 1) % q->queue_size;
  sem_post(&q->dequeue_mutex);
	char *elem = (char *)malloc(sizeof(char) * q->blk_size);
	memcpy(elem, BLK_ELEM(head_blk), q->elem_size);
  return elem;
}
