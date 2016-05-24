#include "patch_queue.h"
#include <pthread.h>

size_t q_size = 200;
size_t b_size = 1000;
queue q;
char *data;

void *enqueue_data(void *vargp) {
	char i;
	ERR_LOG("enq %u==========\n", pthread_self());
	char *input = (char *)malloc(sizeof(char) * b_size);
	memcpy(input, data, b_size);
	for (i = 'a'; i <= 'z'; i++) {
		*input = i;
		enqueue(&q, input);
	}
	free(input);
	ERR_LOG("enq %u==========\n", pthread_self());
	return 0;
}

void *dequeue_data(void *vargp) {
	int i;
	char *output;
	ERR_LOG("deq %u==========\n", pthread_self());
	for (i = 0; i < 26; i++) {
		output = dequeue(&q);
		ERR_LOG("output of dequeue: %c\n", *output);
		free(output);
	}
	ERR_LOG("deq %u==========\n", pthread_self());
	return 0;
}

int main() {

	initialize_queue(&q, q_size, b_size);
	data = (char *)malloc(sizeof(char) * b_size);
	memset(data, 'd', b_size);

	int i;
//  test single thread enqueue and dequeue
//	for (i = 0; i < q_size; i++) {
//		enqueue(&q, data); 					// ensure that size of what data point to is at least blk_size
//	}

//	char *output;
//	for (i = 0; i < q_size; i++) {
//		output = dequeue(&q);
//		if (output != 0) {
//			ERR_LOG("output of dequeue: %c\n", *output);
//			free(output);
//		}
//	}

	/**
	 * test multi-threaded enqueue and dequeue
	 */
	ERR_LOG("before thread create\n");
	pthread_t *q_threads = (pthread_t *)malloc(sizeof(pthread_t) * 10);
	for (i = 0; i < 5; i++) {
		pthread_create(&q_threads[i], NULL, enqueue_data, NULL);
//		pthread_detach(q_threads[i]);
	}
	for (i = 5; i < 10; i++) {
		pthread_create(&q_threads[i], NULL, dequeue_data, NULL);
//		pthread_detach(q_threads[i]);
	}

	for (i = 0; i < 10; i++) {
		pthread_join(q_threads[i], NULL);
	}
	free(q_threads);
	ERR_LOG("after thread destroy\n");

	/**
	 * test other functions
	 */
	char c;
	char *input = (char *)malloc(sizeof(char) * b_size);
	memcpy(input, data, b_size);
	for (c = 'a'; c <= 'z'; c++) {
		*input = c;
		ERR_LOG("debug %c\n", c);
		char *first = (char *)get_first_elem(&q);
		if (first)
			ERR_LOG("current first element: %c\n", *first);
		else
			ERR_LOG("first is NULL\n");
		char *last = (char *)get_last_elem(&q);
		if (last)
			ERR_LOG("current last element: %c\n", *last);
		else
			ERR_LOG("last is NULL\n");
		enqueue(&q, input);
	}
	free(input);

	{
		char *first = (char *)get_first_elem(&q);
		if (first)
			ERR_LOG("current first element: %c\n", *first);
		else
			ERR_LOG("first is NULL\n");
		char *last = (char *)get_last_elem(&q);
		if (last)
			ERR_LOG("current last element: %c\n", *last);
		else
			ERR_LOG("last is NULL\n");
	}

	bool isContained = contained_in_queue(&q, data);
	if (isContained) {
		ERR_LOG("%c data is contained in queue\n", *data);
	} else {
		ERR_LOG("%c data is not contained in queue\n", *data);
	}

	char *elem = (char *)get_first_elem(&q);
	ERR_LOG("start enumerate elements in queue:\n");
	while (elem) {
		ERR_LOG("%c\n", *elem);
		elem = next_element(&q, elem);
	}

	finalize_queue(&q);
	return 0;
}
