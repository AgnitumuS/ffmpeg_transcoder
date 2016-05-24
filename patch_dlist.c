#include <stdlib.h>
#include <stdbool.h>
#include "patch_dlist.h"
#include "contracts.h"

void * xmalloc(size_t size) {
	void *p = malloc(size);
	if (p == NULL) {
		fprintf(stderr, "Allocation failed.\n");
		abort();
	}
	return p;
}

void *xcalloc(size_t nobj, size_t size) {
	void *p = calloc(nobj, size);
	if (p == NULL) {
		fprintf(stderr, "Allocation failed.\n");
		abort();
	}
	return p;
}

dlist_node_t *new_empty_ListNode() {
	dlist_node_t *lon = xmalloc(sizeof(dlist_node_t));
	lon->elem = NULL;
	lon->prev = NULL;
	lon->next = NULL;
	return lon;
}

dlist_node_t *new_ListNode(void *elem) {
	dlist_node_t *lon = xmalloc(sizeof(dlist_node_t));
	lon->elem = elem;
	lon->prev = NULL;
	lon->next = NULL;
	return lon;
}


dlist_t *new_List() {
	dlist_t *l = xmalloc(sizeof(dlist_t));
	l->start = NULL;
	return l;
}

bool is_empty_List(dlist_t *l) {
	return (l->start == NULL); 
}

// RETURNS: true iff if l contains elem
bool is_contain_List(void *elem, dlist_t *l) {
	REQUIRES(l != NULL);

	if (is_empty_List(l)) {
		return false;
	} else {
		ListNode *curr = l->start;
		while(true) {
			if (curr->elem == elem) {
				return true;
			} else if (curr == l->end) {
				return false;
			} else {
				curr = curr->next;
			}
		}
	}
}

// EFFECT: insert the given node in the end of the given List
List *insert_List(ListNode *node, List *l) {
	REQUIRES(node != NULL);
	REQUIRES(l != NULL);

	if (is_empty_List(l)) {
		l->start = node;
		l->end = node;
		return l;
	} else {
		l->end->next = node;
		l->end = node;
		return l;
	}
}

// WHERE: l has to be sorted
// EFFECT: insert the given node in the the given List and keep the list
// sorted by cmp with smaller node closer to the start
List *insert_by_order_List(ListNode *node, List *l,
		int (*cmp)(ListNode *n1, ListNode *n2)) {
	REQUIRES(l != NULL);
	REQUIRES(node != NULL);

	if (is_empty_List(l)) {
		l->start = node;
		l->end = node;
		return l;
	} else {
		ListNode *prev = NULL, *curr = l->start;
		while(true) {
			int r = cmp(node, curr);
			if (r <= 0) {
				if (prev == NULL) {
					node->next = l->start;
					l->start = node;
					return l;
				} else {
					node->next = curr;
					prev->next = node;
					return l;
				}
			} else {
				if (curr == l->end) {
					curr->next = node;
					node->next = NULL;
					l->end = node;
				} else {
					prev = curr;
					curr = curr->next;
				}
			}
		}
	}
}

// EFFECT: append l2 to the end of l1
// RETURNS: the result list
List *append_List(List *l1, List *l2) {
	REQUIRES(l1 != NULL);
	REQUIRES(l2 != NULL);

	if (is_empty_List(l1)) {
		l1->start = l2->start;
		l1->end = l2->end;
		return l1;
	} else {
		l1->end->next = l2->start;
		l1->end = l2->end;
		return l1;
	}
}

// EFFECT: remove the first node from the start of the list
// RETURNS: the first node;
ListNode *remove_List(List *l) {
	REQUIRES(is_empty_List(l) == false);

	ListNode *node = l->start;
	if (l->start == l->end) {
		l->start = NULL;
		l->end = NULL;
	} else {
		l->start = l->start->next;
	}
	return node;
}

// WHERE: l contains a node whose element equals to elem
// EFFECT: remove the list node whose element equals to the given one
void remove_elem_List(List *l, void *elem) {
	ENSURES(elem != NULL);
	ENSURES(!is_empty_List(l));

	ListNode *prev = l->start, *curr = l->start;
	while (true) {
		if (curr->elem == elem) {
			if (prev == curr) {// equal the first node
				if (prev == l->end) { // only one node
					l->start = NULL;
					l->end = NULL;
					free(curr);
					return;
				} else {
					l->start = l->start->next;
					free(curr);
					return;
				}
			}
			if (curr == l->end) {// equal the last node
				prev->next = NULL;
				l->end = prev;
				free(curr);
				return;
			} else {
				prev->next = curr->next;
				free(curr);
				return;
			}
		} else {
			ENSURES(curr != l->end);
			if(curr == l->end) {
				return;
			} else {
				prev = curr;
				curr = curr->next;
			}
		}
	}
}

void *get_first_elem_List(List *l) {
	REQUIRES(is_empty_List(l) == false);
	return l->start->elem;
}

int length_List(List *l) {
	REQUIRES(l != NULL);

	if (is_empty_List(l)) {
		return 0;
	} else {
		int len = 1;
		ListNode *curr = l->start;

		// LOOP_INVARIANT: len represents the length of sublist [l.start, curr]
		while(curr != l->end) {
			curr = curr->next;
			len++;
		}
		return len;
	}
}
