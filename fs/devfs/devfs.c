//
// Created by Aaron Gill-Braun on 2025-06-12.
//

#include "devfs.h"

#include <kernel/fs.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#include <fs/ramfs/ramfs.h>

#include <rb_tree.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("devfs: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("devfs: %s: " fmt, __func__, ##__VA_ARGS__)


LIST_HEAD(devfs_class_t) dev_classes;
rb_tree_t *dev_full_lookup;
rb_tree_t *dev_major_lookup;


str_t devfs_name_for_dev(cstr_t path, dev_t dev) {
  uint8_t major = dev_major(dev);
  uint8_t minor = dev_minor(dev);
  uint8_t unit = dev_unit(dev);

  devfs_class_t *class = NULL;
  bool is_fullname = true;
  // first check if an exact <major><minor> match exists
  class = rb_tree_find(dev_full_lookup, makedev(major, minor));
  if (class == NULL) {
    // now check if a <major> match exists
    class = rb_tree_find(dev_major_lookup, major);
    is_fullname = false;
  }
  if (class == NULL) {
    EPRINTF("no class found for dev %d\n", dev);
    return str_null;
  }

  char buf[NAME_MAX];
  size_t n;
  if (!cstr_isnull(path)) {
    n = ksnprintf(buf, NAME_MAX, "{:cstr}/", &path);
  } else {
    n = 0;
  }

  if (is_fullname) {
    n += ksnprintf(buf+n, NAME_MAX-n, "%s", class->prefix);
  } else if (class->attr == DEVFS_NUMBERED) {
    n += ksnprintf(buf+n, NAME_MAX-n, "%s%d", class->prefix, minor);
  } else if (class->attr == DEVFS_LETTERED) {
    char letters[8];
    size_t i = 0;
    uint8_t m = minor;

    do {
      letters[i++] = (char)('a' + (m % 26));
      m = m / 26 - 1;
    } while (m != (uint8_t)-1 && i < sizeof(letters) - 1);
    letters[i] = '\0';

    reverse(letters);
    n += ksnprintf(buf+n, NAME_MAX-n, "%s%s", class->prefix, letters);
  }
  if (n == NAME_MAX) {
    EPRINTF("name too long for dev %d\n", dev);
    return str_null;
  }

  // add the unit number if it is not zero
  if (unit > 0) {
    n += ksnprintf(buf+n, NAME_MAX-n, "s%d", unit);
    if (n == NAME_MAX) {
      EPRINTF("name too long for dev %d\n", dev);
      return str_null;
    }
  }
  return str_new(buf, n);
}


int devfs_register_class(int major, int minor, const char *prefix, int attr) {
  if (major <= 0 || major > UINT8_MAX || prefix == NULL ||
      minor > UINT8_MAX || prefix[0] == '\0') {
    EPRINTF("invalid parameters\n");
    return -EINVAL;
  }

  if (minor >= 0) {
    DPRINTF("registering device class: major=%d, minor=%d, prefix='%s', attr=%d\n",
            major, minor, prefix, attr);
  } else {
    DPRINTF("registering device class: major=%d, prefix='%s', attr=%d\n",
            major, prefix, attr);
  }

  if (dev_full_lookup == NULL) {
    // allocate the lookup trees if they are not already allocated
    ASSERT(dev_major_lookup == NULL);
    dev_full_lookup = create_rb_tree();
    dev_major_lookup = create_rb_tree();
  }

  devfs_class_t *class = kmallocz(sizeof(devfs_class_t));
  class->major = major;
  class->minor = minor;
  class->prefix = prefix;
  class->attr = attr;

  uint64_t key;
  rb_tree_t *tree;
  if (minor >= 0) {
    key = makedev(class->major, class->minor);
    tree = dev_full_lookup;
  } else {
    key = major;
    tree = dev_major_lookup;
  }

  rb_tree_insert(tree, key, class);
  LIST_ADD_FRONT(&dev_classes, class, list);
  return 0;
}

int devfs_synchronize_main(devfs_mount_t *mount) {
  cstr_t path = cstr_from_str(mount->path);
  DPRINTF("starting devfs process for '{:cstr}'\n", &path);

  struct device_event event;
  while (chan_recv(device_events, &event) == 0) {
    device_t *dev = device_get(event.dev);
    if (dev == NULL) {
      EPRINTF("device not found for dev %d\n", event.dev);
      continue;
    }

    int res;
    str_t dev_path = devfs_name_for_dev(path, event.dev);
    if (str_isnull(dev_path)) {
      continue;
    }

    if (event.type == DEV_EVT_ADD) {
      int flags = 0;
      switch (dev->dtype) {
        case D_BLK: flags |= S_IFBLK; break;
        case D_CHR: flags |= S_IFCHR; break;
        default: unreachable;
      }

      res = fs_mknod(cstr_from_str(dev_path), flags, event.dev);
      if (res < 0) {
        EPRINTF("failed to create device node {:str} for dev %d: {:err}\n", &dev_path, event.dev, res);
      } else {
        DPRINTF("created device node {:str} for dev %d\n", &dev_path, event.dev);
      }
    } else if (event.type == DEV_EVT_REMOVE) {
      res = fs_unlink(cstr_from_str(dev_path));
      if (res < 0) {
        EPRINTF("failed to remove device node {:str} for dev %d: {:err}\n", &dev_path, event.dev, res);
      } else {
        DPRINTF("removed device node {:str} for dev %d\n", &dev_path, event.dev);
      }
    }

    str_free(&dev_path);
  }

  DPRINTF("exiting devfs process for '{:cstr}'\n", &mount->path);
  return 0;
}

struct vfs_ops devfs_vfs_ops = {
  .v_mount = devfs_vfs_mount,
  .v_unmount = devfs_vfs_unmount,
  .v_stat = ramfs_vfs_stat,
  .v_cleanup = ramfs_vfs_cleanup,
};

static fs_type_t devfs_type = {
  .name = "devfs",
  .vfs_ops = &devfs_vfs_ops,
};


static void devfs_static_init() {
  // a devfs filesystem is a ramfs filesystem that is automatically sycned with the device tree.
  devfs_type.vn_ops = &ramfs_vnode_ops;
  devfs_type.ve_ops = &ramfs_ventry_ops;

  if (fs_register_type(&devfs_type) < 0) {
    panic("failed to register devfs type\n");
  }
}
STATIC_INIT(devfs_static_init);
