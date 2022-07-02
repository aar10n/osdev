//
// Created by Aaron Gill-Braun on 2022-06-21.
//

#include <init.h>
#include <queue.h>
#include <mm.h>

typedef struct callback_obj {
  init_callback_t callback;
  void *data;
  LIST_ENTRY(struct callback_obj) list;
} callback_obj_t;

LIST_HEAD(callback_obj_t) init_address_space_cb_list = LIST_HEAD_INITR;


void register_init_address_space_callback(init_callback_t callback, void *data) {
  callback_obj_t *obj = kmalloc(sizeof(callback_obj_t));
  obj->callback = callback;
  obj->data = data;
  LIST_ENTRY_INIT(&obj->list);
  LIST_ADD(&init_address_space_cb_list, obj, list);
}

void execute_init_address_space_callbacks() {
  callback_obj_t *obj;
  callback_obj_t *last = NULL;
  LIST_FOREACH(obj, &init_address_space_cb_list, list) {
    obj->callback(obj->data);

    if (last) {
      LIST_REMOVE(&init_address_space_cb_list, obj, list);
      kfree(last);
    }
    last = obj;
  }

  kfree(last);
}
