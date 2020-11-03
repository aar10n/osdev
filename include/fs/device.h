//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#ifndef FS_DEVICE_H
#define FS_DEVICE_H

#include <base.h>
#include <bus/pci.h>

#define FS_MAX_DEVICE_DRIVERS 16
#define FS_MAX_DEVICES 32

typedef struct fs_controller fs_controller_t;
typedef struct fs_device fs_device_t;

typedef enum fs_device_type {
  FS_STORAGE_DEVICE,
  FS_STORAGE_CONTROLLER
} fs_device_type_t;

/* Storage device controller operations */
typedef struct fs_controller_impl {
  fs_controller_t *(*init)(dev_t id);
} fs_controller_impl_t;

/* Storage device operations */
typedef struct fs_device_impl {
  fs_device_t *(*init)(dev_t id, void *data, fs_controller_t *controller);
  ssize_t (*read)(fs_device_t *device, uint64_t lba, uint32_t seccount, void **buf);
  ssize_t (*write)(fs_device_t *device, uint64_t lba, uint32_t seccount, void **buf);
  int (*release)(fs_device_t *device, void *buf);
} fs_device_impl_t;

/* Discovered to-be registered device list */
typedef struct fs_device_list {
  void *data; // device data
  struct fs_device_list *next;
} fs_device_list_t;

/* A storage device driver */
typedef struct fs_device_driver {
  id_t id;                      // driver id
  const char *name;             // device name
  uint32_t cap;                 // device capabilities
  fs_controller_impl_t *c_impl; // device controller operations
  fs_device_impl_t *d_impl;     // device operations
} fs_device_driver_t;

/* A storage device controller */
typedef struct fs_controller {
  dev_t id;                   // controller device id
  fs_device_type_t type;      // FS_STORAGE_CONTROLLER
  fs_device_list_t *devices;  // child device list
  void *data;                 // device controller specific data
  fs_controller_impl_t *impl; // device controller operations
  fs_device_driver_t *driver; // storage device driver
} fs_controller_t;

/* A storage device */
typedef struct fs_device {
  dev_t id;                    // device id
  fs_device_type_t type;       // FS_STORAGE_DEVICE
  void *data;                  // device specific data
  fs_device_impl_t *impl;      // device operations
  fs_controller_t *controller; // device controller
} fs_device_t;


id_t fs_register_device_driver(fs_device_driver_t driver);
int fs_discover_devices();

fs_device_driver_t *fs_get_driver(id_t id);
fs_device_t *fs_get_device(dev_t id);

#endif
