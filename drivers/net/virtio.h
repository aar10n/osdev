//
// Created by Aaron Gill-Braun on 2025-10-02.
//

#ifndef DRIVERS_NET_VIRTIO_H
#define DRIVERS_NET_VIRTIO_H

#include <kernel/base.h>

#define VIRTIO_VENDOR_ID               0x1AF4
#define VIRTIO_NET_DEVICE_ID_MODERN    0x1041
#define VIRTIO_NET_DEVICE_ID_LEGACY    0x1000

enum {
  VIRTIO_PCI_CAP_COMMON_CFG = 1,
  VIRTIO_PCI_CAP_NOTIFY_CFG = 2,
  VIRTIO_PCI_CAP_ISR_CFG    = 3,
  VIRTIO_PCI_CAP_DEVICE_CFG = 4,
  VIRTIO_PCI_CAP_PCI_CFG    = 5,
};

// generic pci capability descriptor
typedef struct virtio_pci_cap {
  uint8_t  cap_vndr;     // pci capability id (0x09 for virtio)
  uint8_t  cap_next;     // offset to next capability
  uint8_t  cap_len;      // total size of this capability
  uint8_t  cfg_type;     // virtio capability type selector
  uint8_t  bar;          // bar index hosting the structure
  uint8_t  padding[3];   // reserved padding
  uint32_t offset;       // byte offset within the bar
  uint32_t length;       // size of the capability payload
} virtio_pci_cap_t;

typedef struct virtio_pci_notify_cap {
  virtio_pci_cap_t cap;      // embedded capability header
  uint32_t notify_off_multiplier; // multiplier applied to notify offsets
} packed virtio_pci_notify_cap_t;

typedef struct virtio_pci_common_cfg {
  uint32_t device_feature_select; // selects device feature word
  uint32_t device_feature;        // device feature bits for selected word
  uint32_t driver_feature_select; // selects driver feature word
  uint32_t driver_feature;        // driver feature bits for selected word
  uint16_t msix_config;           // shared msix vector for config changes
  uint16_t num_queues;            // total queues exposed by device
  uint8_t  device_status;         // device status register
  uint8_t  config_generation;     // config generation counter

  uint16_t queue_select;          // queue selector/register index
  uint16_t queue_size;            // queue depth reported or programmed
  uint16_t queue_msix_vector;     // per-queue msix vector
  uint16_t queue_enable;          // enables the currently selected queue
  uint16_t queue_notify_off;      // notify offset for selected queue
  uint64_t queue_desc;            // physical address of descriptor table
  uint64_t queue_driver;          // physical address of driver area (avail)
  uint64_t queue_device;          // physical address of device area (used)
} packed virtio_pci_common_cfg_t;

typedef struct virtio_net_config {
  uint8_t  mac[6];              // default mac address
  uint16_t status;              // link status bits
  uint16_t max_virtqueue_pairs; // maximum queue pairs supported
  uint16_t mtu;                 // device advertised mtu
  uint32_t speed;               // link speed in mbps (optional)
  uint8_t  duplex;              // duplex indicator
} packed virtio_net_config_t;

typedef struct virtio_net_hdr_v1 {
  uint8_t  flags;        // checksum and data status flags
  uint8_t  gso_type;     // gso segmentation type
  uint16_t hdr_len;      // ethernet header length
  uint16_t gso_size;     // segmentation size
  uint16_t csum_start;   // checksum start offset
  uint16_t csum_offset;  // checksum offset relative to start
  uint16_t num_buffers;  // number of merged buffers (rx)
} packed virtio_net_hdr_v1_t;

#define VIRTIO_STATUS_ACKNOWLEDGE      0x01
#define VIRTIO_STATUS_DRIVER           0x02
#define VIRTIO_STATUS_DRIVER_OK        0x04
#define VIRTIO_STATUS_FEATURES_OK      0x08
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 0x40
#define VIRTIO_STATUS_FAILED           0x80

#define VIRTIO_F_RING_INDIRECT_DESC    28
#define VIRTIO_F_RING_EVENT_IDX        29
#define VIRTIO_F_VERSION_1             32
#define VIRTIO_F_ACCESS_PLATFORM       33
#define VIRTIO_F_RING_PACKED           34

#define VIRTIO_NET_F_CSUM              0
#define VIRTIO_NET_F_GUEST_CSUM        1
#define VIRTIO_NET_F_CTRL_GUEST_OFFLOADS 2
#define VIRTIO_NET_F_MTU               3
#define VIRTIO_NET_F_MAC               5
#define VIRTIO_NET_F_GSO               6
#define VIRTIO_NET_F_HOST_TSO4         11
#define VIRTIO_NET_F_HOST_TSO6         12
#define VIRTIO_NET_F_MRG_RXBUF         15
#define VIRTIO_NET_F_STATUS            16
#define VIRTIO_NET_F_CTRL_VQ           17
#define VIRTIO_NET_F_CTRL_RX           18
#define VIRTIO_NET_F_CTRL_VLAN         19
#define VIRTIO_NET_F_GUEST_ANNOUNCE    21

#define VIRTIO_NET_S_LINK_UP           0x01
#define VIRTIO_NET_S_ANNOUNCE          0x02

#define VIRTQ_DESC_F_NEXT              0x0001
#define VIRTQ_DESC_F_WRITE             0x0002
#define VIRTQ_DESC_F_INDIRECT          0x0004

typedef struct virtq_desc {
  uint64_t addr;   // guest physical buffer address
  uint32_t len;    // buffer length in bytes
  uint16_t flags;  // descriptor flags
  uint16_t next;   // next descriptor in chain when flag is set
} packed virtq_desc_t;

typedef struct virtq_avail {
  uint16_t flags;  // driver to device flags
  uint16_t idx;    // available ring producer index
  uint16_t ring[]; // descriptor indices ready for device
} packed virtq_avail_t;

typedef struct virtq_used_elem {
  uint32_t id;   // head descriptor id completed by device
  uint32_t len;  // total bytes written or read by device
} packed virtq_used_elem_t;

typedef struct virtq_used {
  uint16_t flags;           // device to driver flags
  uint16_t idx;             // used ring producer index
  virtq_used_elem_t ring[]; // completion records from device
} packed virtq_used_t;

#endif // DRIVERS_NET_VIRTIO_H
