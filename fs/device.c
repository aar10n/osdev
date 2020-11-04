//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#include <device.h>
#include <atomic.h>
#include <stdio.h>

static id_t __driver_id = 0;
static fs_device_driver_t drivers[FS_MAX_DEVICE_DRIVERS] = {};
static dev_t __device_id = 0;
static fs_device_t devices[FS_MAX_DEVICES] = {};

static inline id_t alloc_driver_id() {
  return atomic_fetch_add(&__driver_id, 1);
}

static inline dev_t alloc_device_id() {
  return atomic_fetch_add(&__device_id, 1);
}

//

id_t fs_register_device_driver(fs_device_driver_t driver) {
  if (__driver_id >= FS_MAX_DEVICE_DRIVERS) {
    errno = EOVERFLOW;
    return FS_MAX_DEVICE_DRIVERS + 1;
  }

  id_t id = alloc_driver_id();
  drivers[id] = driver;
  drivers[id].id = id;
  return id;
}

//

int fs_discover_devices() {
  if (__driver_id == 0) {
    return 0;
  }

  kprintf("[devices] discovering devices\n");

  dev_t dev_id = alloc_device_id();
  for (int i = 0; i < __driver_id; i++) {
    if (dev_id > FS_MAX_DEVICES) {
      errno = EOVERFLOW;
      return -1;
    }

    fs_device_driver_t *driver = &(drivers[i]);
    kprintf("[devices] %s: discovering\n", driver->name);

    fs_controller_t *controller = driver->c_impl->init(dev_id);
    if (controller == NULL) {
      // ENODEV means that no controllers for that driver
      // are present in the system. We only return early
      // if an error other than ENODEV is signaled.
      if (errno != ENODEV) {
        return -1;
      }

      continue;
    }

    // this is a bit hacky but we store the controller
    // as a normal device but set the data and impl fields
    // NULL. This is only so we have reference to it somewhere
    // even if no storage devices are attached.
    devices[dev_id].id = dev_id;
    devices[dev_id].type = FS_STORAGE_CONTROLLER;
    devices[dev_id].data = NULL;
    devices[dev_id].controller = controller;
    devices[dev_id].impl = NULL;

    // any discovered storage devices should be in the `devices`
    // field in the controller struct. now we need to register
    // each device
    fs_device_list_t *item = controller->devices;
    while (item) {
      void *data = item->data;

      dev_t id = alloc_device_id();
      if (id > FS_MAX_DEVICES) {
        errno = EOVERFLOW;
        return -1;
      }

      fs_device_t *device = driver->d_impl->init(id, data, controller);
      if (device == NULL) {
        return -1;
      }

      devices[id] = *device;
      devices[id].type = FS_STORAGE_DEVICE;
      kprintf("[device] %s: device registered (%d)\n", driver->name, id);
      item = item->next;
    }

    dev_id = alloc_device_id();
  }

  kprintf("[devices] done!\n");
  return 0;
}

//

fs_device_driver_t *fs_get_driver(id_t id) {
  if (id >= __driver_id) {
    errno = EINVAL;
    return NULL;
  }
  return &(drivers[id]);
}

fs_device_t *fs_get_device(dev_t id) {
  if (id >= __device_id) {
    errno = ENODEV;
    return NULL;
  }

  fs_device_t *device = &(devices[id]);
  if (device->type != FS_STORAGE_DEVICE) {
    errno = ENOTBLK;
    return NULL;
  }

  return device;
}
