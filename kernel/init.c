//
// Created by Aaron Gill-Braun on 2022-06-21.
//

#include <kernel/init.h>
#include <kernel/mm.h>

#include <kernel/panic.h>

typedef struct callback_obj {
  init_callback_t callback;
  void *data;
  LIST_ENTRY(struct callback_obj) list;
} callback_obj_t;

LOAD_SECTION(__early_init_array, ".init_array.early");
LOAD_SECTION(__percpu_early_init_array, ".init_array.early_percpu");
LOAD_SECTION(__static_init_array, ".init_array.static");
LOAD_SECTION(__percpu_static_init_array, ".init_array.static_percpu");
LOAD_SECTION(__module_init_array, ".init_array.module");
LIST_HEAD(callback_obj_t) init_address_space_cb_list;

void register_init_address_space_callback(init_callback_t callback, void *data) {
  callback_obj_t *obj = kmallocz(sizeof(callback_obj_t));
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

void do_early_initializers() {
  // execute all of the early initializers
  if (__early_init_array.virt_addr == 0) {
    panic("failed to load early initializers");
  }

  void (**init_funcs)() = (void *) __early_init_array.virt_addr;
  size_t num_init_funcs = __early_init_array.size / sizeof(void *);
  for (size_t i = 0; i < num_init_funcs; i++) {
    init_funcs[i]();
  }
}

void do_percpu_early_initializers() {
  // execute all of the percpu early initializers
  if (__percpu_early_init_array.virt_addr == 0) {
    panic("failed to load percpu early initializers");
  }

  void (**init_funcs)() = (void *) __percpu_early_init_array.virt_addr;
  size_t num_init_funcs = __percpu_early_init_array.size / sizeof(void *);
  for (size_t i = 0; i < num_init_funcs; i++) {
    init_funcs[i]();
  }
}


void do_static_initializers() {
  // execute all of the static initializers
  if (__static_init_array.virt_addr == 0) {
    panic("no static initializers found");
  }

  void (**init_funcs)() = (void *) __static_init_array.virt_addr;
  size_t num_init_funcs = __static_init_array.size / sizeof(void *);
  for (size_t i = 0; i < num_init_funcs; i++) {
    init_funcs[i]();
  }
}

void do_percpu_static_initializers() {
  // execute all of the percpu static initializers
  if (__percpu_static_init_array.virt_addr == 0) {
    // no percpu static initializers
    return;
  }

  void (**init_funcs)() = (void *) __percpu_static_init_array.virt_addr;
  size_t num_init_funcs = __percpu_static_init_array.size / sizeof(void *);
  for (size_t i = 0; i < num_init_funcs; i++) {
    init_funcs[i]();
  }
}

void do_module_initializers() {
  // execute all of the module initializers
  if (__module_init_array.virt_addr == 0) {
    panic("failed to load module initializers");
  }

  void (**init_funcs)() = (void *) __module_init_array.virt_addr;
  size_t num_init_funcs = __module_init_array.size / sizeof(void *);
  for (size_t i = 0; i < num_init_funcs; i++) {
    init_funcs[i]();
  }
}
