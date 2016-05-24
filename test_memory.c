#include "patch_memory.h"

int gop_size = 327680000;
int big_size = 311040000;

int main() {
	ss_memory_t memory;
	ss_memory_t memory1;
	initialize_ss_memory(&memory, gop_size, 2, "gop");
	initialize_ss_memory(&memory1, big_size, 2, "big");

	int i;
	char *data;
	char *arr[100];
	char *arr1[100];
	data = (char *)malloc(32768);
	memset(data, 'b', 32768);
	for (i = 0; i < 100; i++) {
		char *p = (char *)ss_malloc(&memory, gop_size);
		arr[i] = p;
		int j;
		for (j = 0; j < 10000; j++)
			memcpy(arr[i], data, 32768);
		p = (char *)ss_malloc(&memory1, big_size);
		arr1[i] = p;
	}

	for (i = 0; i < 100; i++) {
		ss_free(&memory, arr[i]);
		arr[i] = 0;
		ss_free(&memory1, arr1[i]);
		arr1[i] = 0;
	}

	finalize_ss_memory(&memory);
	finalize_ss_memory(&memory1);

	char *big = (char *)malloc(big_size);
	free(big);

	return 0;
}
