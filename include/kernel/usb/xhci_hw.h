//
// Created by Aaron Gill-Braun on 2021-04-04.
//

#ifndef KERNEL_USB_XHCI_HW_H
#define KERNEL_USB_XHCI_HW_H

#include <base.h>
#include <mm.h>

typedef struct xhci_dev xhci_dev_t;

#define CAP ((xhci)->cap_base)
#define OP ((xhci)->op_base)
#define RT ((xhci)->rt_base)
#define DB ((xhci)->db_base)
#define XCAP ((xhci)->xcap_base)

#define read32(b, r) (*(volatile uint32_t *)((b) + (r)))
#define write32(b, r, v) ((*(volatile uint32_t *)((b) + (r)) = v))
#define read64(b, r) (read32(b, r) | (uint64_t) read32(b, r + 4) << 32)
#define write64(b, r, v) { write32(b, r, V64_LOW(v)); write32(b, r + 4, V64_HIGH(v)); }
#define addr_read64(b, r) ((read32(b, r) & 0xFFFFFFE0) | (uint64_t) read32(b, r + 4) << 32)
#define addr_write64(b, r, v) { write32(b, r, A64_LOW(v)); write32(b, r + 4, A64_HIGH(v)); }

#define or_write32(b, r, v) write32(b, r, read32(b, r) | (v))

#define as_trb(trb) ((xhci_trb_t *)(&(trb)))
#define clear_trb(trb) memset(trb, 0, sizeof(xhci_trb_t))

#define V64_LOW(addr) ((addr) & 0xFFFFFFFF)
#define V64_HIGH(addr) (((addr) >> 32) & 0xFFFFFFFF)

#define A64_LOW(addr) ((addr) & 0xFFFFFFE0)
#define A64_HIGH(addr) (((addr) >> 32) & 0xFFFFFFFF)

//
// -------- Registers --------
//

// -------- Capability Registers --------
#define XHCI_CAP_LENGTH     0x00
#define   CAP_LENGTH(v)       ((v) & 0xFF)
#define XHCI_CAP_VERSION    0x00
#define   CAP_VERSION(v)      (((v) >> 16) & 0xFFFF)
#define XHCI_CAP_HCSPARAMS1 0x04
#define   CAP_MAX_SLOTS(v)    (((v) >> 0) & 0xFF)
#define   CAP_MAX_INTRS(v)    (((v) >> 8) & 0x7FF)
#define   CAP_MAX_PORTS(v)    (((v) >> 24) & 0xFF)
#define XHCI_CAP_HCSPARAMS2 0x08
#define XHCI_CAP_HCSPARAMS3 0x0C
#define XHCI_CAP_HCCPARAMS1 0x10
#define   HCCPARAMS1_XECP(v)  (((v) >> 16) & 0xFFF)
#define XHCI_CAP_DBOFF      0x14
#define XHCI_CAP_RTSOFF     0x18

// -------- Operational Registers --------
#define XHCI_OP_USBCMD      0x00
#define   USBCMD_RUN          (1 << 0) // controller run
#define   USBCMD_HC_RESET     (1 << 1) // host controller reset
#define   USBCMD_INT_EN       (1 << 2) // controller interrupt enable
#define   USBCMD_HS_ERR_EN    (1 << 3) // host error enable
#define XHCI_OP_USBSTS      0x04
#define   USBSTS_HC_HALTED    (1 << 0)  // controller halted
#define   USBSTS_HS_ERR       (1 << 1)  // host error
#define   USBSTS_EVT_INT      (1 << 2)  // event interrupt
#define   USBSTS_PORT_CHG     (1 << 3)  // port change
#define   USBSTS_NOT_READY    (1 << 11) // controller not ready
#define   USBSTS_HC_ERR       (1 << 12) // controller error
#define XHCI_OP_PAGESZ      0x08
#define XHCI_OP_DNCTRL      0x14
#define XHCI_OP_CRCR        0x18
#define   CRCR_RCS            (1 << 0) // ring cycle state
#define   CRCR_CS             (1 << 1) // command stop
#define   CRCR_CA             (1 << 2) // command abort
#define   CRCR_CRR            (1 << 3) // command ring running
#define   CRCR_PTR_LOW(v)     ((v) & 0xFFFFFFE)
#define   CRCR_PTR_HIGH(v)    (((v) >> 32) & UINT32_MAX)
#define XHCI_OP_DCBAAP      0x30
#define   DCBAAP_LOW(v)       A64_LOW(v)  // device context base array
#define   DCBAAP_HIGH(v)      A64_HIGH(v) // device context base array
#define XHCI_OP_CONFIG      0x38
#define   CONFIG_MAX_SLOTS_EN(v) (((v) & 0xFF) << 0)
#define XHCI_OP_PORT(n)     (0x400 + ((n) * 0x10))

// -------- Port Registers --------
#define XHCI_PORT_SC(n)     (XHCI_OP_PORT(n) + 0x00)
#define   PORTSC_CCS          (1 << 0)  // current connect status
#define   PORTSC_EN           (1 << 1)  // port enable
#define   PORTSC_OCA          (1 << 3)  // over-current active
#define   PORTSC_RESET        (1 << 4)  // port reset
#define   PORTSC_PLS(v)       (((v) >> 5) & 0x7) // port link state (rw)
#define   PORTSC_POWER        (1 << 9)  // power
#define   PORTSC_SPEED(v)     (((v) >> 10) & 0xF) // speed
#define   PORTSC_CSC          (1 << 17) // connect status change
#define   PORTSC_PEC          (1 << 18) // port enable/disabled change
#define   PORTSC_PRC          (1 << 20) // port reset change
#define   PORTSC_CAS          (1 << 24) // cold attach status
#define   PORTSC_WCE          (1 << 25) // wake on connect enable
#define   PORTSC_WDE          (1 << 26) // wake on disconnect enable
#define   PORTSC_DR           (1 << 30) // device removable
#define   PORTSC_WARM_RESET   (1 << 27) // warm port reset
#define XHCI_PORT_PMSC(n)   (XHCI_OP_PORT(n) + 0x04)
#define XHCI_PORT_LI(n)     (XHCI_OP_PORT(n) + 0x08)
#define XHCI_PORT_HLPMC(n)  (XHCI_OP_PORT(n) + 0x0C)

// -------- Runtime Registers --------
#define XHCI_RT_MFINDEX     0x00
#define   MFINDEX(v)          (((v) >> 0) & 0x3FFF)
#define XHCI_RT_INTR(n)     (0x20 + ((n) * 0x20))

// -------- Interrupter Registers --------
#define XHCI_INTR_IMAN(n)   (0x20 + (32 * (n)))
#define   IMAN_IP             (1 << 0) // interrupt pending
#define   IMAN_IE             (1 << 1) // interrupt enable
#define XHCI_INTR_IMOD(n)   (0x24 + (32 * (n)))
#define   IMOD_INTERVAL(v)    (((v) & 0xFFFF) << 0)  // interval
#define   IMOD_COUNTER(v)     (((v) >> 16) & 0xFFFF) // counter
#define XHCI_INTR_ERSTSZ(n) (0x28 + (32 * (n)))
#define   ERSTSZ(v)           ((v) & 0xFFFF) // event ring segment table size
#define XHCI_INTR_ERSTBA(n) (0x30 + (32 * (n)))
#define   ERSTBA_LOW(v)       A64_LOW(v)  // event ring segment table base address
#define   ERSTBA_HIGH(v)      A64_HIGH(v) // event ring segment table base address
#define XHCI_INTR_ERDP(n)   (0x38 + (32 * (n)))
#define   ERDP_BUSY           (1 << 3)       // event handler busy
#define   ERDP_LOW(v)         A64_LOW(v)  // event ring dequeue pointer
#define   ERDP_HIGH(v)        A64_HIGH(v) // event ring dequeue pointer

// -------- Doorbell Registers --------
#define XHCI_DB(n)          ((n) * 0x4)
#define   DB_TARGET(v)        (((v) & 0xFF) << 0)
#define   DB_TASK_ID(v)       (((v) & 0xFFFF) << 16)


// -------- XHCI Capabilities --------

#define XHCI_CAP_LEGACY     1
#define XHCI_CAP_PROTOCOL   2
#define XHCI_CAP_POWER_MGMT 3

typedef volatile struct {
  uint32_t id : 8;   // capability id
  uint32_t next : 8; // next cap pointer
  uint32_t : 16;     // cap specific
} xhci_cap_t;

// Protocol Capabilites

#define XHCI_FULL_SPEED       1 // full speed (12 mb/s) [usb 2.0]
#define XHCI_LOW_SPEED        2 // low speed (1.5 mb/s) [usb 2.0]
#define XHCI_HIGH_SPEED       3 // high speed (480 mb/s) [usb 2.0]
#define XHCI_SUPER_SPEED_G1X1 4 // super speed gen1 x1 (5 gb/s)  [usb 3.x]
#define XHCI_SUPER_SPEED_G2X1 5 // super speed gen2 x1 (10 gb/s) [usb 3.1]
#define XHCI_SUPER_SPEED_G1X2 6 // super speed gen1 x2 (5 gb/s) [usb 3.2]
#define XHCI_SUPER_SPEED_G2X2 7 // super speed gen2 x2 (10 gb/s) [usb 3.2]

typedef volatile struct {
  uint32_t psiv : 4;  // protocol speed id
  uint32_t psie : 2;  // protocol speed id exponent
  uint32_t plt : 2;   // psi type
  uint32_t pfd : 1;   // psi full-duplex
  uint32_t : 5;       // reserved
  uint32_t lp : 2;    // link protocol
  uint32_t psim : 16; // protocol speed id mantissa
} xhci_port_speed_t;

typedef volatile struct {
  // dword0
  uint32_t id : 8;            // capability id
  uint32_t next : 8;          // next cap pointer
  uint32_t rev_minor : 8;     // minor revision (bcd)
  uint32_t rev_major : 8;     // major revision (bcd)
  // dword1
  uint32_t name_str;          // mnemonic name string
  // dword2
  uint32_t port_offset : 8;   // compatible port offset
  uint32_t port_count : 8;    // compatible port count
  uint32_t reserved : 12;     // protocol specific
  uint32_t psic : 4;          // protocol speed id count
  // dword 3
  uint32_t slot_type : 5;     // protocol slot type
  uint32_t : 27;              // reserved
  // dword 4-N
  xhci_port_speed_t speeds[]; // speed of each port
} xhci_cap_protocol_t;

// Legacy Capabilities

typedef volatile struct {
  // dword0
  uint16_t id : 8;            // capability id
  uint16_t next : 8;          // next cap pointer
  uint8_t bios_sem : 1;       // xhc bios owned semaphore
  uint8_t : 7;                // reserved
  uint8_t os_sem : 1;         // xhc os owned semaphore
  uint8_t : 7;                // reserved
  // dword 1
  uint32_t smi_en : 1;         // usb smi enable
  uint32_t : 3;                // reserved
  uint32_t smi_hse_en: 1;      // smi on host system error enable
  uint32_t : 8;                // reserved
  uint32_t smi_os_own_en : 1;  // smi on os ownership enable
  uint32_t smi_pci_cmd_en : 1; // smi on pci command enable
  uint32_t smi_bar_en : 1;     // smi on bar enable
  uint32_t smi_evt_int : 1;    // mirrors usbsts.evt_int bit
  uint32_t : 3;                // reserved
  uint32_t smi_hse : 1;        // mirrors usbsts.hc_err bit
  uint32_t : 8;                // reserved
  uint32_t smi_os_own_chg : 1; // indicates whether os ownership smi was issued
  uint32_t smi_pci_cmd : 1;    // indicates whether pci cmd_ring smi was issued
  uint32_t smi_on_bar : 1;     // indicates whether bar smi was issued
} xhci_cap_legacy_t;
static_assert(sizeof(xhci_cap_legacy_t) == 8);


//
// -------- Transfer Request Blocks --------
//

// TRB Types
#define TRB_NORMAL         1
#define TRB_SETUP_STAGE    2
#define TRB_DATA_STAGE     3
#define TRB_STATUS_STAGE   4
#define TRB_ISOCH          5
#define TRB_LINK           6
#define TRB_EVENT_DATA     7
#define TRB_NOOP           8
#define TRB_ENABL_SLOT_CMD 9
#define TRB_DISBL_SLOT_CMD 10
#define TRB_ADDR_DEV_CMD   11
#define TRB_CONFIG_EP_CMD  12
#define TRB_EVAL_CTX_CMD   13
#define TRB_RESET_EP_CMD   14
#define TRB_STOP_EP_CMD    15
#define TRB_SET_DQ_PTR_CMD 16
#define TRB_RESET_DEV_CMD  17
// ...
#define TRB_FORCE_HDR_CMD  22
#define TRB_NOOP_CMD       23
// ...
#define TRB_TRANSFER_EVT   32
#define TRB_CMD_CMPL_EVT   33
#define TRB_PORT_STS_EVT   34
// ...
#define TRB_HOST_CTRL_EVT  37
#define TRB_DEV_NOTIF_EVT  38
#define TRB_MFINDEX_EVT    39

typedef struct xhci_trb {
  // dword 0-2
  uint32_t : 32;         // reserved
  uint32_t : 32;         // reserved
  uint32_t : 32;         // reserved
  // dword 3
  uint32_t cycle : 1;    // cycle bit
  uint32_t : 9;          // reserved
  uint32_t trb_type : 6; // trb type
  uint32_t : 16;         // reserved
} xhci_trb_t;

// -------- Transfer TRBs --------

// Normal TRB
typedef struct {
  // dword 0 & 1
  uint64_t buf_ptr;         // data buffer pointer
  // dword 2
  uint32_t trs_length : 17; // trb transfer length
  uint32_t td_size : 5;     // td size
  uint32_t intr_trgt : 10;  // interrupter target
  // dword 3
  uint32_t cycle : 1;       // cycle bit
  uint32_t ent : 1;         // evaluate next trb
  uint32_t isp : 1;         // interrupt on short packet
  uint32_t ns : 1;          // no snoop
  uint32_t ch : 1;          // chain bit
  uint32_t ioc : 1;         // interrupt on completion
  uint32_t idt : 1;         // immediate data
  uint32_t : 2;             // reserved
  uint32_t bei : 1;         // block event interrupt
  uint32_t trb_type : 6;    // trb type
  uint32_t : 16;            // reserved
} xhci_normal_trb_t;
static_assert(sizeof(xhci_normal_trb_t) == 16);

// Control TRBs

// Setup Stage TRB
typedef struct {
  // dword 0
  uint32_t rqst_type : 8;   // request type
  uint32_t rqst : 8;        // request
  uint32_t value : 16;      // value
  // dword 1
  uint32_t index : 16;      // index
  uint32_t length : 16;     // length
  // dword 2
  uint32_t trs_length : 17; // trb transfer length
  uint32_t : 5;             // reserved
  uint32_t intr_trgt : 10;  // interrupter target
  // dword 3
  uint32_t cycle : 1;       // cycle bit
  uint32_t : 4;             // reserved
  uint32_t ioc : 1;         // interrupt on completion
  uint32_t idt : 1;         // immediate data
  uint32_t : 3;             // reserved
  uint32_t trb_type : 6;    // trb type
  uint32_t tns_type : 2;    // transfer type
  uint32_t : 14;            // reserved
} xhci_setup_trb_t;
static_assert(sizeof(xhci_setup_trb_t) == 16);

// Data Stage TRB
typedef struct {
  // dword 0 & 1
  uint64_t buf_ptr;         // data buffer pointer
  // dword 2
  uint32_t trs_length : 17; // trb transfer length
  uint32_t td_size : 5;     // td size
  uint32_t intr_trgt : 10;  // interrupter target
  // dword 3
  uint32_t cycle : 1;       // cycle bit
  uint32_t ent : 1;         // evaluate next trb
  uint32_t isp : 1;         // interrupt on short packet
  uint32_t ns : 1;          // no snoop
  uint32_t ch : 1;          // chain bit
  uint32_t ioc : 1;         // interrupt on completion
  uint32_t idt : 1;         // immediate data
  uint32_t : 2;             // reserved
  uint32_t trb_type : 6;    // trb type
  uint32_t dir : 1;         // direction
  uint32_t : 16;            // reserved
} xhci_data_trb_t;
static_assert(sizeof(xhci_data_trb_t) == 16);

// Status Stage TRB
typedef struct {
  // dword 0 & 1
  uint64_t reserved;        // reserved
  // dword 2
  uint32_t : 22;            // reserved
  uint32_t intr_trgt : 10;  // interrupter target
  // dword 3
  uint32_t cycle : 1;       // cycle bit
  uint32_t ent : 1;         // evaluate next trb
  uint32_t : 2;             // reserved
  uint32_t ch : 1;          // chain bit
  uint32_t ioc : 1;         // interrupt on completion
  uint32_t : 4;             // reserved
  uint32_t trb_type : 6;    // trb type
  uint32_t dir : 1;         // direction
  uint32_t : 15;            // reserved
} xhci_status_trb_t;
static_assert(sizeof(xhci_status_trb_t) == 16);

// Isoch TRB
typedef struct {
  // dword 0 & 1
  uint64_t buf_ptr;         // data buffer pointer
  // dword 2
  uint32_t trb_length : 17; // trb transfer length
  uint32_t td_size : 5;     // td size
  uint32_t intr_trgt : 10;  // interrupter target
  // dword 3
  uint32_t cycle : 1;       // cycle bit
  uint32_t ent : 1;         // evaluate next trb
  uint32_t isp : 1;         // interrupt on short packet
  uint32_t ns : 1;          // no snoop
  uint32_t ch : 1;          // chain bit
  uint32_t ioc : 1;         // interrupt on completion
  uint32_t idt : 1;         // immediate data
  uint32_t tbc : 2;         // transfer burst count
  uint32_t bei : 1;         // block event interrupt
  uint32_t trb_type : 6;    // trb type
  uint32_t tlbpc : 4;       // transfer last burst packet count
  uint32_t frame_id : 11;   // frame id
  uint32_t sia : 1;         // start isoch asap
} xhci_isoch_trb_t;
static_assert(sizeof(xhci_isoch_trb_t) == 16);

// No Op TRB
typedef struct {
  // dword 0 & 1
  uint64_t reserved;        // reserved
  // dword 2
  uint32_t : 22;            // reserved
  uint32_t intr_trgt : 10;  // interrupter target
  // dword 3
  uint32_t cycle : 1;       // cycle bit
  uint32_t ent : 1;         // evaluate next trb
  uint32_t : 2;             // reserved
  uint32_t ch : 1;          // chain bit
  uint32_t ioc : 1;         // interrupt on completion
  uint32_t : 4;             // reserved
  uint32_t trb_type : 6;    // trb type
  uint32_t : 16;            // reserved
} xhci_noop_trb_t;
static_assert(sizeof(xhci_noop_trb_t) == 16);

// -------- Event TRBs --------

// Transfer Event TRB
typedef struct {
  // dword 0 & 1
  uint64_t trb_ptr;         // trb pointer
  // dword 2
  uint32_t trs_length: 24;  // trb transfer length
  uint32_t compl_code : 8;  // completion code
  // dword 3
  uint32_t cycle : 1;       // cycle bit
  uint32_t : 1;             // reserved
  uint32_t ed : 1;          // event data
  uint32_t : 7;             // reserved
  uint32_t trb_type : 6;    // trb type
  uint32_t endp_id : 5;     // endpoint id
  uint32_t : 3;             // reserved
  uint32_t slot_id : 8;     // slot id
} xhci_transfer_evt_trb_t;
static_assert(sizeof(xhci_transfer_evt_trb_t) == 16);

// Command Completion Event TRB
typedef struct {
  // dword 0 & 1
  uint64_t trb_ptr;         // trb pointer
  // dword 2
  uint32_t cmd_compl : 24;  // command completion parameter
  uint32_t compl_code : 8;  // completion code
  // dword 3
  uint32_t cycle : 1;       // cycle bit
  uint32_t : 9;             // reserved
  uint32_t trb_type : 6;    // trb type
  uint32_t vf_id : 8;       // vf id
  uint32_t slot_id : 8;     // slot id
} xhci_cmd_compl_evt_trb_t;
static_assert(sizeof(xhci_cmd_compl_evt_trb_t) == 16);

// Port Status Change Event TRB
typedef struct {
  // dword 0
  uint32_t : 24;            // reserved
  uint32_t port_id: 8;      // port id (root hub port)
  // dword 1
  uint32_t : 32;            // reserved
  // dword 2
  uint32_t : 24;            // trb transfer length
  uint32_t compl_code : 8;  // completion code
  // dword 3
  uint32_t cycle : 1;       // cycle bit
  uint32_t : 9;             // reserved
  uint32_t trb_type : 6;    // trb type
  uint32_t : 16;            // reserved
} xhci_port_status_evt_trb_t;
static_assert(sizeof(xhci_port_status_evt_trb_t) == 16);

// -------- Command TRBs --------

// No Op Command TRB
typedef struct {
  // dword 0 & 1
  uint64_t : 64;            // reserved
  // dword 2
  uint32_t : 32;            // reserved
  // dword 3
  uint32_t cycle : 1;       // cycle bit
  uint32_t : 9;             // reserved
  uint32_t trb_type : 6;    // trb type
  uint32_t : 16;            // reserved
} xhci_noop_cmd_trb_t;
static_assert(sizeof(xhci_noop_cmd_trb_t) == 16);

// Enable Slot Command TRB
typedef struct {
  // dword 0 & 1
  uint64_t : 64;            // reserved
  // dword 2
  uint32_t : 32;            // reserved
  // dword 3
  uint32_t cycle : 1;       // cycle bit
  uint32_t : 9;             // reserved
  uint32_t trb_type : 6;    // trb type
  uint32_t slot_type : 5;   // slot type
  uint32_t : 11;            // reserved
} xhci_enabl_slot_cmd_trb_t;
static_assert(sizeof(xhci_enabl_slot_cmd_trb_t) == 16);

// Disable Slot Command TRB
typedef struct {
  // dword 0 & 1
  uint64_t : 64;            // reserved
  // dword 2
  uint32_t : 32;            // reserved
  // dword 3
  uint32_t cycle : 1;       // cycle bit
  uint32_t : 9;             // reserved
  uint32_t trb_type : 6;    // trb type
  uint32_t : 8;             // reserved
  uint32_t slot_id : 8;     // slot id
} xhci_disbl_slot_cmd_trb_t;
static_assert(sizeof(xhci_disbl_slot_cmd_trb_t) == 16);

// Address Device Command TRB
typedef struct {
  // dword 0 & 1
  uint64_t input_ctx;       // input context pointer
  // dword 2
  uint32_t : 32;            // reserved
  // dword 3
  uint32_t cycle : 1;       // cycle bit
  uint32_t : 8;             // reserved
  uint32_t bsr : 1;         // block set address request
  uint32_t trb_type : 6;    // trb type
  uint32_t : 8;             // reserved
  uint32_t slot_id : 8;     // slot id
} xhci_addr_dev_cmd_trb_t;
static_assert(sizeof(xhci_addr_dev_cmd_trb_t) == 16);

// -------- Other TRBs --------

// Link TRB
typedef struct {
  // dword 0 & 1
  uint64_t rs_addr;          // ring segment base address
  // dword 2
  uint32_t : 24;             // reserved
  uint32_t target : 8;       // interrupter target
  // dword 3
  uint32_t cycle : 1;        // cycle bit
  uint32_t toggle_cycle : 1; // evaluate next trb
  uint32_t : 2;              // reserved
  uint32_t ch : 1;           // chain bit
  uint32_t ioc : 1;          // interrupt on completion
  uint32_t : 4;              // reserved
  uint32_t trb_type : 6;     // trb type
  uint32_t : 16;             // reserved
} xhci_link_trb_t;
static_assert(sizeof(xhci_link_trb_t) == 16);

//
// -------- Data Structures --------
//

// Endpoint Types
#define XHCI_ISOCH_OUT_EP 1
#define XHCI_BULK_OUT_EP  2
#define XHCI_INTR_OUT_EP  3
#define XHCI_CTRL_BI_EP   4
#define XHCI_ISOCH_IN_EP  5
#define XHCI_BULK_IN_EP   6
#define XHCI_INTR_IN_EP   7

// Slot Context
typedef struct {
  // dword 0
  uint32_t route_string : 20;   // route string
  uint32_t speed : 4;           // speed (deprecated)
  uint32_t : 1;                 // reserved
  uint32_t multi_tt : 1;        // multi-tt
  uint32_t hub : 1;             // hub (device is usb hub)
  uint32_t ctx_entries : 5;     // context entries
  // dword 1
  uint32_t max_latency : 16;    // max exit latency
  uint32_t root_hub_port : 8;   // root hub port number
  uint32_t num_ports : 8;       // number of ports
  // dword 2
  uint32_t parent_hub_slot : 8; // parent hub slot id
  uint32_t parent_port_num : 8; // parent port number
  uint32_t tt_think_time : 2;   // tt think time
  uint32_t : 4;                 // reserved
  uint32_t intrptr_target : 10; // interrupter target
  // dword 3
  uint32_t device_addr : 8;     // usb device address
  uint32_t : 19;                // reserved
  uint32_t slot_state : 5;      // slot state
  // dword 4-7
  uint32_t reserved[4];
} xhci_slot_ctx_t;
static_assert(sizeof(xhci_slot_ctx_t) == 32);

// Endpoint Context
typedef struct {
  // dword 0
  uint32_t ep_state : 3;     // endpoint state
  uint32_t : 5;              // reserved
  uint32_t mult : 2;         // multi
  uint32_t max_streams : 5;  // max primary streams
  uint32_t lsa : 5;          // linear stream array
  uint32_t interval : 8;     // interval
  uint32_t max_esit_hi : 8;  // max endpoint service time interval (high)
  // dword 1
  uint32_t : 1;              // reserved
  uint32_t err_count : 2;    // error count
  uint32_t ep_type : 3;      // endpoint type
  uint32_t : 1;              // reserved
  uint32_t hid : 1;          // host initiate disable
  uint32_t max_burst_sz : 8; // max burst size
  uint32_t max_packt_sz : 8; // max packet size
  // dword 2 & 3
  uint64_t tr_dequeue_ptr;   // tr dequeue pointer
  // dword 4
  uint16_t avg_trb_length;   // average trb length
  uint16_t max_esit_lo;      // max endpoint service time interval (low)
  // dword 5-7
  uint32_t reserved[3];      // reserved
} xhci_endpoint_ctx_t;
static_assert(sizeof(xhci_endpoint_ctx_t) == 32);

// Input Control Context
typedef struct {
  uint32_t drop_flags;      // drop context flags
  uint32_t add_flags;       // add context flags
  uint32_t reserved[5];     // reserved
  uint32_t config_val : 8;  // configuration value
  uint32_t intrfc_num : 8;  // interface number
  uint32_t alt_setting : 8; // alternate setting
  uint32_t : 8;             // reserved
} xhci_input_ctrl_ctx_t;
static_assert(sizeof(xhci_input_ctrl_ctx_t) == 32);

// Input Context
typedef struct xhci_input_ctx {
  xhci_input_ctrl_ctx_t ctrl;
  xhci_slot_ctx_t slot;
  xhci_endpoint_ctx_t endpoint[31];
} xhci_input_ctx_t;

// Device Context
typedef struct xhci_device_ctx {
  xhci_slot_ctx_t slot;
  xhci_endpoint_ctx_t endpoint[31];
} xhci_device_ctx_t;

// Event Ring Segment Table Entry
typedef struct {
  // dword 0 & 1
  uint64_t rs_addr;       // ring segment base address
  // dword 2
  uint32_t rs_size : 16;  // ring segment size
  uint32_t : 16;          // reserved
  // dword 3
  uint32_t : 32;          // reserved
} xhci_erst_entry_t;

//

typedef struct xhci_protocol {
  uint8_t rev_major;   // major usb revision
  uint8_t rev_minor;   // minor usb revision
  uint8_t port_offset; // compatible port offset
  uint8_t port_count;  // compatible port count
  struct xhci_protocol *next;
} xhci_protocol_t;

typedef struct {
  xhci_trb_t *ptr;    // ring base
  page_t *page;       // ring pages
  uint32_t index;     // ring enqueue/dequeue index
  uint32_t max_index; // max index
  bool ccs;           // cycle state
} xhci_ring_t;

typedef struct {
  uint8_t number;    // interrupter number
  uint8_t vector;    // mapped interrupt vector
  uintptr_t erst;    // event ring segment table
  xhci_ring_t *ring; // event ring
} xhci_intrptr_t;

typedef struct {
  uint8_t slot_id;
  uint8_t port_num;

  xhci_ring_t *ring;
  xhci_input_ctx_t *input;
  xhci_device_ctx_t *output;
} xhci_device_t;

typedef struct xhci_port {
  uint8_t number;            // port number
  xhci_protocol_t *protocol; // port protocol
  xhci_device_t *device;     // attached device
  struct xhci_port *next;    // linked list
} xhci_port_t;

//
// private api
//

// controller
int xhci_init_controller(xhci_dev_t *xhci);
int xhci_reset_controller(xhci_dev_t *xhci);
int xhci_run_controller(xhci_dev_t *xhci);
void *xhci_execute_cmd_trb(xhci_dev_t *xhci, xhci_trb_t *trb);

// ports
xhci_port_t *xhci_discover_ports(xhci_dev_t *xhci);

// devices
xhci_device_t *xhci_setup_device(xhci_dev_t *xhci, xhci_port_t *port);
int xhci_address_device(xhci_dev_t *xhci, xhci_device_t *device);

// interrupters
xhci_intrptr_t *xhci_seutp_interrupter(xhci_dev_t *xhci, uint8_t n);
xhci_trb_t *xhci_dequeue_event(xhci_dev_t *xhci, xhci_intrptr_t *intrptr);

// doorbells
void xhci_ring_db(xhci_dev_t *xhci, uint8_t slot, uint16_t endpoint);

// capabilities
xhci_cap_t *xhci_get_cap(xhci_dev_t *xhci, xhci_cap_t *cap_ptr, uint8_t cap_id);
xhci_protocol_t *xhci_get_protocols(xhci_dev_t *xhci);

// trb ring
xhci_ring_t *xhci_alloc_ring();
void xhci_free_ring(xhci_ring_t *ring);
bool xhci_ring_enqueue_trb(xhci_ring_t *ring, xhci_trb_t *trb);
bool xhci_ring_dequeue_trb(xhci_ring_t *ring, xhci_trb_t **result);

#endif
