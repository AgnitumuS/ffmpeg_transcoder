#include "patch_queue.h"

size_t q_size = 100;
size_t b_size = 1000;

int main() {
	queue q;

	initialize_queue(&q, q_size, b_size);

	char *data = (char *)malloc(sizeof(char) * b_size);
	memset(data, 'd', b_size);

	int i;
	for (i = 0; i < q_size; i++)
		enqueue(&q, data); 					// ensure that size of what data point to is at least blk_size

	char *output;
	for (i = 0; i < q_size; i++) {
		output = dequeue(&q);
		if (output != 0) {
			ERR_LOG("output of dequeue: %c\n", *output);
			free(output);
		}
	}

	finalize_queue(&q);
	return 0;
}
