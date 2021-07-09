//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#include <device.h>
#include <atomic.h>
#include <printf.h>
#include <vfs.h>
#include <mm.h>
#include <rb_tree.h>

rb_tree_t *device_tree;

static inline dev_t next_device_id() {
  rb_iter_t *iter = rb_tree_iter(device_tree);
  rb_node_t *node;

  dev_t id = 0;
  while ((node = rb_iter_next(iter))) {
    fs_device_t *device = node->data;
    if (device->id > id) {
      break;
    }
    id = device->id + 1;
  }
  return id;
}

static inline dev_t next_device_num(fs_dev_type_t type) {
  rb_iter_t *iter = rb_tree_iter(device_tree);
  rb_node_t *node;

  dev_t num = 0;
  while ((node = rb_iter_next(iter))) {
    fs_device_t *device = node->data;
    if (device->type != type) {
      continue;
    }

    if (device->num > num) {
      break;
    }
    num = device->num + 1;
  }
  return num;
}

static char *get_device_name(fs_dev_type_t type, dev_t num) {
  const char *prefix;
  bool letter = false;
  switch (type) {
    case DEV_FB:
      prefix = "fb";
      break;
    case DEV_HD:
      prefix = "hd";
      letter = true;
      break;
    case DEV_TTY:
      prefix = "tty";
      break;
    case DEV_SD:
      prefix = "sd";
      letter = true;
      break;
    default:
      return NULL;
  }

  char *buffer = kmalloc(8);
  if (letter) {
    ksnprintf(buffer, 8, "%s%c", prefix, num + 'a');
  } else {
    ksnprintf(buffer, 8, "%s%d", prefix, num);
  }
  return buffer;
}


void fs_device_init() {
  device_tree = create_rb_tree();

  // add special devices (null, random, zero)
}


dev_t fs_register_device(fs_dev_type_t type, void *driver) {
  if (device_tree->nodes >= FS_MAX_DEVICES) {
    errno = EOVERFLOW;
    return -1;
  }

  dev_t id = next_device_id();
  dev_t num = next_device_num(type);
  char *name = get_device_name(type, num);

  kprintf("[fs] registering device (%s)\n", name);
  fs_device_t *device = kmalloc(sizeof(fs_device_t));
  device->id = id;
  device->num = num;
  device->type = type;
  device->name = name;
  device->driver = driver;
  if (vfs_add_device(device) < 0) {
    return -1;
  }
  return device->id;
}
