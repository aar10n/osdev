//
// Created by Aaron Gill-Braun on 2020-11-01.
//

#ifndef KERNEL_DEV_H
#define KERNEL_DEV_H

#include <base.h>
#include <bus/pci.h>

//
// System device database
//

typedef enum {
  AHCI_STORAGE_CONTROLLER,
  AHCI_STORAGE_DEVICE,
} dev_type_t;

typedef struct device {
  dev_t id;              // device id
  dev_type_t type;       // device type
  const char *name;      // device name
  pci_device_t *pci;     // device's pci struct
  void *data;            // device specific data

  struct device *parent; // parent devices
  struct device *child;  // last child device
  struct device *next;   // next device of same type
} device_t;

void device_tree_init();
device_t *device_get(dev_t id);
dev_t device_register(dev_type_t type, dev_t parent_id, const char *name,
                      pci_device_t *pci, void *data);

#endif
