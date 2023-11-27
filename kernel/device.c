
//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#include <kernel/device.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#include <kernel/string.h>
#include <rb_tree.h>

#define DECLARE_DEV_TYPE(_name, _maj, _typ) [_maj] = { .name = (_name), .major = (_maj), .type = (_typ) }

#define HMAP_TYPE void *
#include <hash_map.h>

struct bus_type {
  const char *name;

  int last_number;
  LIST_HEAD(struct device_bus) buses;
  LIST_HEAD(struct device_driver) drivers;
  hash_map_t *driver_by_name;
  spinlock_t lock;
};

struct dev_type {
  const char *name;
  uint8_t major;
  enum dtype type;
  struct device_ops *ops; // single-driver device types

  uint8_t last_minor;
  spinlock_t lock;
  LIST_HEAD(struct device) devices;
};

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("device: %s: " fmt, __func__, ##__VA_ARGS__)

struct bus_type bus_types[] = {
  { "pci" },
  { "usb" },
};

struct dev_type dev_types[] = {
  { /* reserved */ },
  DECLARE_DEV_TYPE("ramdisk", 1, D_BLK),
  DECLARE_DEV_TYPE("serial" , 2, D_CHR),
  DECLARE_DEV_TYPE("memory" , 3, D_CHR),
};

static rb_tree_t *device_tree;
static spinlock_t device_tree_lock;
static hash_map_t *bus_type_by_name;
static hash_map_t *dev_type_by_name;

static void device_static_init() {
  device_tree = create_rb_tree();
  spin_init(&device_tree_lock);

  bus_type_by_name = hash_map_new();
  for (int i = 0; i < ARRAY_SIZE(bus_types); i++) {
    bus_types[i].driver_by_name = hash_map_new();
    spin_init(&bus_types[i].lock);
    hash_map_set(bus_type_by_name, bus_types[i].name, (void *) &bus_types[i]);
  }

  dev_type_by_name = hash_map_new();
  for (int i = 1; i < ARRAY_SIZE(dev_types); i++) {
    hash_map_set(dev_type_by_name, dev_types[i].name, (void *) &dev_types[i]);
  }
}
STATIC_INIT(device_static_init);

//
// MARK: Device API
//

device_t *alloc_device(void *data, struct device_ops *ops) {
  ASSERT(ops != NULL); // data can be NULL
  device_t *dev = kmallocz(sizeof(device_t));
  dev->data = data;
  dev->ops = ops;
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

//

void probe_all_buses() {
  for (int i = 0; i < ARRAY_SIZE(bus_types); i++) {
    struct bus_type *bus_type = &bus_types[i];
    SPIN_LOCK(&bus_type->lock);
    LIST_FOR_IN(bus, &bus_type->buses, list) {
      if (bus->probe(bus) < 0) {
        DPRINTF("failed to probe bus '%s%d'\n", bus->name, bus->number);
      }
    }
    SPIN_UNLOCK(&bus_type->lock);
  }
}

//
// MARK: Public API
//

device_t *device_get(dev_t dev) {
  SPIN_LOCK(&device_tree_lock);
  rb_node_t *node = rb_tree_find(device_tree, (uint64_t) dev);
  device_t *device = node ? node->data : NULL;
  SPIN_UNLOCK(&device_tree_lock);
  return device;
}

int register_bus(device_bus_t *bus) {
  if (bus->name == NULL || bus->probe == NULL) {
    DPRINTF("invalid bus: missing one or more required field(s)\n");
    return -1;
  }

  struct bus_type *bus_type = hash_map_get(bus_type_by_name, bus->name);
  if (bus_type == NULL) {
    DPRINTF("bus type %s not found\n", bus->name);
    return -1;
  }

  SPIN_LOCK(&bus_type->lock);
  bus->number = bus_type->last_number++;
  LIST_ENTRY_INIT(&bus->list);

  LIST_ADD(&bus_type->buses, bus, list);
  SPIN_UNLOCK(&bus_type->lock);

  kprintf("device: registered bus '%s%d'\n", bus->name, bus->number);
  return 0;
}

int register_driver(const char *bus_type, device_driver_t *driver) {
  if (driver->name == NULL || driver->check_device == NULL) {
    DPRINTF("invalid driver: missing one or more required field(s)\n");
    return -1;
  }

  struct bus_type *type = hash_map_get(bus_type_by_name, bus_type);
  if (type == NULL) {
    DPRINTF("bus type '%s' not found\n", bus_type);
    return -1;
  }

  LIST_ENTRY_INIT(&driver->list);
  SPIN_LOCK(&type->lock);
  LIST_ADD(&type->drivers, driver, list);
  hash_map_set(type->driver_by_name, driver->name, driver);
  SPIN_UNLOCK(&type->lock);

  kprintf("device: registered driver '%s' for bus type '%s'\n", driver->name, bus_type);
  return 0;
}

int register_bus_device(device_bus_t *bus, void *bus_device) {
  if (bus_device == NULL) {
    DPRINTF("bus_device cannot be null\n");
    return -1;
  }

  struct bus_type *type = hash_map_get(bus_type_by_name, bus->name);
  ASSERT(type != NULL);

  device_t *dev = kmalloc(sizeof(device_t));
  memset(dev, 0, sizeof(device_t));
  dev->bus_device = bus_device;
  dev->bus = bus;
  LIST_INIT(&dev->children);
  LIST_INIT(&dev->entries);

  // look for a driver that can handle this device
  device_driver_t *driver = NULL;
  LIST_FOR_IN(drv, &type->drivers, list) {
    if (drv->check_device(drv, dev) == 0) {
      // found a driver!
      driver = drv;
      break;
    }
  }

  if (driver == NULL) {
    // no driver found. in the future we should keep a list of devices that
    // don't have a driver and try to find one when a driver is registered
    DPRINTF("no driver found for device on bus '%s%d'\n", bus->name, bus->number);
    kfree(dev);
    return -1;
  }

  SPIN_LOCK(&bus->devices_lock);
  SLIST_ADD(&bus->devices, dev, bus_list);
  SPIN_UNLOCK(&bus->devices_lock);
  return 0;
}

int register_device_ops(const char *dev_type, struct device_ops *ops) {
  struct dev_type *type = hash_map_get(dev_type_by_name, dev_type);
  if (type == NULL) {
    DPRINTF("device type '%s' not found\n", dev_type);
    return -1;
  }

  SPIN_LOCK(&type->lock);
  type->ops = ops;
  SPIN_UNLOCK(&type->lock);
  return 0;
}

int register_dev(const char *dev_type, device_t *dev) {
  struct dev_type *type = hash_map_get(dev_type_by_name, dev_type);
  if (type == NULL) {
    DPRINTF("device type '%s' not found\n", dev_type);
    return -1;
  }

  if (dev->ops == NULL)
    dev->ops = type->ops;

  // if (dev->data == NULL) {
  //   panic("register_dev: dev->data is NULL. did you forget to set it?");
  // } else
  if (dev->ops == NULL) {
    panic("register_dev: dev->ops is NULL.");
  }

  SPIN_LOCK(&type->lock);
  dev->dtype = type->type;
  dev->major = type->major;
  dev->minor = type->last_minor++;
  dev->bus_list = NULL;
  dev->dev_list = NULL;
  SLIST_ADD(&type->devices, dev, dev_list);
  SPIN_UNLOCK(&type->lock);

  SPIN_LOCK(&device_tree_lock);
  rb_tree_insert(device_tree, (uint64_t) make_dev(dev), dev);
  SPIN_UNLOCK(&device_tree_lock);

  kprintf("device: registered %s device %d\n", dev_type, dev->minor);
  return 0;
}

// MARK: Device Operations

int d_open(device_t *device, int flags) {
  if (device->ops->d_open == NULL)
    return 0;
  return device->ops->d_open(device, flags);
}

int d_close(device_t *device) {
  if (device->ops->d_close == NULL)
    return 0;
  return device->ops->d_close(device);
}

ssize_t d_nread(device_t *device, size_t off, size_t nmax, kio_t *kio) {
  if (device->ops->d_read == NULL)
    return -ENOTSUP;
  return device->ops->d_read(device, off, nmax, kio);
}

ssize_t d_nwrite(device_t *device, size_t off, size_t nmax, kio_t *kio) {
  if (device->ops->d_write == NULL)
    return -ENOTSUP;
  return device->ops->d_write(device, off, nmax, kio);
}
