#ifndef PATCH_MEMORY_H
#define PATCH_MEMORY_H

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "list.h"
#include "debug.h"

#define GET_BLK_ADDR(DATA_ADDR) (((char *)DATA_ADDR)-sizeof(void *))
#define NEXT(DATA_ADDR) *(((void **)DATA_ADDR) - 1) 

/**
 * a memory structure for types that share the same size
 */
typedef struct ss_memory {
  char *name;             	// the name of the memory, no more than 30 alphabet
  size_t unit_size;         // the unit size of each type, does not equal memory block size
  size_t blk_size;          // the size of each memory block
  size_t memory_size;       // the total number of memory blocks in this memory
  char *free_list;        	// pointer to the head of free list
  char *data;             	// the memory where actual data is stored
} ss_memory_t;

/**
 * @param memory: the memory to be initialized
 * @param unit_size: the unit size of a single block
 * @param memory_size: the total number of memory blocks in this memory
 */
void initialize_ss_memory(ss_memory_t *memory, size_t unit_size, size_t memory_size, char *memory_name);
/**
 * free the memory
 */
void finalize_ss_memory(ss_memory_t *memory);
/**
 * allocate a memory block
 */
void *ss_malloc(ss_memory_t *memory, size_t size);
/**
 * free the previous allocated block
 */
void ss_free(ss_memory_t *memory, void *p);

// TODO: expand and shrink not supported
#endif
