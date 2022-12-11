//
// Created by Aaron Gill-Braun on 2022-06-21.
//

#include <init.h>
#include <queue.h>
#include <mm.h>

#include <panic.h>

typedef struct callback_obj {
  init_callback_t callback;
  void *data;
  LIST_ENTRY(struct callback_obj) list;
} callback_obj_t;

LOAD_SECTION(__init_array, ".init_array");
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
  LIST_FOREACH(obj, &init_address_space_cb_list, list) {
    obj->callback(obj->data);
    kfree(obj);
  }
  
  LIST_INIT(&init_address_space_cb_list);
}

//

void do_module_initializers() {
  // execute all of the module initializers
  if (__init_array.virt_addr == 0) {
    panic("something went wrong");
  }

  void (**init_funcs)() = (void *) __init_array.virt_addr;
  size_t num_init_funcs = __init_array.size / sizeof(void *);
  for (size_t i = 0; i < num_init_funcs; i++) {
    init_funcs[i]();
  }
}
