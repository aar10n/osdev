//
// Created by Aaron Gill-Braun on 2021-07-10.
//

#include <list.h>
#include <mm.h>

list_head_t *list_init() {
  list_head_t *head = kmalloc(sizeof(list_head_t));
  head->first = NULL;
  head->last = NULL;
  spin_init(&head->lock);
  return head;
}

void list_add(list_head_t *list, void *data) {
  list_node_t *node = kmalloc(sizeof(list_node_t));
  node->next = NULL;
  node->prev = NULL;
  node->data = data;
  spin_lock(&list->lock);
  if (list->first == NULL) {
    list->first = node;
    list->last = node;
  } else {
    node->prev = list->last;
    list->last->next = node;
    list->last = node;
  }
  spin_unlock(&list->lock);
}

void *list_remove(list_head_t *list, list_node_t *node) {
  void *data = node->data;
  spin_lock(&list->lock);
  if (node == list->first) {
    if (node->next == NULL) {
      list->first = NULL;
      list->last = NULL;
    } else {
      node->next->prev = node->prev;
      list->first = node->next;
    }
  } else if (node == list->last) {
    node->prev->next = node->next;
    list->last = node->prev;
  } else {
    node->next->prev = node->prev;
    node->prev->next = node->next;
  }
  spin_unlock(&list->lock);
  kfree(node);
  return data;
}
