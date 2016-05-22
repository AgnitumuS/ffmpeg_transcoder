#ifndef _LIST_H
#define _LIST_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct ListNode {
	void *elem;
	struct ListNode *next;
}ListNode;

ListNode * new_empty_ListNode();
ListNode * new_ListNode(void *elem);

typedef struct List {
	ListNode *start;
	ListNode *end;
}List;

List *new_List();
bool is_empty_List(List *l);

// EFFECT: insert the given node in the end of the given List
List *insert_List(ListNode *node, List *l);

// RETURNS: true iff if l contains elem
bool is_contain_List(void *elem, List *l);

// WHERE: l has to be sorted
// EFFECT: insert the given node in the the given List and keep the list
// sorted by cmp with smaller node closer to the end
List *insert_by_order_List(ListNode *node, List *l,
		int (*cmp)(ListNode *n1, ListNode *n2));

// EFFECT: append l2 to the end of l1
// RETURNS: the result list
List *append_List(List *l1, List *l2);

// EFFECT: remove the first node in the given List
// RETURNS: the first node;
ListNode *remove_List(List *l);

// WHERE: l contains a node that conatins element equals to elem
// EFFECT: remove the list node whose element equals to the given one
void remove_elem_List(List *l, void *elem);
void *get_first_elem_List(List *l);

int length_List(List *l);

void *xmalloc(size_t size);

void *xcalloc(size_t nobj, size_t size);

#endif
