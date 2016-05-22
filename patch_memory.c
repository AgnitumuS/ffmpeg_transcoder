#include "patch_memory.h"

/**
 * @param memory: the memory to be initialized
 * @param unit_size: the unit size of a single block
 * @param memory_size: the total number of memory blocks in this memory
 */
void initialize_ss_memory(ss_memory_t *memory, int unit_size, int memory_size, char *memory_name) {
	memory->name = (char *)malloc(30);
	strcpy(memory->name, memory_name);
  memory->unit_size = unit_size;
  memory->blk_size = unit_size + sizeof(void *);         // block should contain a pointer to next block and data unit
  memory->memory_size = memory_size;
  memory->free_list = 0;
  memory->data = (char *)xmalloc(sizeof(char) * memory->blk_size * memory_size);
  memset(memory->data, 0, memory->blk_size * memory_size);
  // TODO: initialize free list
  int i;
  for (i = 0; i < memory->memory_size; i++) {
    ss_free(memory, memory->data + sizeof(void *) + i * memory->blk_size);
  }
	ERR_LOG("memory %s is initialized.\n", memory->name);
}

void finalize_ss_memory(ss_memory_t *memory) {
  free(memory->data);
	ERR_LOG("memory %s is finalized.\n", memory->name);
}

void *ss_malloc(ss_memory_t *memory, size_t size) {
  if (size == 0) {
		ERR_LOG("%s malloc, empty block is allocated.\n", memory->name);
    return memory->data;
	}
  if (size != memory->unit_size) {
    // TODO: error
		ERR_LOG("%s malloc, error! size: %d, desired size: %d.\n", memory->name, (int)size, memory->unit_size);
    return 0;
  }
  if (memory->free_list) {
    char *old_head = memory->free_list;
    memory->free_list = *((void **) old_head);        // the first chunk of memory is pointer to the next free block in free list 
		ERR_LOG("%s malloc, 0x%lx is allocated\n", memory->name, (long)(old_head + sizeof(void *)));
    return (old_head) + sizeof(void *);
  } else {
    ss_expand_memory(memory);
    char *old_head = memory->free_list;
    memory->free_list = *((void **) old_head);
		ERR_LOG("%s malloc, 0x%lx is allocated\n", memory->name, (long)(old_head + sizeof(void *)));
    return (old_head) + sizeof(void *);
  }
}

void ss_free(ss_memory_t *memory, void *p) {
  if (p == memory->data) {       // memory->data[0] is kept for malloc of size 0
		ERR_LOG("%s free, 0x%lx is empty block\n", memory->name, (long)p);
    return ;
	}
  if (((char *)p) < memory->data || ((char *)p) >= (memory->data + memory->blk_size * memory->memory_size)) {
    // TODO: error
		ERR_LOG("%s free, 0x%lx is out of range\n", memory->name, (long)p);
    return ;
  }
	ERR_LOG("%s free, 0x%lx is freed\n", memory->name, (long)p);
  NEXT(p) = memory->free_list;
  memory->free_list = GET_BLK_ADDR(p);
}

void ss_expand_memory(ss_memory_t *memory) {
  int old_mem_size = memory->memory_size;
  int new_mem_size = old_mem_size * 2;
  ERR_LOG("memory expand from %d to %d\n", old_mem_size, new_mem_size);
  char *new_data = (char *)realloc(memory->data, new_mem_size);
  memory->memory_size = new_mem_size;
  memory->data = new_data;
  int i;
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
