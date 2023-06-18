//
// Created by Aaron Gill-Braun on 2021-04-04.
//

#ifndef KERNEL_USB_XHCI_HW_H
#define KERNEL_USB_XHCI_HW_H

#include <kernel/base.h>

//
// Definitions for XHCI registers and structures
// as are defined in the specification.
//

#define CAP ((xhci)->cap_base)
#define OP ((xhci)->op_base)
#define RT ((xhci)->rt_base)
#define DB ((xhci)->db_base)
#define XCAP ((xhci)->xcap_base)

#define xhci_cap ((xhci_cap_regs_t *)(xhci->cap_base))
#define xhci_op ((xhci_op_regs_t *)(xhci->op_base))
#define xhci_port(n) (&((xhci_port_regs_t *)(xhci->op_base + 0x400))[n])
#define xhci_rt ((xhci_rt_regs_t *)(xhci->rt_base))
#define xhci_intr(n) (&((xhci_intr_regs_t *)(xhci->rt_base + 0x20))[n])
#define xhci_db(n) (&((xhci_db_regs_t *)(xhci->db_base))[n])

#define read32(b, r) (*(volatile uint32_t *)((b) + (r)))
#define write32(b, r, v) ((*(volatile uint32_t *)((b) + (r)) = v))
// #define read64(b, r) (read32(b, r) | (uint64_t) read32(b, r + 4) << 32)
// #define write64(b, r, v) { write32(b, r, V64_LOW(v)); write32(b, r + 4, V64_HIGH(v)); }
#define read64(b, r) (*(volatile uint64_t *)((b) + (r)))
#define write64(b, r, v) (*(volatile uint64_t *)((b) + (r)) = v)
#define read64_split(b, r) ((uint64_t) read32(b, r) | ((uint64_t) read32(b, (r) + 8) << 32))
#define write64_split(b, r, v) ({ write32(b, (r) + 8, V64_HIGH(v)); write32(b, r, V64_LOW(v)); })

#define addr_read64(b, r) ((read32(b, r) & 0xFFFFFFE0) | (uint64_t) read32(b, r + 4) << 32)
#define addr_write64(b, r, v) { write32(b, r, A64_LOW(v)); write32(b, r + 4, A64_HIGH(v)); }

#define mask_64a_addr(a) ((a) & ~(0x1FULL))
#define mask_low5(v) ((v) & 0x1F)

#define or_write32(b, r, v) write32(b, r, read32(b, r) | (v))

#define as_trb(trb) ((xhci_trb_t *)(&(trb)))
#define cast_trb(trb) (*((xhci_trb_t *)(trb)))
#define downcast_trb(trb, type) (*((type *)(trb)))
#define upcast_trb_ptr(trb) ((xhci_trb_t *)((void *)(trb)))

#define cast_trb_ptr(trb) ((xhci_trb_t *)(trb))
#define clear_trb(trb) memset(trb, 0, sizeof(xhci_trb_t))

#define A64_LOW(addr) ((addr) & 0xFFFFFFC0)
#define A64_HIGH(addr) (((addr) >> 32) & 0xFFFFFFFF)
#define A64_MASK 0xFFFFFFFFFFFFFFC0
#define V64_LOW(v) ((v) & UINT32_MAX)
#define V64_HIGH(v) (((v) >> 32) & UINT32_MAX)

//
// -------- Registers --------
//

// -------- Capability Registers --------
typedef volatile struct {
  // dword 0
  uint32_t length : 8;
  uint32_t : 8;
  uint32_t hciversion : 16;
  // dword 1
  union {
    uint32_t hcsparams1_r;
    struct {
      uint32_t max_slots : 8;     // number of device slots
      uint32_t max_intrs : 11;    // number of interrupters
      uint32_t : 5;               // reserved
      uint32_t max_ports : 8;     // number of ports
    } hcsparams1;
  };
  // dword 2
  union {
    uint32_t hcsparams2_r;
    struct {
      uint32_t ist : 4;           // isochronous scheduling threshold
      uint32_t erst_max : 4;      // event ring segment table max
      uint32_t : 13;              // reserved
      uint32_t max_scrtch_hi : 5; // max scratchpad buffers (high)
      uint32_t spr : 1;           // scratchpad restore
      uint32_t max_scrtch_lo : 5; // max scratchpad buffers (low)
    } hcsparams2;
  };
  // dword 3
  union {
    uint32_t hcsparams3_r;
    struct {
      uint32_t u1_dev_latency : 8;  // U1 device exit latency
      uint32_t : 8;                 // reserved
      uint32_t u2_dev_latency : 16; // U2 device exit latency
    } hcsparams3;
  };
  // dword 4
  union {
    uint32_t hccparams1_r;
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
  };
  // dword 5
  uint32_t dboff;
  // dword 6
  uint32_t rtsoff;
  // dword 7
  union {
    uint32_t hccparams2_r;
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
  };
  // dword 8
  uint32_t vtios_offset;            // virtualization based trusted io register space offset
} xhci_cap_regs_t;

#define XHCI_CAP_LENGTH     0x00
#define   CAP_LENGTH(v)       ((v) & 0xFF)
#define   CAP_VERSION(v)      (((v) >> 16) & 0xFFFF)
#define XHCI_CAP_HCSPARAMS1 0x04
#define   CAP_MAX_SLOTS(v)    (((v) >> 0) & 0xFF)
#define   CAP_MAX_INTRS(v)    (((v) >> 8) & 0x7FF)
#define   CAP_MAX_PORTS(v)    (((v) >> 24) & 0xFF)
#define XHCI_CAP_HCSPARAMS2 0x08
#define   HCSPARAMS2_ERST_MAX(v) (((v) >> 4) & 0xF)
#define   HCSPARAMS2_MAX_SCRATCHPAD(v) (((((v) >> 21) & 0x1F) << 5) | (((v) >> 27) & 0x1F))
#define XHCI_CAP_HCSPARAMS3 0x0C
#define XHCI_CAP_HCCPARAMS1 0x10
#define   HCCPARAMS1_AC64(v)  ((v) & 0x1)
#define   HCCPARAMS1_BNC(v)   (((v) >> 1) & 0x1)
#define   HCCPARAMS1_CSZ(v)   (((v) >> 2) & 0x1)
#define   HCCPARAMS1_PPC(v)   (((v) >> 3) & 0x1)
#define   HCCPARAMS1_PIND(v)  (((v) >> 4) & 0x1)
#define   HCCPARAMS1_LHRC(v)  (((v) >> 5) & 0x1)
#define   HCCPARAMS1_XECP(v)  ((((v) >> 16) & 0xFFF) << 2)
#define XHCI_CAP_DBOFF      0x14
#define   DBOFF_OFFSET(v)     ((v) & 0xFFFFFFFC)
#define XHCI_CAP_RTSOFF     0x18
#define   RTSOFF_OFFSET(v)    ((v) & 0xFFFFFFE0)

// -------- Operational Registers --------
typedef volatile struct {
  // dword 0
  union {
    uint32_t usbcmd_r;
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
    } usbcmd;
  };
  // dword 1
  union {
    uint32_t usbsts_r;
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
    } usbsts;
  };
  // dword 2
  uint32_t pagesz : 16;
  uint32_t : 16;
  // dword 3-4
  uint32_t reserved1[2];
  // dword 5
  uint32_t dnctrl;
  // dword 6-7
  union {
    uint64_t crcr_r;
    struct {
      // dword 6
      uint32_t rcs : 1;     // ring cycle state
      uint32_t cs : 1;      // command stop
      uint32_t ca : 1;      // command abort
      uint32_t crr : 1;     // command ring running
      uint32_t : 2;         // reserved
      uint32_t ptr_lo : 27; // ring pointer low
      // dword 7
      uint32_t ptr_hi;      // ring pointer high
    } crcr;
  };
  // dword 8-11
  uint32_t reserved2[2];
  // dword 12-13
  uint64_t dcbaap;
  // dword 14
  union {
    uint32_t config_r;
    struct {
      uint32_t max_slots_en : 8;    // max device slots enabled
      uint32_t u3_entry_en : 1;     // U3 entry enable
      uint32_t config_info_en : 1;  // configuration information enable
      uint32_t : 22;                // reserved
    } config;
  };
} xhci_op_regs_t;
static_assert(offsetof(xhci_op_regs_t, usbcmd) == 0x00);
static_assert(offsetof(xhci_op_regs_t, usbsts) == 0x04);
static_assert(offsetof(xhci_op_regs_t, dnctrl) == 0x14);
static_assert(offsetof(xhci_op_regs_t, crcr) == 0x18);
static_assert(offsetof(xhci_op_regs_t, dcbaap) == 0x30);
static_assert(offsetof(xhci_op_regs_t, config) == 0x38);

#define XHCI_OP_USBCMD      0x00
#define   USBCMD_RUN          (1 << 0) // controller run
#define   USBCMD_HC_RESET     (1 << 1) // host controller reset
#define   USBCMD_INT_EN       (1 << 2) // controller interrupt enable
#define   USBCMD_HS_ERR_EN    (1 << 3) // host error enable
#define XHCI_OP_USBSTS      0x04
#define   USBSTS_HC_HALTED    (1 << 0)  // controller halted
#define   USBSTS_HS_ERR       (1 << 2)  // host error
#define   USBSTS_EVT_INT      (1 << 3)  // event interrupt
#define   USBSTS_PORT_CHG     (1 << 4)  // port change
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
#define   CRCR_PTR(v)         ((uint64_t)(v) & A64_MASK)
#define XHCI_OP_DCBAAP      0x30
#define   DCBAAP_LOW(v)       ((v) & 0xFFFFFFC0)         // device context base array (lo)
#define   DCBAAP_HIGH(v)      (((v) >> 32) & UINT32_MAX) // device context base array (hi)
#define   DCBAAP_PTR(v)       ((uint64_t)(v) & A64_MASK) // device context base array
#define XHCI_OP_CONFIG      0x38
#define   CONFIG_MAX_SLOTS_EN(v) (((v) & 0xFF) << 0)
#define XHCI_OP_PORT(n)     (0x400 + ((n) * 0x10))

// -------- Port Registers --------
typedef volatile struct {
  // dword 0
  union {
    uint32_t portsc_r;
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
    } portsc;
  };
  // dword 1
  uint32_t portpmsc;
  // dword 2
  uint32_t portli;
  // dword 3
  uint32_t porthlpmc;
} xhci_port_regs_t;

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
#define   PORTSC_PRC          (1 << 21) // port reset change
#define   PORTSC_CAS          (1 << 24) // cold attach status
#define   PORTSC_WCE          (1 << 25) // wake on connect enable
#define   PORTSC_WDE          (1 << 26) // wake on disconnect enable
#define   PORTSC_WOE          (1 << 27) // wake on over-current enable
#define   PORTSC_DR           (1 << 30) // device removable
#define   PORTSC_WARM_RESET   (1 << 27) // warm port reset
#define XHCI_PORT_PMSC(n)   (XHCI_OP_PORT(n) + 0x04)
#define XHCI_PORT_LI(n)     (XHCI_OP_PORT(n) + 0x08)
#define XHCI_PORT_HLPMC(n)  (XHCI_OP_PORT(n) + 0x0C)

//    0X00XXXXXXXXXXXXXXXXXXXXXXX0X00X
//   0b10110000000000000000000000010110
// ~ 0b01001111111111111111111111101001
//   0b00000000001000000000000000000000
#define PORTSC_MASK 0x4FFFFFE9 // 0b01001111111111111111111111101001

// -------- Interrupter Registers --------
typedef volatile struct {
  // dword 0
  union {
    uint32_t iman_r;
    struct {
      uint32_t ip : 1;
      uint32_t ie : 1;
      uint32_t : 30;
    } iman;
  };
  // dword 1
  union {
    uint32_t imod_r;
    struct {
      uint32_t imodi : 16;
      uint32_t imodc : 16;
    } imod;
  };
  // dword 2
  uint32_t erstsz : 16;
  uint32_t : 16;
  // dword 3-4
  union {
    uint64_t erstba_r;
    struct {
      uint32_t erstba_lo;
      uint32_t erstba_hi;
    } erstba;
  };
  // 5-6
  union {
    uint64_t erdp_r;
    struct {
      uint32_t desi : 3;
      uint32_t busy : 1;
      uint32_t erdp_lo : 28;
      uint32_t erdp_hi;
    } erdp;
  };
} xhci_intr_regs_t;

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
#define   ERSTBA_PTR(v)       (((uint64_t)(v)) & A64_MASK)
#define XHCI_INTR_ERDP(n)   (0x38 + (32 * (n)))
#define   ERDP_EH_BUSY        (1 << 3)       // event handler busy
#define   ERDP_LOW(v)         A64_LOW(v)  // event ring dequeue pointer
#define   ERDP_HIGH(v)        A64_HIGH(v) // event ring dequeue pointer
#define   ERDP_PTR(v)         (((uint64_t)(v)) & 0xFFFFFFFFFFFFFFF0)

#define ERDP_MASK 0xF

// -------- Runtime Registers --------
typedef volatile struct {
  uint32_t mfindex : 16;
  uint32_t : 16;
  uint32_t reserved[7];
  xhci_intr_regs_t intrs[1024];
} xhci_rt_regs_t;

#define XHCI_RT_MFINDEX     0x00
#define   MFINDEX(v)          ((v) & 0x3FFF)
#define XHCI_RT_INTR(n)     (0x20 + ((n) * 0x20))

// -------- Doorbell Registers --------
typedef volatile union {
  uint32_t db;
  struct {
    uint32_t target : 8;
    uint32_t : 8;
    uint32_t task_id : 16;
  };
} xhci_db_regs_t;

#define XHCI_DB(n)          ((n) * 0x4)
#define   DB_TARGET(v)        (((v) & 0xFF) << 0)
#define   DB_TASK_ID(v)       (((v) & 0xFFFF) << 16)


// -------- XHCI Capabilities --------

#define XHCI_CAP_LEGACY     1
#define XHCI_CAP_PROTOCOL   2
#define XHCI_CAP_POWER_MGMT 3

#define XHCI_PSI_OFFSET 0x10

#define XCAP_ID(v) ((v) & 0xFF)
#define XCAP_NEXT(v) ((((v) >> 8) & 0xFF) << 2)

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

typedef struct xhci_port_speed {
  uint32_t psiv : 4;  // protocol speed id
  uint32_t psie : 2;  // protocol speed id exponent
  uint32_t plt : 2;   // psi type
  uint32_t pfd : 1;   // psi full-duplex
  uint32_t : 5;       // reserved
  uint32_t lp : 2;    // link protocol
  uint32_t psim : 16; // protocol speed id mantissa
} xhci_port_speed_t;

#define XHCI_REV_MAJOR_2 0x02
#define XHCI_REV_MAJOR_3 0x03
#define XHCI_REV_MINOR_0 0x00
#define XHCI_REV_MINOR_1 0x10
#define XHCI_REV_MINOR_2 0x10

typedef struct xhci_cap_protocol {
  // dword0
  uint8_t id;                 // capability id
  uint8_t next;               // next cap pointer
  uint8_t rev_minor;          // minor revision (bcd)
  uint8_t rev_major;          // major revision (bcd)
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

typedef union xhci_trb {
  struct {
    // dword 0-2
    uint32_t : 32;         // reserved
    uint32_t : 32;         // reserved
    uint32_t : 32;         // reserved
    // dword 3
    uint32_t cycle : 1;    // cycle bit
    uint32_t : 9;          // reserved
    uint32_t trb_type : 6; // trb type
    uint32_t : 16;         // reserved
  };
  struct {
    uint64_t qword0;
    uint64_t qword1;
  };
} xhci_trb_t;
static_assert(sizeof(xhci_trb_t) == 16);

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

#define SETUP_DATA_NONE 1
#define SETUP_DATA_OUT  2
#define SETUP_DATA_IN   3

#define DATA_OUT 0
#define DATA_IN  1

// Setup Stage TRB
typedef struct {
  // dword 0
  uint32_t rqst_type : 8;   // bmRequestType
  uint32_t rqst : 8;        // bRequest
  uint32_t value : 16;      // wValue
  // dword 1
  uint32_t index : 16;      // wIndex
  uint32_t length : 16;     // wLength
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
  uint32_t : 3;             // reserved
  uint32_t trb_type : 6;    // trb type
  uint32_t dir : 1;         // direction
  uint32_t : 15;            // reserved
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

#define CC_SUCCESS         1
#define CC_DATA_BUF_ERROR  2
#define CC_BABBLE_DT_ERROR 3
#define CC_USB_TX_ERROR    4
#define CC_TRB_ERROR       5
#define CC_STALL_ERROR     6
#define CC_RESOURCE_ERROR  7
#define CC_BANDWIDTH_ERROR 8
#define CC_NO_SLOTS_ERROR  9
#define CC_SHORT_PACKET    13

// Transfer Event TRB
typedef struct {
  // dword 0 & 1
  uint64_t trb_ptr;         // trb pointer
  // dword 2
  uint32_t trs_length: 24;  // trb transfer length remaining
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

// Configure Endpoint Command TRB
typedef struct {
  // dword 0 & 1
  uint64_t input_ctx;       // input context pointer
  // dword 2
  uint32_t : 32;            // reserved
  // dword 3
  uint32_t cycle : 1;       // cycle bit
  uint32_t : 8;             // reserved
  uint32_t dc : 1;          // deconfigure
  uint32_t trb_type : 6;    // trb type
  uint32_t : 8;             // reserved
  uint32_t slot_id : 8;     // slot id
} xhci_config_ep_cmd_trb_t;
static_assert(sizeof(xhci_config_ep_cmd_trb_t) == 16);

// Evaluate Context Command TRB
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
} xhci_eval_ctx_cmd_trb_t;
static_assert(sizeof(xhci_eval_ctx_cmd_trb_t) == 16);


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
typedef struct xhci_slot_ctx {
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
typedef struct xhci_endpoint_ctx {
  // dword 0
  uint32_t ep_state : 3;     // endpoint state
  uint32_t : 5;              // reserved
  uint32_t mult : 2;         // multi
  uint32_t max_streams : 5;  // max primary streams
  uint32_t lsa : 1;          // linear stream array
  uint32_t interval : 8;     // interval
  uint32_t max_esit_hi : 8;  // max endpoint service time interval (high)
  // dword 1
  uint32_t : 1;               // reserved
  uint32_t cerr : 2;          // error count
  uint32_t ep_type : 3;       // endpoint type
  uint32_t : 1;               // reserved
  uint32_t hid : 1;           // host initiate disable
  uint32_t max_burst_sz : 8;  // max burst size
  uint32_t max_packt_sz : 16; // max packet size
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
typedef struct xhci_input_ctrl_ctx {
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
static_assert(sizeof(xhci_input_ctx_t) == (33 * 32));

// Device Context
// TODO: HANDLE 64-byte contexts!
typedef struct xhci_device_ctx {
  xhci_slot_ctx_t slot;
  xhci_endpoint_ctx_t endpoint[31];
} xhci_device_ctx_t;

// Event Ring Segment Table Entry
typedef struct {
  // dword 0 & 1
  uint64_t rs_addr;       // ring segment base address
  // dword 2
  uint32_t rs_size;       // ring segment size
  // dword 3
  uint32_t : 32;          // reserved
} xhci_erst_entry_t;

#endif
