#include "patch_memory.h"

int gop_size = 100;

int main() {
	ss_memory_t memory;
	initialize_ss_memory(&memory, gop_size, 2, "gop");

	int i;
	char *arr[100];
	for (i = 0; i < 100; i++) {
		char *p = (char *)ss_malloc(&memory, gop_size);
		arr[i] = p;
	}

	for (i = 0; i < 100; i++) {
		ss_free(&memory, arr[i]);
		arr[i] = 0;
	}

	finalize_ss_memory(&memory);
}
