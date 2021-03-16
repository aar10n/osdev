//
// Created by Aaron Gill-Braun on 2021-03-04.
//

#ifndef KERNEL_USB_XHCI_H
#define KERNEL_USB_XHCI_H

#include <base.h>
#include <mm/mm.h>
#include <bus/pcie.h>

//
// -------- Registers --------
//

// -------- Capability Registers --------

typedef volatile struct {
  uint8_t length;                 // capability register length
  uint8_t reserved;               // reserved
  uint16_t hciversion;            // host controller interface version
  union {
    uint32_t raw;
    struct {
      uint32_t max_slots : 8;     // number of device slots
      uint32_t max_intrs : 11;    // number of interrupters
      uint32_t : 5;               // reserved
      uint32_t max_ports : 8;     // number of ports
    };
  } hcsparams1;
  union {
    uint32_t raw;
    struct {
      uint32_t ist : 4;           // isochronous scheduling threshold
      uint32_t erst_max : 4;      // event ring segment table max
      uint32_t : 13;              // reserved
      uint32_t max_scrtch_hi : 5; // max scratchpad buffers (high)
      uint32_t spr : 1;           // scratchpad restore
      uint32_t max_scrtch_lo : 5; // max scratchpad buffers (low)
    };
  } hcsparams2;
  struct {
    uint32_t u1_dev_latency : 8;  // U1 device exit latency
    uint32_t : 8;                 // reserved
    uint32_t u2_dev_latency : 16; // U2 device exit latency
  } hcsparams3;
  struct {
    uint32_t ac64 : 1;            // 64-bit addressing capability
    uint32_t bnc : 1;             // BW negotiation capability
    uint32_t csz : 1;             // context size (1 = 64-byte | 0 = 32-byte)
    uint32_t ppc : 1;             // port power control
    uint32_t pind : 1;            // port indicators
    uint32_t lhrc : 1;            // light HC reset capability
    uint32_t ltc : 1;             // latency tolerance messaging capability
    uint32_t nss : 1;             // no secondary SID capability
    uint32_t pae : 1;             // parse all event data
    uint32_t spc : 1;             // stopped - short packet capability
    uint32_t sec : 1;             // stopped EDTLA capability
    uint32_t cfc : 1;             // contiguous frame id capability
    uint32_t max_psa_size : 4;    // maximum primary stream array size
    uint32_t ext_cap_ptr : 16;    // xHCI extended capabilities pointer (offset in 32-bit words)
  } hccparams1;
  uint32_t db_offset;             // doorbell offset
  uint32_t rt_offset;             // runtime register space offset
  struct {
    uint32_t u3c : 1;             // U3 entry capability
    uint32_t cmc : 1;             // configure endpoint command max exit latency too large capability
    uint32_t fsc : 1;             // force save context capability
    uint32_t ctc : 1;             // compliance transition capability
    uint32_t lec : 1;             // large ESIT payload capability
    uint32_t cic : 1;             // configuration information capability
    uint32_t etc : 1;             // extended tbc capability
    uint32_t etc_tsc : 1;         // extended tbc trb status capability
    uint32_t gsc : 1;             // get/set extended property capability
    uint32_t vtc : 1;             // virtualization based trusted i/o capability
    uint32_t : 22;                // reserved
  } hccparams2;
  uint32_t vtios_offset;          // virtualization based trusted io register space offset
} xhci_cap_regs_t;
static_assert(sizeof(xhci_cap_regs_t) == 36);

// -------- XHCI Capabilities --------

#define XHCI_CAP_LEGACY     1
#define XHCI_CAP_PROTOCOL   2
#define XHCI_CAP_POWER_MGMT 3

typedef volatile struct {
  uint32_t id : 8;   // capability id
  uint32_t next : 8; // next cap pointer
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

// -------- Operational Registers --------

typedef volatile struct {
  // port status and control register (dword 0)
  union {
    uint32_t raw;
    struct {
      uint32_t ccs : 1;            // current connect status
      uint32_t enabled : 1;        // port enabled/disabled
      uint32_t : 1;                // reserved
      uint32_t oca : 1;            // over-current active
      uint32_t reset : 1;          // port reset
      uint32_t pls : 4;            // port link state
      uint32_t power : 1;          // port power
      uint32_t speed : 4;          // port speed
      uint32_t pic : 2;            // port indicator control
      uint32_t lws : 1;            // port link state write strobe
      uint32_t csc : 1;            // connect status change
      uint32_t pec : 1;            // port enabled/disabled change
      uint32_t wrc : 1;            // warm port reset change
      uint32_t occ : 1;            // over-current change
      uint32_t prc : 1;            // port reset change
      uint32_t plc : 1;            // port link state change
      uint32_t cec : 1;            // port config error change
      uint32_t cas : 1;            // cold attach status
      uint32_t wce : 1;            // wake on connect enable
      uint32_t wde : 1;            // wake on disconnect enable
      uint32_t woe : 1;            // wake on over-current enable
      uint32_t : 2;                // reserved
      uint32_t dr : 1;             // device removable
      uint32_t warm_rst : 1;       // warm port reset
    };
  } portsc;
  // port pm status and control register (dword 1)
  union {
    struct {
      uint32_t l1_sts : 3;         // L1 status
      uint32_t rwe : 1;            // remote wake enable
      uint32_t besl : 4;           // best effort service latency
      uint32_t l1_slot : 8;        // L1 device slot
      uint32_t hle : 1;            // hardware lpm enable
      uint32_t : 11;               // reserved
      uint32_t test_ctrl : 4;      // port test control
    } usb2;
    struct {
      uint32_t u1_timeout : 8;     // U1 timeout
      uint32_t u2_timeout : 8;     // U2 timeout
      uint32_t fla : 1;            // force link pm accept
      uint32_t : 15;               // reserved
    } usb3;
  } portpmsc;
  // port link info register (dword 2)
  union {
    struct {
      uint32_t : 32;               // reserved
    } usb2;
    struct {
      uint32_t link_err_cnt : 16;  // link error count
      uint32_t rlc : 4;            // rx lane count
      uint32_t tlc : 4;            // tx lane count
      uint32_t : 8;                // reserved
    } usb3;
  } portli;
  // port hardware lpm control register (dword 3)
  union {
    // port extended status and control register
    struct {
      uint32_t hirdm : 2;          // host initiated resume duration mode
      uint32_t l1_timeout : 8;     // L1 timeout
      uint32_t besld : 4;          // best effort service latency deep
      uint32_t : 16;               // reserved
    } usb2;
    struct {
      // uint32_t link_serr_cnt : 16; // link soft error count
      // uint32_t : 16;               // reserved
      uint32_t : 32;               // reserved
    } usb3;
  } porthlpmc;
} xhci_port_regs_t;
static_assert(sizeof(xhci_port_regs_t) == 16);

typedef volatile union {
  uint32_t raw;
  struct {
    uint32_t run : 1;              // run/stop
    uint32_t hc_reset : 1;         // host controller reset
    uint32_t int_en : 1;           // interrupt enable
    uint32_t hs_err_en : 1;        // host system error enable
    uint32_t : 3;                  // reserved
    uint32_t lhc_reset : 1;        // light host controller reset
    uint32_t save_state : 1;       // controller save state
    uint32_t restore_state : 1;    // controller restore state
    uint32_t wrap_evt_en : 1;      // enable wrap event
    uint32_t u3mfx_stop_en : 1;    // enable U3 MFINDEX stop
    uint32_t : 1;                  // reserved
    uint32_t cem_en : 1;           // CEM enable
    uint32_t ext_tbc_en : 1;       // extended TBC enable
    uint32_t ext_tbc_trb_en : 1;   // extended TBC TRB status enable
    uint32_t vtio_en : 1;          // VTIO enable
    uint32_t : 15;                 // reserved
  };
} xhci_usbcmd_t;
static_assert(sizeof(xhci_usbcmd_t) == 4);

typedef volatile union {
  uint32_t raw;
  struct {
    uint32_t hc_halted : 1;        // host controller halted
    uint32_t : 1;                  // reserved
    uint32_t hs_err : 1;           // host system error
    uint32_t evt_int : 1;          // event interrupt
    uint32_t port_change : 1;      // port change detected
    uint32_t : 3;                  // reserved
    uint32_t save_state : 1;       // save state status
    uint32_t restore_state : 1;    // restore state status
    uint32_t save_restore_err : 1; // save/restore error
    uint32_t not_ready : 1;        // controller not ready
    uint32_t hc_error : 1;         // host controller error
    uint32_t : 19;                 // reserved
  };
} xhci_usbsts_t;

typedef volatile struct {
  uint32_t max_slots_en : 8;    // max device slots enabled
  uint32_t u3_entry_en : 1;     // U3 entry enable
  uint32_t config_info_en : 1;  // configuration information enable
  uint32_t : 22;                // reserved
} xhci_config_reg_t;

typedef volatile union {
  uint64_t raw;
  struct {
    uint64_t rcs : 1;  // ring cycle state
    uint64_t cs : 1;   // command stop
    uint64_t ca : 1;   // command abort
    uint64_t crr : 1;  // command ring running
    uint64_t : 2;      // reserved
    uint64_t ptr : 58; // command ring pointer
  };
} xhci_crcr_reg_t;

typedef volatile struct {
  xhci_usbcmd_t usbcmd;          // usb command
  xhci_usbsts_t usbsts;          // usb status
  uint32_t pagesz : 16;          // page size
  uint32_t : 16;                 // reserved
  uint32_t : 32;                 // reserved
  uint32_t dnctrl;               // device notification control
  xhci_crcr_reg_t cmdrctrl;      // command ring control
  uint32_t reserved3[4];         // reserved
  uint64_t dcbaap;               // device context base address array pointer
  xhci_config_reg_t config;      // configuration register
} xhci_op_regs_t;
// static_assert(sizeof(xhci_op_regs_t) == 36);

// -------- Runtime Registers --------

// Interrupt Register Set
typedef volatile struct {
  // interrupt management register (dword 0)
  uint32_t ip : 1;        // interrupt pending
  uint32_t ie : 1;        // interrupt enable
  uint32_t : 30;          // reserved
  // interrupt moderation register (dword 1)
  // interrupts/sec = 1/(250 * 10^-9sec * interval)
  uint32_t imodi : 16;  // interrupt moderation interval
  uint32_t imodc : 16;  // interrupt moderation counter
  // event ring segment table size register (dword 2)
  uint32_t erstsz : 16;
  uint32_t : 16;
  // reserved (dword 3)
  uint32_t reserved;
  // event ring segment table base address register (dword 4 & 5)
  uint64_t erstba;
  // event ring dequeue pointer register (dword 6 & 7)
  uint64_t erdp;
  // union {
  //   uint64_t raw;
  //   struct {
  //     uint64_t dqsi : 3;     // dequeue erst segment index
  //     uint64_t busy : 1;     // event handler busy
  //     uint64_t erdp_lo : 28; // event ring dequeue pointer (low)
  //     uint64_t erdp_hi : 32; // event ring dequeue pointer (high)
  //   };
  // };
} xhci_intr_regs_t;

typedef volatile struct {
  uint32_t mfindex;           // microframe index
  uint32_t reserved[7];       // reserved
  xhci_intr_regs_t irs[1024]; // interrupter register sets
} xhci_rt_regs_t;


// -------- Doorbell Registers --------

// Doorbell register
typedef volatile struct {
  uint32_t target : 8; // doorbell
  uint32_t : 8;        // reserved
  uint32_t : 16;       // doorbell task id
} xhci_db_reg_t;

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

// Device Context
typedef struct {
  xhci_slot_ctx_t slot_ctx;
  xhci_endpoint_ctx_t endpoint_ctx[31];
} xhci_device_ctx_t;


//
// -------- Transfer Request Blocks --------
//

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


typedef struct {
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

//

// Event Ring Segment Table Entry
typedef struct {
  // dword 0 & 1
  uint64_t rs_addr;       // ring segment base address
  // dword 2
  uint32_t rs_size : 16;  // ring segment size
  uint32_t : 16;          // reserved
  // dword 3
  uint32_t : 32;      // reserved
} xhci_erst_entry_t;

//
//
//

typedef struct xhci_protocol {
  uint8_t rev_major;   // major usb revision
  uint8_t rev_minor;   // minor usb revision
  uint8_t port_offset; // compatible port offset
  uint8_t port_count;  // compatible port count
  struct xhci_protocol *next;
} xhci_protocol_t;

// Software maintains an Event Ring Consumer Cycle State (CCS) bit,
// initializing it to ‘1’ and toggling it every time the Event Ring
// Dequeue Pointer wraps back to the beginning of the Event Ring. If
// the Cycle bit of the Event TRB pointed to by the Event Ring Dequeue
// Pointer equals CCS, then the Event TRB is a valid event.

typedef struct {
  pcie_device_t *pci_dev;
  uintptr_t phys_addr;
  uintptr_t virt_addr;
  size_t size;

  xhci_cap_regs_t *cap;
  xhci_op_regs_t *op;
  xhci_rt_regs_t *rt;
  xhci_db_reg_t *db;
  xhci_port_regs_t *ports;

  uintptr_t *dcbaap;
  xhci_erst_entry_t *erst;

  xhci_trb_t *evt_ring;
  uint32_t evt_max : 15;
  uint32_t : 1;
  uint32_t evt_index : 15;
  uint32_t evt_ccs : 1;

  xhci_trb_t *cmd_ring;
  uint32_t cmd_max : 15;
  uint32_t : 1;
  uint32_t cmd_index : 15;
  uint32_t cmd_ccs : 1;

  xhci_protocol_t *protocols;
} xhci_dev_t;

void xhci_init();


#endif
