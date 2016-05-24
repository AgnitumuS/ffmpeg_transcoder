#include "patch_memory.h"

/**
 * @param memory: the memory to be initialized
 * @param unit_size: the unit size of a single block
 * @param memory_size: the total number of memory blocks in this memory
 */
void initialize_ss_memory(ss_memory_t *memory, size_t unit_size, size_t memory_size, char *memory_name) {
	memory->name = (char *)malloc(30);
	strcpy(memory->name, memory_name);
  memory->unit_size = unit_size;
  memory->blk_size = unit_size + sizeof(void *);         // block should contain a pointer to next block and data unit
  memory->memory_size = memory_size;
  memory->free_list = 0;
  memory->data = (char *)malloc(sizeof(char) * memory->blk_size * memory_size);
  memset(memory->data, 0, memory->blk_size * memory_size);
  // TODO: initialize free list
  size_t i;
  for (i = 0; i < memory->memory_size; i++) {
    ss_free(memory, memory->data + sizeof(void *) + i * memory->blk_size);
  }
  // FIXME: debug
//  for (i = 0; i < memory->memory_size; i++) {
    //fprintf(stdout, "data 0x%lx : 0x%lx\n", (long)(memory->data + i * memory->blk_size), *(unsigned long *)(memory->data + i*memory->blk_size));
//  }
//	FILE_LOG(stdout, "memory %s is initialized.\n", memory->name);
}

void finalize_ss_memory(ss_memory_t *memory) {
  free(memory->data);
//	FILE_LOG(stdout, "memory %s is finalized.\n", memory->name);
}

void *ss_malloc(ss_memory_t *memory, size_t size) {
  if (size == 0) {
//		FILE_LOG(stdout, "%s malloc, empty block is allocated.\n", memory->name);
    return memory->data;
	}
  if (size != memory->unit_size) {
    // TODO: error
//		FILE_LOG(stdout, "%s malloc, error! size: %lu, desired size: %lu.\n", memory->name, size, memory->unit_size);
    return 0;
  }
  if (memory->free_list) {
    char *old_head = memory->free_list;
    memory->free_list = *((void **) old_head);        // the first chunk of memory is pointer to the next free block in free list 
//		FILE_LOG(stdout, "%s malloc, 0x%lx is allocated\n", memory->name, (long)(old_head + sizeof(void *)));
    // FIXME: debug
//    fprintf(stdout, "mem free_list head: 0x%lx\n", (unsigned long)memory->free_list);
//    int i;
//    for (i = 0; i < memory->memory_size; i++) {
//      fprintf(stdout, "data 0x%lx : 0x%lx\n", (long)(memory->data + i * memory->blk_size), *(unsigned long *)(memory->data + i*memory->blk_size));
//    }
    return (old_head) + sizeof(void *);
  } else {
    ss_expand_memory(memory);
    char *old_head = memory->free_list;
    memory->free_list = *((void **) old_head);
//		FILE_LOG(stdout, "%s malloc, 0x%lx is allocated\n", memory->name, (long)(old_head + sizeof(void *)));
    // FIXME: debug
//    fprintf(stdout, "mem free_list head: 0x%lx\n", (unsigned long)memory->free_list);
//    int i;
//    for (i = 0; i < memory->memory_size; i++) {
//      fprintf(stdout, "data 0x%lx : 0x%lx\n", (long)(memory->data + i * memory->blk_size), *(unsigned long *)(memory->data + i*memory->blk_size));
//    }
    return (old_head) + sizeof(void *);
  }
}

void ss_free(ss_memory_t *memory, void *p) {
  if (p == memory->data) {       // memory->data[0] is kept for malloc of size 0
//		FILE_LOG(stdout, "%s free, 0x%lx is empty block\n", memory->name, (long)p);
    return ;
	}
  if (((char *)p) < memory->data || ((char *)p) >= (memory->data + memory->blk_size * memory->memory_size)) {
    // TODO: error
//		FILE_LOG(stdout, "%s free, 0x%lx is out of range\n", memory->name, (long)p);
    return ;
  }
//	FILE_LOG(stdout, "%s free, 0x%lx is freed\n", memory->name, (long)p);
  NEXT(p) = memory->free_list;
  memory->free_list = GET_BLK_ADDR(p);
  // FIXME: debug
//  fprintf(stdout, "mem free_list head: 0x%lx\n", (unsigned long)memory->free_list);
//  int i;
//  for (i = 0; i < memory->memory_size; i++) {
//    fprintf(stdout, "data 0x%lx : 0x%lx\n", (long)(memory->data + i * memory->blk_size), *(unsigned long *)(memory->data + i*memory->blk_size));
//  }
}

void ss_expand_memory(ss_memory_t *memory) {
  size_t old_mem_size = memory->memory_size;
  size_t new_mem_size = old_mem_size * 2;
//  FILE_LOG(stdout, "%s memory expand from %lu to %lu\n", memory->name, old_mem_size, new_mem_size);
  char *new_data = (char *)realloc(memory->data, new_mem_size * memory->blk_size);
  memory->memory_size = new_mem_size;
  memory->data = new_data;
  size_t i;
  // FIXME: debug
//  fprintf(stdout, "new data start address: 0x%lx\n", (long)new_data);
//  for (i = 0; i < memory->memory_size; i++) {
//    fprintf(stdout, "data 0x%lx : 0x%lx\n", (long)(memory->data + i * memory->blk_size), *(unsigned long *)(memory->data + i*memory->blk_size));
//  }
//  fprintf(stdout, "debug\n");
  for (i = old_mem_size; i < new_mem_size; i++) {
    ss_free(memory, memory->data + sizeof(void *) + i * memory->blk_size);
  }
}

/**
 * to make unlink effective, a two way list is needed
 */
void ss_unlink(char *p_node) {
  
}

/**
 * not finished
 */
void ss_shrink_memory(ss_memory_t *memory) {
  int old_mem_size = memory->memory_size;
  int new_mem_size = old_mem_size / 2;
  if (new_mem_size == 0) new_mem_size = 1;
  char *new_data = (char *)realloc(memory->data, new_mem_size);
  memory->memory_size = new_mem_size;
  memory->data = new_data;
  char *p_node = memory->free_list;
  while (p_node) {
    if (p_node >= (memory->data + memory->memory_size * memory->blk_size))
      ss_unlink(p_node);
    p_node = NEXT(p_node + sizeof(void *));
  }
}
