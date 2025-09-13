
//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#include <kernel/device.h>
#include <kernel/chan.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#include <kernel/vfs_types.h>
#include <kernel/vfs/file.h>
#include <kernel/vfs/vnode.h>

#include <rb_tree.h>

#define HMAP_TYPE void *
#include <hash_map.h>

#define ASSERT(x) kassert(x)
//#define DPRINTF(fmt, ...) kprintf("device: " fmt, ##__VA_ARGS__)
#define DPRINTF(fmt, ...)
#define EPRINTF(fmt, ...) kprintf("device: %s: " fmt, __func__, ##__VA_ARGS__)

#define DEV_FOPS(device, op) ((device)->f_ops && (device)->f_ops->op)

struct bus_type {
  const char *name;

  int last_number;
  LIST_HEAD(struct device_bus) buses;
  LIST_HEAD(struct device_driver) drivers;
  hash_map_t *driver_by_name;
  mtx_t lock;
};

struct dev_type {
  const char *name;
  uint8_t major;
  enum dtype type;
  struct device_ops *ops; // single-driver device types

  uint8_t last_minor;
  mtx_t lock;
  LIST_HEAD(struct device) devices;
};

struct bus_type bus_types[] = {
  { "pci" },
  { "usb" },
};

#define DECLARE_DEV_TYPE(_name, _maj, _typ) [_maj] = { .name = (_name), .major = (_maj), .type = (_typ) }
struct dev_type dev_types[] = {
  { /* reserved */ },
  DECLARE_DEV_TYPE("ramdisk" , 1, D_BLK),
  DECLARE_DEV_TYPE("serial"  , 2, D_CHR),
  DECLARE_DEV_TYPE("memory"  , 3, D_CHR),
  DECLARE_DEV_TYPE("loop"    , 4, D_BLK),
  DECLARE_DEV_TYPE("framebuf", 5, D_BLK),
  DECLARE_DEV_TYPE("input"   , 6, D_CHR),
};
#undef DECLARE_DEV_TYPE

chan_t *device_events;
static rb_tree_t *device_tree;
static mtx_t device_tree_lock;
static hash_map_t *bus_type_by_name;
static hash_map_t *dev_type_by_name;

static struct device_ops dev_null_ops = {};

static inline bool is_valid_device_bus(device_bus_t *bus) {
  if (bus->name == NULL) {
    DPRINTF("invalid bus: missing name\n");
    return false;
  } else if (bus->probe == NULL) {
    DPRINTF("invalid bus: missing probe function\n");
    return false;
  }
  return true;
}

static inline bool is_valid_device_driver(device_driver_t *driver) {
  if (driver->name == NULL) {
    DPRINTF("invalid driver: missing name\n");
    return false;
  } else if (driver->check_device == NULL) {
    DPRINTF("invalid driver: missing check_device function\n");
    return false;
  } else if (driver->setup_device == NULL) {
    DPRINTF("invalid driver: missing setup_device function\n");
    return false;
  } else if (driver->remove_device == NULL) {
    DPRINTF("invalid driver: missing remove_device function\n");
    return false;
  }
  return true;
}

//

static void device_static_init() {
  device_events = chan_alloc(256, sizeof(struct device_event), 0, "device_events");
  device_tree = create_rb_tree();
  mtx_init(&device_tree_lock, MTX_SPIN, "device_tree_lock");

  bus_type_by_name = hash_map_new();
  for (int i = 0; i < ARRAY_SIZE(bus_types); i++) {
    bus_types[i].driver_by_name = hash_map_new();
    mtx_init(&bus_types[i].lock, 0, "bus_type_lock");
    hash_map_set(bus_type_by_name, bus_types[i].name, (void *) &bus_types[i]);
  }

  dev_type_by_name = hash_map_new();
  for (int i = 1; i < ARRAY_SIZE(dev_types); i++) {
    dev_types[i].last_minor = 0;
    mtx_init(&dev_types[i].lock, 0, "dev_type_lock");
    hash_map_set(dev_type_by_name, dev_types[i].name, (void *) &dev_types[i]);
  }
}
STATIC_INIT(device_static_init);

//
// MARK: Device API
//

device_t *alloc_device(void *data, struct device_ops *ops, struct file_ops *f_ops) {
  device_t *dev = kmallocz(sizeof(device_t));
  dev->data = data;
  dev->ops = ops;
  dev->f_ops = f_ops;
  return dev;
}

device_t *free_device(device_t *dev) {
  ASSERT(dev != NULL);
  ASSERT(dev->bus_device == NULL);
  ASSERT(dev->bus == NULL);
  ASSERT(dev->data == NULL);
  ASSERT(dev->driver == NULL);
  ASSERT(LIST_FIRST(&dev->children) == NULL);
  ASSERT(LIST_FIRST(&dev->entries) == NULL);
  kfree(dev);
  return NULL;
}

device_driver_t *alloc_driver(const char *name, void *data, struct device_ops *ops) {
  ASSERT(name != NULL);
  ASSERT(ops != NULL); // data can be NULL
  device_driver_t *driver = kmallocz(sizeof(device_driver_t));
  driver->name = name;
  driver->data = data;
  driver->ops = ops;
  return driver;
}

device_driver_t *free_driver(device_driver_t *driver) {
  ASSERT(driver != NULL);
  ASSERT(driver->data == NULL);
  ASSERT(driver->name == NULL);
  ASSERT(driver->ops == NULL);
  kfree(driver);
  return NULL;
}

//

void probe_all_buses() {
  for (int i = 0; i < ARRAY_SIZE(bus_types); i++) {
    struct bus_type *bus_type = &bus_types[i];
    mtx_lock(&bus_type->lock);
    LIST_FOR_IN(bus, &bus_type->buses, list) {
      if (bus->probe(bus) < 0) {
        EPRINTF("failed to probe bus '%s.%d'\n", bus->name, bus->number);
      }
    }
    mtx_unlock(&bus_type->lock);
  }
}

//
// MARK: Public API
//

device_t *device_get(dev_t dev) {
  mtx_spin_lock(&device_tree_lock);
  device_t *device = rb_tree_find(device_tree, (uint64_t) dev);
  mtx_spin_unlock(&device_tree_lock);
  return device;
}

int dev_major_by_name(const char *name) {
  struct dev_type *type;
  for (int i = 0; i < ARRAY_SIZE(dev_types); i++) {
    type = &dev_types[i];
    if (type->name && strcmp(type->name, name) == 0) {
      return (int) type->major;
    }
  }
  return -1; // not found
}

int register_bus(device_bus_t *bus) {
  if (!is_valid_device_bus(bus)) {
    EPRINTF("invalid bus: missing one or more required field(s)\n");
    return -1;
  }

  struct bus_type *bus_type = hash_map_get(bus_type_by_name, bus->name);
  if (bus_type == NULL) {
    EPRINTF("bus type %s not found\n", bus->name);
    return -1;
  }

  mtx_lock(&bus_type->lock);
  bus->number = bus_type->last_number++;
  LIST_ENTRY_INIT(&bus->list);
  LIST_ADD(&bus_type->buses, bus, list);
  mtx_unlock(&bus_type->lock);

  DPRINTF("registered bus '%s.%d'\n", bus->name, bus->number);
  return 0;
}

int register_driver(const char *bus_type, device_driver_t *driver) {
  if (!is_valid_device_driver(driver)) {
    EPRINTF("invalid driver: missing one or more required field(s)\n");
    return -1;
  }

  struct bus_type *type = hash_map_get(bus_type_by_name, bus_type);
  if (type == NULL) {
    EPRINTF("bus type '%s' not found\n", bus_type);
    return -1;
  }

  LIST_ENTRY_INIT(&driver->list);
  mtx_lock(&type->lock);
  LIST_ADD(&type->drivers, driver, list);
  hash_map_set(type->driver_by_name, driver->name, driver);
  mtx_unlock(&type->lock);

  kprintf("device: registered driver '%s' for bus type '%s'\n", driver->name, bus_type);
  return 0;
}

int register_bus_device(device_bus_t *bus, void *bus_device) {
  if (bus_device == NULL) {
    EPRINTF("bus_device cannot be null\n");
    return -1;
  }

  struct bus_type *type = hash_map_get(bus_type_by_name, bus->name);
  ASSERT(type != NULL);

  device_t *dev = kmallocz(sizeof(device_t));
  dev->bus_device = bus_device;
  dev->bus = bus;

  // look for a driver that can handle this device
  device_driver_t *driver = NULL;
  LIST_FOR_IN(drv, &type->drivers, list) {
    if (drv->check_device(drv, dev)) {
      // found a driver!
      DPRINTF("found driver '%s' for device on bus '%s.%d'\n", drv->name, bus->name, bus->number);
      driver = drv;
      break;
    }
  }

  if (driver == NULL) {
    // no driver found. in the future we should keep a list of devices that
    // don't have a driver and try to find one when a driver is registered
    EPRINTF("no driver found for device on bus '%s.%d'\n", bus->name, bus->number);
    kfree(dev);
    return -1;
  }

  // bind the driver and initialize the device
  dev->driver = driver;
  if (driver->setup_device(dev) < 0) {
    EPRINTF("failed to initialize driver '%s' to device on bus '%s.%d'\n", driver->name, bus->name, bus->number);
    kfree(dev);
    return -1;
  }

  mtx_lock(&bus->devices_lock);
  SLIST_ADD(&bus->devices, dev, bus_list);
  mtx_unlock(&bus->devices_lock);
  return 0;
}

int register_device_ops(const char *dev_type, struct device_ops *ops) {
  struct dev_type *type = hash_map_get(dev_type_by_name, dev_type);
  if (type == NULL) {
    EPRINTF("device type '%s' not found\n", dev_type);
    return -1;
  }

  mtx_lock(&type->lock);
  type->ops = ops;
  mtx_unlock(&type->lock);
  return 0;
}

int register_dev(const char *dev_type, device_t *dev) {
  struct dev_type *type = hash_map_get(dev_type_by_name, dev_type);
  if (type == NULL) {
    EPRINTF("device type '%s' not found\n", dev_type);
    return -1;
  }

  if (dev->ops == NULL)
    dev->ops = type->ops;
  if (dev->ops == NULL)
    dev->ops = &dev_null_ops;

  if (dev->ops == NULL && dev->f_ops == NULL) {
    panic("register_dev: dev->ops and dev->f_ops are both NULL for device type '%s'", dev_type);
  }

  mtx_lock(&type->lock);
  dev->dtype = type->type;
  dev->major = type->major;
  dev->minor = type->last_minor++;
  dev->bus_list = NULL;
  dev->dev_list = NULL;
  SLIST_ADD(&type->devices, dev, dev_list);
  mtx_unlock(&type->lock);

  mtx_spin_lock(&device_tree_lock);
  rb_tree_insert(device_tree, (uint64_t) make_dev(dev), dev);
  mtx_spin_unlock(&device_tree_lock);

  kprintf("device: registered %s device %d\n", dev_type, dev->minor);
  struct device_event event = { .type = DEV_EVT_ADD, .dev = make_dev(dev) };
  if (chan_send(device_events, &event) < 0) {
    EPRINTF("failed to send device event for %s device %d\n", dev_type, dev->minor);
    // we don't fail the registration if the event cannot be sent
  }
  return 0;
}

//
// MARK: Device File API
//

int dev_f_open(file_t *file, int flags) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(file->nopen == 0);
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
  device_t *device = vn->v_dev;
  DPRINTF("dev_f_open: opening file %p with flags 0x%x [device %lu]\n", file, flags, make_dev(device));

  int res;
  if (DEV_FOPS(device, f_open)) {
    // device file open
    res = device->f_ops->f_open(file, flags);
  } else {
    // device open
    res = d_open(device, flags);
  }

  if (res < 0) {
    EPRINTF("failed to open device {:err}\n", res);
  }

  return 0;
}

int dev_f_close(file_t *file) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(file->nopen == 1);
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
  device_t *device = vn->v_dev;
  DPRINTF("dev_f_close: closing file %p [device %lu]\n", file, make_dev(device));

  // device close
  int res = d_close(device);
  if (res < 0) {
    EPRINTF("failed to close file %p [device %lu] {:err}\n", file, make_dev(device), res);
  }

  return res;
}

int dev_f_getpage(file_t *file, off_t off, __move page_t **page) {
  // file does not need to be locked
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
  device_t *device = vn->v_dev;
  DPRINTF("dev_f_getpage: getting page for file %p at offset %lld [device %lu]\n", file, off, make_dev(device));

  // device getpage
  int res = 0;
  page_t *out = d_getpage(device, off);
  if (out == NULL) {
    EPRINTF("failed to get page for file %p at offset %ld [device %lu] {:err}\n", file, off, make_dev(device), res);
    res = -EIO;
  } else {
    *page = moveref(out);
  }

  return res;
}

ssize_t dev_f_read(file_t *file, kio_t *kio) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));
  if (file->flags & O_WRONLY)
    return -EBADF; // file is not open for reading

  vnode_t *vn = file->data;
  device_t *device = vn->v_dev;
  DPRINTF("dev_f_read: reading from file %p at offset %ld [device %lu]\n", file, file->offset, make_dev(device));

  // this operation can block so we unlock the file during the read
  f_unlock(file);
  // device read
  ssize_t res = d_read(device, file->offset, kio);
  // and re-lock the file
  f_lock(file);

  if (device->dtype == D_CHR && res >= 0) {
    // update the file offset for character devices
    file->offset += res;
  }

  if (res < 0) {
    EPRINTF("failed to read from file %p at offset %ld [device %lu] {:err}\n", file, file->offset, make_dev(device), (int)res);
  }
  return res;
}

ssize_t dev_f_write(file_t *file, kio_t *kio) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));
  if (file->flags & O_RDONLY)
    return -EBADF; // file is not open for writing

  vnode_t *vn = file->data;
  device_t *device = vn->v_dev;
  DPRINTF("dev_f_write: writing to file %p at offset %ld [device %lu]\n", file, file->offset, make_dev(device));

  // this operation can block so we unlock the file during the write
  f_unlock(file);
  // device write
  ssize_t res = d_write(device, file->offset, kio);
  // and re-lock the file
  f_lock(file);

  if (device->dtype == D_CHR && res >= 0) {
    // update the file offset for character devices
    file->offset += res;
  }

  if (res < 0) {
    EPRINTF("failed to write to file %p at offset %ld [device %lu] {:err}\n", file, file->offset, make_dev(device), (int)res);
  }
  return res;
}

int dev_f_stat(file_t *file, struct stat *statbuf) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
  device_t *device = vn->v_dev;
  DPRINTF("dev_f_stat: getting stat for file %p [device %lu]\n", file, make_dev(device));

  // grab the vnode lock because we need to call vn_stat to populate certain
  // fields that the device cannot fill even if it implements d_stat
  if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  vn_stat(vn, statbuf);
  if (D_OPS(device)->d_stat) {
    // devices can implement d_stat to provide additional information
    D_OPS(device)->d_stat(device, statbuf);
  }

  vn_unlock(vn);
  return 0;
}

int dev_f_ioctl(file_t *file, unsigned int request, void *arg) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
  device_t *device = vn->v_dev;
  DPRINTF("dev_f_ioctl: ioctl on file %p with request %#llx [device %lu]\n", file, request, make_dev(device));

  // device ioctl
  int res = d_ioctl(device, request, arg);
  if (res == -ENOTSUP)
    res = -ENOTTY; // not a tty device or not supported

  return res;
}

int dev_f_kqevent(file_t *file, knote_t *kn) {
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));
  ASSERT(kn->event.filter == EVFILT_READ);

  // called from the file `event` filter_ops method
  vnode_t *vn = file->data;
  device_t *device = vn->v_dev;
  DPRINTF("dev_f_kqevent: checking kqevent for file %p [device %lu, filter %s]\n",
          file, make_dev(device), evfilt_to_string(kn->event.filter));

  int res;
  if (D_OPS(device)->d_kqevent) {
    res = D_OPS(device)->d_kqevent(device, kn);
  } else {
    DPRINTF("dev_f_kqevent: file %p does not support kqevent [device %lu]\n", file, make_dev(device));
    res = 0;
  }

  if (res < 0) {
    EPRINTF("failed to get kqevent for file %p [device %lu] {:err}\n", file, make_dev(device), res);
  } else if (res == 0) {
    kn->event.data = 0;
    DPRINTF("dev_f_kqevent: no data available for file %p [device %lu]\n", file, make_dev(device));
  } else {
    DPRINTF("dev_f_kqevent: %lld bytes available for file %p [device %lu]\n", kn->event.data, file, make_dev(device));
  }
  return res;
}

void dev_f_cleanup(file_t *file) {
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));
  if (mtx_owner(&file->lock) != NULL) {
    ASSERT(mtx_owner(&file->lock) == curthread);
  }

  vnode_t *vn = moveptr(file->data);
  vn_putref(&vn);
}

// referenced in vfs/file.c
struct file_ops dev_file_ops = {
  .f_open = dev_f_open,
  .f_close = dev_f_close,
  .f_allocate = NULL,
  .f_getpage = dev_f_getpage,
  .f_read = dev_f_read,
  .f_write = dev_f_write,
  .f_readdir = NULL,
  .f_stat = dev_f_stat,
  .f_ioctl = dev_f_ioctl,
  .f_kqevent = dev_f_kqevent,
  .f_cleanup = dev_f_cleanup,
};

struct file_ops *device_get_file_ops(device_t *device) {
  if (device->f_ops == NULL) {
    return &dev_file_ops;
  } else {
    return device->f_ops;
  }
}
