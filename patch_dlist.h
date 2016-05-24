#ifndef PATCH_DLIST_H
#define PATCH_DLIST_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct dlist_node {
	void *elem;
	struct dlist_node *prev;
	struct dlist_node *next;
} dlist_node_t;

typedef struct dlist {
	dlist_node_t *start;
} dlist_t;

dlist_t *new_List();
bool is_empty_list(dlist_t *);
dlist_t *insert_List(dlist_node_t *, dlist_t *);
bool is_contain_list(void *, dlist_t *);
dlist_t *insert_by_order_List(dlist_node_t *, dlist_t *, int (*cmp)(dlist_node_t *, dlist_node_t *));
dlist_t *append_List(dlist_t *, dlist_t *);
dlist_node_t *remove_List(dlist_t *);
void remove_elem_List(dlist_t *, void *);
void *get_first_elem_List(dlist_t *);
int length_List(dlist_t *);
void *xmalloc(size_t size);
void *xcalloc(size_t nobj, size_t size);
#endif
