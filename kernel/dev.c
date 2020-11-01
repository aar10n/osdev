//
// Created by Aaron Gill-Braun on 2020-11-01.
//

#include <dev.h>
#include <atomic.h>
#include <rb_tree.h>
#include <stdio.h>
#include <mm/heap.h>

static dev_t __id = 1;
rb_tree_t *device_tree;

static inline dev_t alloc_id() {
  return atomic_fetch_add(&__id, 1);
}

//

void device_tree_init() {
  device_tree = create_rb_tree();
}

device_t *device_get(dev_t id) {
  rb_node_t *node = rb_tree_find(device_tree, id);
  if (node) {
    return node->data;
  }
  return NULL;
}

dev_t device_register(dev_type_t type, dev_t parent_id, const char *name,
                      pci_device_t *pci, void *data) {

  device_t *device = kmalloc(sizeof(device_t));
  device_t *parent = NULL;
  if (parent != 0) {
    parent = device_get(parent_id);
    if (parent == NULL) {
      kprintf("[dev] invalid parent id\n");
      return 0;
    }
  }

  device->id = alloc_id();
  device->type = type;
  device->name = name;
  device->pci = pci;
  device->data = data;

  device->parent = parent;
  device->child = NULL;
  device->next = NULL;

  if (parent) {
    if (parent->child) {
      parent->child->next = device;
    }
    parent->child = device;
  }

  rb_tree_insert(device_tree, device->id, device);
  return device->id;
}
