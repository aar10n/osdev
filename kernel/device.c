
//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#include <kernel/device.h>
#include <kernel/chan.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#include <rb_tree.h>

#define HMAP_TYPE void *
#include <hash_map.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("device: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("device: %s: " fmt, __func__, ##__VA_ARGS__)

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
};
#undef DECLARE_DEV_TYPE

chan_t *device_events;
static rb_tree_t *device_tree;
static mtx_t device_tree_lock;
static hash_map_t *bus_type_by_name;
static hash_map_t *dev_type_by_name;

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

device_t *alloc_device(void *data, struct device_ops *ops) {
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

  if (dev->ops == NULL) {
    panic("register_dev: dev->ops is NULL.");
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
