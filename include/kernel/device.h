//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#ifndef KERNEL_DEVICE_H
#define KERNEL_DEVICE_H

#include <kernel/mm_types.h>
#include <kernel/queue.h>
#include <kernel/spinlock.h>
#include <kernel/mutex.h>
#include <kernel/kio.h>

struct device;
struct device_driver;
struct device_bus;

struct ventry;

#define D_OPS(d) __type_checked(struct device *, d, (d)->ops)

#define DEVICE_DATA(d) __type_checked(struct device *, d, (d)->data)
#define DEVFILE_DATA(f) __type_checked(struct file *, f, (f)->inode->i_device->data)

enum dtype {
  D_BLK = 1,
  D_CHR,
};

/**
 * A system device.
 *
 * This is the structure used for all devices in the system. If the device is
 * registered, then the major and minor numbers will be set and it will have
 * the ops pointer set. If the device is anonymous, then it has no major or
 * minor number, but the bus and bus_device pointers will be set. Anonymous
 * devices are not accessible.
 */
typedef struct device {
  enum dtype dtype;
  uint8_t major;
  uint8_t minor;
  uint8_t unit;

  void *bus_device;  // device struct for the specific bus
  void *data;        // private data for the device (for the driver)

  struct device_bus *bus;
  struct device_driver *driver;
  struct device_ops *ops;

  LIST_HEAD(struct device) children;
  LIST_HEAD(struct ventry) entries;

  SLIST_ENTRY(struct device) dev_list;
  SLIST_ENTRY(struct device) bus_list;
} device_t;

struct device_ops {
  int (*d_open)(struct device *device, int flags);
  int (*d_close)(struct device *device);
  ssize_t (*d_read)(struct device *device, size_t off, size_t nmax, struct kio *kio);
  ssize_t (*d_write)(struct device *device, size_t off, size_t nmax, struct kio *kio);
  // int (*d_ioctl)(struct device *device, int cmd, void *arg);
  page_t *(*d_getpage)(struct device *device, size_t off);
  int (*d_putpage)(struct device *device, size_t off, page_t *page);
};

/**
 * A device driver.
 *
 * Device drivers provide an interface between the kernel and devices connected
 * to the system. They access the device through the bus_device object using the
 * native type for the bus.
 */
typedef struct device_driver {
  const char *name;             // the driver identifier
  void *data;                   // private data
  struct device_ops *ops;       // device interface

  /**
   * Checks whether the driver supports a device.
   *
   * This function is called when a new device is registered. The provided
   * device's 'bus_data' field will be set prior to the function being called
   * and it will contain the native device type used for the bus. For example,
   * if the driver is a PCI driver then the bus data will be a pci_device_t
   * structure.
   * @return 0 if the device is supported, -1 otherwise
   */
  int (*check_device)(struct device_driver *drv, struct device *dev);

  LIST_ENTRY(struct device_driver) list;
} device_driver_t;

/// A device bus (e.g. PCI, USB, etc.)
typedef struct device_bus {
  const char *name;

  int number;
  void *data; // private data

  LIST_HEAD(struct device) devices;
  spinlock_t devices_lock;

  /**
   * Probe the bus for devices.
   *
   * This function is called when the bus is registered. It should probe the
   * bus for devices and register them.
   *
   * @param bus The bus to probe.
   * @return 0 on success, -1 on failure.
   */
  int (*probe)(struct device_bus *bus);

  LIST_ENTRY(struct device_bus) list;
} device_bus_t;


static inline dev_t makedev(uint8_t major, uint8_t minor) {
  return ((dev_t)major) | ((dev_t)minor << 8);
}
static inline dev_t make_dev(device_t *dev) {
  return ((dev_t)dev->major) | ((dev_t)dev->minor << 8) | ((dev_t)dev->unit << 16);
}
static inline uint8_t dev_major(dev_t dev) {
  return dev & 0xFF;
}
static inline uint16_t dev_minor(dev_t dev) {
  return (dev >> 8) & 0xFF;
}
static inline uint16_t dev_unit(dev_t dev) {
  return (dev >> 16) & 0xFF;
}

device_t *alloc_device(void *data, struct device_ops *ops);
device_t *free_device(device_t *dev);

void probe_all_buses();

//
// MARK: Public API
//

device_t *device_get(dev_t dev);

/**
 * Registers a new device bus on the system.
 *
 * On success the bus will be assigned a number and added to the list of registered
 * buses.
 *
 * \note Only name, data, and the function pointers should be set and the
 *       name should refer to an existing bus type.
 */
int register_bus(device_bus_t *bus);

/**
 * Registers a new device driver on the system.
 *
 * This function associates a driver with a bus type. The driver is bus-specific
 * and should access bus_device pointers as the native device type for the bus.
 *
 * \note Only name, data, device_ops, and the function pointers should be set.
 *
 */
int register_driver(const char *bus_type, device_driver_t *driver);

/**
 * Registers a new anonymous device on a bus.
 *
 * This function registers devices that do not yet have a driver. This is normally
 * called by the bus drivers when a device is found. The kernel will try to match
 * the device with one or more registered drivers. The bus and bus_device pointers
 * must be non-NULL.
 *
 * On failure, references to bus_device will be released.
 */
int register_bus_device(device_bus_t *bus, void *bus_device);

/**
 * Registers device_ops for a device type.
 *
 * This function is for single-driver device types that do not have a bus. It
 * associates the device operations with the specific type. When registering
 * devices of this type, the devices should be allocated with NULL ops.
 */
int register_device_ops(const char *dev_type, struct device_ops *ops);

/**
 * Registers a new device on the system.
 *
 * This function registers devices that are bound to a driver. The device may or
 * may not be associated with a bus, but the driver and device_ops pointers must
 * both be non-NULL.
 *
 * On success the device type, major, and minor number will be valid.
 * On failure, the device will remain anonymous.
 */
int register_dev(const char *dev_type, device_t *dev);

// MARK: Device Operations

int d_open(device_t *device, int flags);
int d_close(device_t *device);
ssize_t d_nread(device_t *device, size_t off, size_t nmax, kio_t *kio);
ssize_t d_nwrite(device_t *device, size_t off, size_t nmax, kio_t *kio);

static inline ssize_t d_read(device_t *device, size_t off, kio_t *kio) {
  return d_nread(device, off, kio_remaining(kio), kio);
}

static inline ssize_t d_write(device_t *device, size_t off, kio_t *kio) {
  return d_nwrite(device, off, kio_remaining(kio), kio);
}

static inline ssize_t __d_read(device_t *device, size_t off, void *buf, size_t len) {
  kio_t tmp = kio_new_writeonly(buf, len);
  return d_read(device, off, &tmp);
}

static inline ssize_t __d_write(device_t *device, size_t off, const void *buf, size_t len) {
  kio_t tmp = kio_new_readonly(buf, len);
  return d_write(device, off, &tmp);
}

#endif
