//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#ifndef FS_DEVICE_H
#define FS_DEVICE_H

#include <base.h>

#define FS_MAX_DEVICES 64

typedef struct fs_device fs_device_t;

typedef enum fs_dev_type {
  DEV_FB,  // framebuffer
  DEV_HD,  // hard drive
  DEV_TTY, // serial port
  DEV_SD,  // scsi drive
} fs_dev_type_t;

/* a generic filesystem device */
typedef struct fs_device {
  dev_t id;           // device id
  dev_t num;          // device number
  fs_dev_type_t type; // device type
  char *name;         // device name
  void *driver;       // device driver
} fs_device_t;


void fs_device_init();
dev_t fs_register_device(fs_dev_type_t type, void *driver);
// dev_t fs_register_partition(fs_device_t *device, dev_t partition);

#endif
