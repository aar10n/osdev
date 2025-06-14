//
// Created by Aaron Gill-Braun on 2021-04-03.
//

#include <kernel/usb/usb.h>

#include <kernel/mm.h>
#include <kernel/proc.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/string.h>

#include <rb_tree.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("usb: " x, ##__VA_ARGS__)
#define EPRINTF(x, ...) kprintf("usb: %s: " x, __func__, ##__VA_ARGS__)

// #define USB_DEBUG
#ifdef USB_DEBUG
#define usb_trace_debug(str, args...) kprintf("[usb] " str "\n", ##args)
#else
#define usb_trace_debug(str, args...)
#endif

#define desc_type(d) (((usb_descriptor_t *)((void *)(d)))->type)
#define desc_length(d) (((usb_descriptor_t *)((void *)(d)))->length)

#define EVT_RING_SIZE 256

static rb_tree_t *tree = NULL;
static id_t device_id = 0;
static cond_t init;

static chan_t *pending_usb_devices;

static LIST_HEAD(usb_driver_t) drivers;
static int num_drivers = 0;


static inline usb_endpoint_t *find_endpoint_for_xfer(usb_device_t *device, usb_xfer_type_t type) {
  if (type == USB_SETUP_XFER) {
    // default control endpoint
    usb_endpoint_t *ep = LIST_FIND(e, &device->endpoints, list, e->number == 0);
    ASSERT(ep != NULL);
    return ep;
  }

  usb_dir_t dir = type == USB_DATA_IN_XFER ? USB_IN : USB_OUT;
  return LIST_FIND(e, &device->endpoints, list, e->number != 0 && e->dir == dir);
}

static inline usb_ep_type_t usb_get_endpoint_type(usb_ep_descriptor_t *ep_desc) {
  uint8_t ep_type = ep_desc->attributes & 0x3;
  switch (ep_type) {
    case 0b00: return USB_CONTROL_EP;
    case 0b01: return USB_ISOCHRONOUS_EP;
    case 0b10: return USB_BULK_EP;
    case 0b11: return USB_INTERRUPT_EP;
    default: break;
  }
  unreachable;
}

static inline void *usb_seek_descriptor(uint8_t type, void *buffer, void *buffer_end) {
  void *ptr = buffer;
  void *eptr = buffer_end;
  while (ptr < eptr) {
    if (cast_usb_desc(ptr)->type == type) {
      return ptr;
    } else if (cast_usb_desc(ptr)->length == 0) {
      return NULL;
    }
    ptr = offset_ptr(ptr, cast_usb_desc(ptr)->length);
  }
  return NULL;
}

static inline usb_driver_t *locate_device_driver(uint8_t dev_class, uint8_t dev_subclass) {
  usb_driver_t *drv = LIST_FIND(d, &drivers, next, d->dev_class == dev_class);
  if (drv == NULL) {
    return NULL;
  }

  if (drv->dev_subclass == 0 || drv->dev_subclass == dev_subclass) {
    // driver matches any subclass or the subclasses match
    return drv;
  }
  // driver does not match the subclass
  return NULL;
}

//

const char *usb_get_event_type_string(usb_event_type_t type) {
  switch (type) {
    case USB_CTRL_EV: return "USB_CTRL_EV";
    case USB_IN_EV: return "USB_IN_EV";
    case USB_OUT_EV: return "USB_OUT_EV";
    default: return "UNKNOWN";
  }
}

const char *usb_get_status_string(usb_status_t status) {
  switch (status) {
    case USB_SUCCESS: return "USB_SUCCESS";
    case USB_ERROR: return "USB_ERROR";
    default: return "UNKNOWN";
  }
}

//

noreturn void *usb_device_connect_event_loop() {
  DPRINTF("starting device connect event loop\n");

  usb_device_t *device;
  while (chan_recv(pending_usb_devices, &device) == 0) {
    usb_host_t *host = device->host;
    DPRINTF("handling device connection\n");

    if (usb_device_init(device) < 0) {
      EPRINTF("failed to initialize device\n");
      continue;
    }
  }

  panic("usb: device channel was closed\n");
}

//

static void usb_module_init() {
  pending_usb_devices = chan_alloc(64, sizeof(usb_device_t *), 0, "pending_usb_devices");
  chan_set_free_cb(pending_usb_devices, kfree);

  // start a new process to handle device connection events
  thread_t *maintd = thread_alloc(TDF_KTHREAD, SIZE_16KB);
  thread_setup_name(maintd, cstr_make("usb_device_connect_event_loop"));
  thread_setup_entry(maintd, (uintptr_t) usb_device_connect_event_loop, 0);
  __ref proc_t *driver_proc = proc_alloc_new(getref(curproc->creds));
  proc_setup_add_thread(driver_proc, maintd);
  proc_setup_name(driver_proc, cstr_make("usb_core"));
  proc_finish_setup_and_submit_all(moveref(driver_proc));
}
MODULE_INIT(usb_module_init);

//
// MARK: USB Driver API
//

int usb_register_driver(usb_driver_t *driver) {
  DPRINTF("registering usb driver '%s'\n", driver->name);

  usb_driver_t *existing = locate_device_driver(driver->dev_class, driver->dev_subclass);
  if (existing != NULL) {
    EPRINTF("duplicate driver registered \"%s\" [existing = \"%s\"]\n",
            driver->name, existing->name);
    return -1;
  }

  LIST_ADD_FRONT(&drivers, driver, next);
  num_drivers++;
  return 0;
}

//
// MARK: Host Driver API
//

int usb_register_host(usb_host_t *host) {
  DPRINTF("registering host controller '%s'\n", host->name);

  usb_hub_t *root = kmalloc(sizeof(usb_hub_t));
  root->port = 0;
  root->tier = 0;
  root->data = NULL;
  root->self = NULL;
  root->host = host;
  root->num_devices = 0;
  LIST_INIT(&root->devices);
  host->root = root;

  DPRINTF("initializing host controller '%s'\n", host->name);
  if (host->host_impl->init(host) < 0) {
    EPRINTF("failed to initialize host controller\n");
    return -1;
  }

  DPRINTF("starting host controller '%s'\n", host->name);
  if (host->host_impl->start(host) < 0) {
    EPRINTF("failed to start host controller\n");
    return -1;
  }

  DPRINTF("discovering devices on '%s'\n", host->name);
  if (host->host_impl->discover(host) < 0) {
    EPRINTF("failed to start host controller '%s'\n", host->name);
    return -1;
  }
  return 0;
}

int usb_handle_device_connect(usb_host_t *host, void *data) {
  DPRINTF(">> usb device connected <<\n");

  usb_device_t *device = kmalloc(sizeof(usb_device_t));
  memset(device, 0, sizeof(usb_device_t));
  device->host = host;
  device->host_data = data;
  LIST_ENTRY_INIT(&device->list);

  return chan_send(pending_usb_devices, &device);
}

int usb_handle_device_disconnect(usb_host_t *host, usb_device_t *device) {
  DPRINTF(">> usb device disconnected <<\n");
  return 0;
}

//
// MARK: Common API
//

int usb_run_ctrl_transfer(usb_device_t *device, usb_setup_packet_t setup, uintptr_t buffer, size_t length) {
  usb_transfer_t xfer = {
    .type = USB_SETUP_XFER,
    .flags = USB_XFER_SETUP,
    .buffer = buffer,
    .length = length,
    .setup = setup,
  };

  usb_host_t *host = device->host;
  usb_endpoint_t *endpoint = LIST_FIRST(&device->endpoints);
  ASSERT(endpoint != NULL);
  ASSERT(endpoint->number == 0);
  if (host->device_impl->add_transfer(device, endpoint, &xfer) < 0) {
    EPRINTF("usb_execute_ctrl_transfer(): failed to add transfer\n");
    return -1;
  }

  // kick off transfer
  if (host->device_impl->start_transfer(device, endpoint) < 0) {
    EPRINTF("usb_execute_ctrl_transfer(): failed to start transfer\n");
    return -1;
  }

  usb_event_t event;
  if (host->device_impl->await_event(device, endpoint, &event) < 0) {
    EPRINTF("usb_await_transfer(): await_event failed\n");
    return -1;
  }

  if (event.status == USB_ERROR) {
    EPRINTF("usb_await_setup_transfer(): transfer completed with error\n");
    return -1;
  }
  return 0;
}

//

int usb_add_transfer(usb_device_t *device, usb_dir_t direction, uintptr_t buffer, size_t length) {
  usb_transfer_t xfer = {
    .type = direction == USB_IN ? USB_DATA_IN_XFER : USB_DATA_OUT_XFER,
    .flags = 0,
    .buffer = buffer,
    .length = length,
    .raw = 0,
  };

  usb_host_t *host = device->host;
  usb_endpoint_t *endpoint = find_endpoint_for_xfer(device, xfer.type);
  if (endpoint == NULL) {
    EPRINTF("no endpoint found for transfer\n");
    return -1;
  }

  if (host->device_impl->add_transfer(device, endpoint, &xfer) < 0) {
    EPRINTF("failed to add transfer\n");
    return -1;
  }
  return 0;
}

int usb_start_transfer(usb_device_t *device, usb_dir_t direction) {
  usb_host_t *host = device->host;
  usb_xfer_type_t type = direction == USB_IN ? USB_DATA_IN_XFER : USB_DATA_OUT_XFER;
  usb_endpoint_t *endpoint = find_endpoint_for_xfer(device, type);
  if (endpoint == NULL) {
    EPRINTF("invalid direction\n");
    return -1;
  }

  if (host->device_impl->start_transfer(device, endpoint) < 0) {
    EPRINTF("failed to start transfer\n");
    return -1;
  }
  return 0;
}

int usb_await_transfer(usb_device_t *device, usb_dir_t direction) {
  usb_host_t *host = device->host;
  usb_xfer_type_t type = direction == USB_IN ? USB_DATA_IN_XFER : USB_DATA_OUT_XFER;
  usb_endpoint_t *endpoint = find_endpoint_for_xfer(device, type);
  if (endpoint == NULL) {
    EPRINTF("invalid direction\n");
    return -1;
  }

  usb_event_t event;
  if (chan_recv(endpoint->event_ch, &event) < 0) {
    EPRINTF("failed to await transfer\n");
    return -1;
  }

  if (host->device_impl->await_event(device, endpoint, &event) < 0) {
    EPRINTF("await_event failed\n");
    return -1;
  }

  if (event.status == USB_ERROR) {
    EPRINTF("transfer completed with error\n");
    return -1;
  }
  return 0;
}

int usb_start_await_transfer(usb_device_t *device, usb_dir_t direction) {
  usb_host_t *host = device->host;
  usb_xfer_type_t type = direction == USB_IN ? USB_DATA_IN_XFER : USB_DATA_OUT_XFER;
  usb_endpoint_t *endpoint = find_endpoint_for_xfer(device, type);
  if (endpoint == NULL) {
    EPRINTF("invalid direction\n");
    return -1;
  }

  // start the transfer
  if (host->device_impl->start_transfer(device, endpoint) < 0) {
    EPRINTF("failed to start transfer\n");
    return -1;
  }

  // wait for it
  usb_event_t event;
  if (chan_recv(endpoint->event_ch, &event) < 0) {
    EPRINTF("failed to await transfer\n");
    return -1;
  }

  if (event.status == USB_ERROR) {
    EPRINTF("transfer completed with error\n");
    return -1;
  }
  return 0;
}

//
// MARK: Internal API
//

int usb_device_init(usb_device_t *device) {
  usb_host_t *host = device->host;
  DPRINTF("initializing device\n");
  if (host->device_impl->init(device) < 0) {
    EPRINTF("failed to initialize device\n");
    return -1;
  }

  // read device descriptor
  usb_device_descriptor_t *desc = NULL;
  if (host->device_impl->read_device_descriptor(device, &desc) < 0) {
    EPRINTF("failed to read device descriptor\n");
    return -1;
  }
  device->desc = desc;

  // initialize special "dummy" struct for the default control endpoint
  usb_endpoint_t *ep_zero = kmalloc(sizeof(usb_endpoint_t));
  memset(ep_zero, 0, sizeof(usb_endpoint_t));

  ep_zero->type = USB_CONTROL_EP;
  ep_zero->dir = USB_IN; // incorrect but ignored
  ep_zero->number = 0;
  ep_zero->attributes = 0;
  ep_zero->max_pckt_sz = desc->max_packt_sz0;
  ep_zero->interval = 0;
  ep_zero->device = device;
  ep_zero->event_ch = chan_alloc(32, sizeof(usb_event_t), 0, "ep0_usb_event_ch");

  ASSERT(host->device_impl->init_endpoint(ep_zero) >= 0);
  LIST_ADD(&device->endpoints, ep_zero, list);

  // read device and product info
  // kprintf("USB Device Descriptor\n");
  // usb_print_device_descriptor(desc);

  // product name
  device->product = usb_device_read_string(device, desc->product_idx);
  // manufacturer name
  device->manufacturer = usb_device_read_string(device, desc->manuf_idx);
  // serial string
  device->serial = usb_device_read_string(device, desc->serial_idx);

  kprintf("USB Device\n");
  kprintf("    Product: %s\n", device->product);
  kprintf("    Manufacturer: %s\n", device->manufacturer);
  kprintf("    Serial: %s\n", device->serial);

  // read config descriptors
  device->configs = kmalloc(desc->num_configs * sizeof(void *));
  for (int i = 0; i < desc->num_configs; i++) {
    usb_config_descriptor_t *config = usb_device_read_config_descriptor(device, 0);
    ASSERT(config != NULL);

    kprintf("USB Config Descriptor\n");
    usb_print_config_descriptor(config);
    device->configs[i] = config;
  }

  // TODO: support more than one config?
  ASSERT(desc->num_configs >= 1);
  usb_config_descriptor_t *config = device->configs[0];

  // collect interface descriptors
  device->config = config;
  device->interfaces = kmalloc(config->num_ifs * sizeof(void *));
  memset(device->interfaces, 0, config->num_ifs * sizeof(void *));

  void *ptr = (void *) config;
  void *ptr_end = offset_ptr(config, config->total_len);
  for (int i = 0; i < device->config->num_ifs; i++) {
    usb_if_descriptor_t *if_desc = usb_seek_descriptor(IF_DESCRIPTOR, ptr, ptr_end);
    if (if_desc == NULL) {
      EPRINTF("couldn't find all interfaces\n");
      break;
    }

    ptr = offset_ptr(if_desc, if_desc->length);
    device->interfaces[i] = if_desc;
  }

  // find driver for device
  DPRINTF("locating device driver\n");
  usb_driver_t *driver = NULL;
  uint8_t if_index = 0;
  for (int i = 0; i < config->num_ifs; i++) {
    usb_if_descriptor_t *if_desc = device->interfaces[i];
    if ((driver = locate_device_driver(if_desc->if_class, if_desc->if_subclass)) != NULL) {
      if_index = i;
      break;
    }
  }
  if (driver == NULL) {
    EPRINTF("unable to find compatible driver for device\n");
    return -1;
  }

  // initialize endpoints and select the configuration
  usb_if_descriptor_t *interface = device->interfaces[if_index];
  ASSERT(interface != NULL);
  if (usb_device_configure(device, config, interface) < 0) {
    DPRINTF("failed configure device with config %d, interface %d\n", config->config_val, interface->if_number);
    return -1;
  }

  // initialize device driver
  DPRINTF("using driver \"%s\"\n", driver->name);
  device->driver = driver;
  if (driver->init(device) < 0) {
    EPRINTF("failed to initialize driver \"%s\" for device\n", driver->name);
    device->driver = NULL;
    return -1;
  }

  return 0;
}

int usb_device_configure(usb_device_t *device, usb_config_descriptor_t *config, usb_if_descriptor_t *interface) {
  // TODO: cleanup previous configuration
  // if (device->interfaces != NULL) {
  //   // cleanup endpoints from previous interface
  //   if (usb_device_free_endpoints(device) < 0) {
  //     panic("usb: failed to free device endpoints");
  //   }
  //
  //   kfree(device->interfaces);
  //   device->interfaces = NULL;
  //   device->interface = NULL;
  // }

  usb_host_t *host = device->host;

  void *ptr = offset_ptr(interface, interface->length);
  void *ptr_end = offset_ptr(config, config->total_len);
  for (int j = 0; j < interface->num_eps; j++) {
    usb_ep_descriptor_t *ep_desc = usb_seek_descriptor(EP_DESCRIPTOR, ptr, ptr_end);
    if (ep_desc == NULL) {
      EPRINTF("couldn't find all endpoints\n");
      break;
    }
    ptr = offset_ptr(ep_desc, ep_desc->length);

    usb_endpoint_t *endpoint = kmalloc(sizeof(usb_endpoint_t));
    memset(endpoint, 0, sizeof(usb_endpoint_t));

    endpoint->type = usb_get_endpoint_type(ep_desc);
    endpoint->dir = (ep_desc->ep_addr >> 7) ? USB_IN : USB_OUT;
    endpoint->number = ep_desc->ep_addr & 0xF;
    endpoint->attributes = ep_desc->attributes;
    endpoint->max_pckt_sz = ep_desc->max_pckt_sz;
    endpoint->interval = ep_desc->interval;
    endpoint->device = device;
    endpoint->event_ch = chan_alloc(EVT_RING_SIZE, sizeof(usb_event_t), 0, "usb_endpoint_event_ch");

    if (host->device_impl->init_endpoint(endpoint) < 0) {
      EPRINTF("failed to initialize endpoint %d\n", endpoint->number);
      return -1;
    }

    LIST_ADD(&device->endpoints, endpoint, list);
  }

  device->dev_class = interface->if_class;
  device->dev_subclass = interface->if_subclass;
  device->dev_protocol = interface->if_protocol;

  device->config = config;
  device->interface = interface;

  usb_setup_packet_t set_config = SET_CONFIGURATION(config->config_val);
  if (usb_run_ctrl_transfer(device, set_config, 0, 0) < 0) {
    EPRINTF("failed to select configuration %d\n", config->config_val);
    return -1;
  }

  return 0;
}

int usb_device_free_endpoints(usb_device_t *device) {
  usb_host_t *host = device->host;
  usb_endpoint_t *endpoint = LIST_FIRST(&device->endpoints);
  while (endpoint != NULL) {
    usb_endpoint_t *next = LIST_NEXT(endpoint, list);

    if (host->device_impl->deinit_endpoint(endpoint) < 0) {
      EPRINTF("failed to de-initialize endpoint\n");
      return -1;
    }

    kfree(endpoint);
    endpoint = next;
  }

  LIST_INIT(&device->endpoints);
  return 0;
}

usb_config_descriptor_t *usb_device_read_config_descriptor(usb_device_t *device, uint8_t n) {
  size_t size = sizeof(usb_config_descriptor_t);

LABEL(get_descriptor);
  usb_setup_packet_t get_desc = GET_DESCRIPTOR(CONFIG_DESCRIPTOR, n, size);
  usb_config_descriptor_t *desc = kmalloc(size);
  memset(desc, 0, size);

  if (usb_run_ctrl_transfer(device, get_desc, kheap_ptr_to_phys(desc), size) < 0) {
    EPRINTF("failed to execute transfer\n");
    return NULL;
  }

  if (size < desc->total_len) {
    size = desc->total_len;
    kfree(desc);
    goto get_descriptor;
  }

  return desc;
}

char *usb_device_read_string(usb_device_t *device, uint8_t n) {
  if (n == 0) {
    return NULL;
  }

  // allocate enough to handle most cases
  size_t size = 64;
LABEL(get_descriptor);
  usb_setup_packet_t get_desc = GET_DESCRIPTOR(STRING_DESCRIPTOR, n, size);
  usb_string_t *desc = kmalloc(size);
  memset(desc, 0, size);

  if (usb_run_ctrl_transfer(device, get_desc, kheap_ptr_to_phys(desc), size) < 0) {
    EPRINTF("failed to execute transfer\n");
    return NULL;
  }

  if (size < desc->length) {
    size = desc->length;
    kfree(desc);
    goto get_descriptor;
  }

  // usb string descriptors use utf-16 encoding.
  size_t len = ((desc->length - 2) / 2);
  char *ascii = kmalloc(len + 1); // 1 extra null char
  ascii[len] = 0;

  int result = utf16_iconvn_ascii(ascii, desc->string, len);
  if (result != len) {
    EPRINTF("string descriptor conversion failed\n");
  }

  kfree(desc);
  return ascii;
}

//
// MARK: Debugging
//

void usb_print_device_descriptor(usb_device_descriptor_t *desc) {
  kprintf("  length = %d | usb_version = %x\n", desc->length, desc->usb_ver);
  kprintf("  class = %d | subclass = %d | protocol = %d\n",
          desc->dev_class, desc->dev_subclass, desc->dev_protocol);
  kprintf("  max_packet_sz_ep0 = %d\n", desc->max_packt_sz0);
  kprintf("  vendor_id = %x | product_id = %x | dev_release = %x\n",
          desc->vendor_id, desc->product_id, desc->dev_release);
  kprintf("  product_idx = %d | manuf_idx = %d | serial_idx = %d\n",
          desc->product_idx, desc->manuf_idx, desc->serial_idx);
  kprintf("  num_configs = %d \n", desc->num_configs);
}

void usb_print_config_descriptor(usb_config_descriptor_t *desc) {
  kprintf("  type = %d | length = %d | total length = %d\n", desc->type, desc->length, desc->total_len);
  kprintf("  num_ifs = %d | config_val = %d | this_idx = %d\n", desc->num_ifs, desc->config_val, desc->this_idx);
  kprintf("  attributes = %#b | max_power = %d\n", desc->attributes, desc->max_power);

  void *ptr = (void *) desc;
  void *ptr_end = offset_ptr(desc, desc->total_len);
  for (int i = 0; i < desc->num_ifs; i++) {
    usb_if_descriptor_t *if_desc = usb_seek_descriptor(IF_DESCRIPTOR, ptr, ptr_end);
    if (if_desc == NULL) {
      return;
    }
    ptr = offset_ptr(if_desc, if_desc->length);

    kprintf("  interface %d\n", if_desc->if_number);
    kprintf("    type = %d | length = %d\n", if_desc->type, if_desc->length);
    kprintf("    num_eps = %d | alt_setting = %d\n", if_desc->num_eps, if_desc->alt_setting);
    kprintf("    if_class = %d | if_subclass = %d | if_protocol = %d\n",
            if_desc->if_class, if_desc->if_subclass, if_desc->if_protocol);
    kprintf("    this_idx = %d\n", if_desc->this_idx);

    for (int j = 0; j < if_desc->num_eps; j++) {
      usb_ep_descriptor_t *ep_desc = usb_seek_descriptor(EP_DESCRIPTOR, ptr, ptr_end);
      if (ep_desc == NULL) {
        return;
      }
      ptr = offset_ptr(ep_desc, ep_desc->length);

      uint8_t ep_num = ep_desc->ep_addr & 0xF;
      uint8_t ep_dir = (ep_desc->ep_addr >> 7) & 1;
      kprintf("    endpoint %d - %s\n", ep_num, ep_dir == USB_EP_IN ? "IN" : "OUT");
      kprintf("      type = %d | length = %d\n", ep_desc->type, ep_desc->length);
      kprintf("      address = %#b | attributes = %#b\n", ep_desc->ep_addr, ep_desc->attributes);
      kprintf("      max_packet_size = %d | interval = %d\n", ep_desc->max_pckt_sz, ep_desc->interval);
    }
  }
}
