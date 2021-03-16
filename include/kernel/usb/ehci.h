//
// Created by Aaron Gill-Braun on 2021-02-19.
//

#ifndef DRIVERS_EHCI_H
#define DRIVERS_EHCI_H

#include <base.h>

#define EHCI_MMIO_SIZE

typedef struct {
  uintptr_t phys_addr;
  uintptr_t virt_addr;
  size_t length;
} ehci_device_t;


//
// ======= Registers =======
//

// -------- Capability Registers --------

// Capability Register
typedef struct packed {
  uint8_t length;       // register length
  uint8_t reserved;     // reserved
  uint16_t hci_version; // interface version
  struct {
    uint32_t n_ports : 4;     // number of ports
    uint32_t ppc : 1;         // supports implements port power control
    uint32_t : 2;             // reserved
    uint32_t prt : 1;         // port routing rules
    uint32_t n_pcc : 4;       // number of ports per companion controller
    uint32_t n_cc : 4;        // number of companion controllers
    uint32_t p_indicator : 1; // supports port indicators
    uint32_t : 15;            // reserved
  } hcs_params;  // structural parameters;
  struct {
    uint32_t addr64 : 1;           // supports 64-bit addressing
    uint32_t prog_fr_lst_flg : 1;  // programmable frame list flag
    uint32_t async_sched_park : 1; // asynchronous schedule park capability
    uint32_t : 1;                  // reserved
    uint32_t isoc_sched_thres : 4; // isochronous scheduling threshold
    uint32_t ehci_ext_cap_ptr : 8; // ehci extended capabilities pointer
    uint32_t : 16;                 // reserved
  } hcc_params; // capability parameters
  uint64_t hcsp_portrt; // companion port route description
} ehci_cap_regs_t;
static_assert(sizeof(ehci_cap_regs_t) == 20);

// -------- Operational Registers --------

// USB Command Register
typedef union {
  uint32_t raw;
  struct {
    uint32_t run : 1;            // run/stop
    uint32_t hc_reset : 1;       // host controller reset
    uint32_t frame_lst_sz : 2;   // frame list size
    uint32_t per_sched_en : 1;   // periodic schedule enable
    uint32_t async_sched_en : 1; // asynchronous schedule enable
    uint32_t int_async_adv : 1;  // interrupt on async advance doorbell
    uint32_t : 8;                // reserved
    uint32_t int_thres_ctrl : 8; // interrupt threshold control
    uint32_t : 8;                // reserved
  };
} ehci_usb_cmd_reg_t;
static_assert(sizeof(ehci_usb_cmd_reg_t) == 4);

// USB Status Register
typedef union {
  uint32_t raw;
  struct {
    uint32_t usb_int : 1;         // usb interrupt (completion success)
    uint32_t usb_err_int : 1;     // usb error interrupt (completion failure
    uint32_t port_change : 1;     // port change detect
    uint32_t frame_lst_ro : 1;    // frame list rollover
    uint32_t host_sys_err : 1;    // host system error
    uint32_t int_async_adv : 1;   // interrupt on async advance
    uint32_t : 6;                 // reserved
    uint32_t hc_halted : 1;       // host controller halted (ro)
    uint32_t reclamation : 1;     // reclamation
    uint32_t per_sched_sts : 1;   // period schedule status
    uint32_t async_sched_sts : 1; // asynchronous schedule status
    uint32_t : 16;                // reserved
  };
} ehci_usb_sts_reg_t;
static_assert(sizeof(ehci_usb_sts_reg_t) == 4);

// USB Interrupt Enable Register
typedef union {
  uint32_t raw;
  struct {
    uint32_t usb_en : 1;          // usb interrupt enable
    uint32_t usb_err_en : 1;      // usb error interrupt enable
    uint32_t port_chg_en : 1;     // port chage interrupt enable
    uint32_t frame_lst_ro_en : 1; // frame list rollover interrupt enable
    uint32_t host_sys_err_en : 1; // host system error interrupt enable
    uint32_t async_adv_en : 1;    // interrupt on async advance enable
    uint32_t : 26;                // reserved
  };
} ehci_usb_intr_reg_t;
static_assert(sizeof(ehci_usb_intr_reg_t) == 4);

// Frame Index Register
typedef union {
  uint32_t raw;
  struct {
    uint32_t frame_index : 14; // frame index
    uint32_t : 18;             // reserved
  };
} ehci_frindex_reg_t;
static_assert(sizeof(ehci_frindex_reg_t) == 4);

// Port Status and Control Register
typedef union {
  uint32_t raw;
  struct {
    uint32_t conn_sts : 1;        // current connect status
    uint32_t conn_sts_chg : 1;    // connect status change
    uint32_t port_en : 1;         // port enabled/disabled
    uint32_t port_en_chg : 1;     // port enabled/disabled change
    uint32_t overcur_active : 1;  // over-current active
    uint32_t overcur_chg : 1;     // over-current change
    uint32_t force_port_res : 1;  // force port resume
    uint32_t suspend : 1;         // suspend
    uint32_t port_reset : 1;      // port reset
    uint32_t : 1;                 // reserved
    uint32_t line_status : 2;     // line status
    uint32_t port_power : 1;      // port power
    uint32_t port_owner : 1;      // port owner
    uint32_t port_indc_ctrl : 2;  // port indicator control
    uint32_t port_test_ctrl : 4;  // port test control
    uint32_t wake_conn_en : 1;    // wake on connect enable
    uint32_t wake_disconn_en : 1; // wake on disconnect enable
    uint32_t wake_overcur_en : 1; // wake on over-current enable
    uint32_t : 9;                 // reserved
  };
} ehci_portsc_reg_t;
static_assert(sizeof(ehci_portsc_reg_t) == 4);

// USB Registers
typedef struct {
  ehci_usb_cmd_reg_t  usbcmd;
  ehci_usb_sts_reg_t  usbsts;
  ehci_usb_intr_reg_t usbintr;
  ehci_frindex_reg_t  frindex;
  uint32_t dsegment;
  uint32_t periodicbase;
  uint32_t asynclistaddr;
  uint8_t reserved[36];
  uint32_t configflag;
  ehci_portsc_reg_t portsc[];
} ehci_op_regs_t;
static_assert(sizeof(ehci_op_regs_t) == 68);

//
// ======= Data Structures =======
//

// structure types
#define EHCI_STRUCT_iTD  0 // isochronous transfer descriptor
#define EHCI_STRUCT_QH   1 // queue head
#define EHCI_STRUCT_siTD 2 // split transaction isochronous transfer descriptor
#define EHCI_STRUCT_FSTN 3 // frame span traversal node

// Frame List Link Pointer
typedef struct {
  uint32_t t : 1;    // identifies end of list
  uint32_t typ : 2;  // pointer type
  uint32_t : 2;      // reserved
  uint32_t ptr : 26; // pointer
} ehci_frlist_link_ptr_t;
static_assert(sizeof(ehci_frlist_link_ptr_t) == 4);

// iTD Transaction Status and Control
typedef struct {
  // control
  uint32_t tx_offset : 12;  // transaction x offset
  uint32_t page_select : 3; // page select
  uint32_t ioc : 1;         // interrupt on complete
  uint32_t tx_length : 12;  // transaction length
  // status
  uint32_t tx_err : 1;  // transaction error
  uint32_t babble : 1;  // babble detected
  uint32_t buf_err : 1; // data buffer error
  uint32_t active : 1;  // active
} ehci_idt_txsc_t;
static_assert(sizeof(ehci_idt_txsc_t) == 4);

// Page 0
typedef struct {
  uint32_t address : 7; // device address
  uint32_t : 1;         // reserved
  uint32_t endpt : 4;   // endpoint number
  uint32_t ptr : 20;    // buffer pointer (page 0)
} ehci_idt_bufptr0_t;
static_assert(sizeof(ehci_idt_bufptr0_t) == 4);

// Page 1
typedef struct {
  uint32_t max_packet_sz : 11; // maximum packet size
  uint32_t direction : 1;      // direction (i/o)
  uint32_t ptr : 20;           // buffer pointer (page 1)
} ehci_idt_bufptr1_t;
static_assert(sizeof(ehci_idt_bufptr1_t) == 4);

// Page 2
typedef struct {
  uint32_t multi : 2; // multi
  uint32_t : 10;      // reserved
  uint32_t ptr : 20;  // buffer pointer (page 2)
} ehci_idt_bufptr2_t;
static_assert(sizeof(ehci_idt_bufptr2_t) == 4);

// Pages 3-6
typedef struct {
  uint32_t : 12;     // reserved
  uint32_t ptr : 20; // buffer pointer
} ehci_idt_bufptr_t;
static_assert(sizeof(ehci_idt_bufptr_t) == 4);

// Isochronous Transfer Descriptor
typedef struct {
  ehci_frlist_link_ptr_t next;
  ehci_idt_txsc_t txsc_list[8];
  ehci_idt_bufptr0_t bufptr0;
  ehci_idt_bufptr1_t bufptr1;
  ehci_idt_bufptr2_t bufptr2;
  ehci_idt_bufptr_t bufptr_list[3];
} ehci_itd_t;
static_assert(sizeof(ehci_itd_t) == 60);

// -------- Asynchronous Data Structures --------

#define PID_OUT   0b00
#define PID_IN    0b01
#define PID_SETUP 0b10

// Queue Head
typedef struct {
  // dword 0
  uint32_t t : 1;              // terminate (1 = last qh)
  uint32_t typ : 2;            // type (0b01 -> queue head)
  uint32_t : 2;                // reserved
  uint32_t qhlp : 27;          // queue head link pointer
  // dword 1
  uint32_t address : 7;        // device address
  uint32_t inactivate : 1;     // inactivate on next transfer
  uint32_t endpoint : 4;       // endpoint number
  uint32_t eps : 2;            // endpoint speed
  uint32_t dtc : 1;            // data toggle control
  uint32_t recl_hd_flg : 1;    // head of reclamation list flag
  uint32_t max_pkt_len : 11;   // maximum packet length (max value 0x400)
  uint32_t ctrl_endpt_flg : 1; // control endpoint flag
  uint32_t nak_count_rl : 4;   // nak count reload
  // dword 2
  uint32_t uframe_smask : 8;   // interrupt schedule mask
  uint32_t uframe_cmask : 8;   // split completion mask
  uint32_t hub_addr : 6;       // usb hub device address
  uint32_t port_num : 7;       // port number
  uint32_t mult : 3;           // high-bandwidth pipe multiplier
  // dword 3
 uint32_t : 5;                 // reserved
 uint32_t qtd_ptr : 27;        // current element tx descriptor link pointer
} ehci_qh_t;
static_assert(sizeof(ehci_qh_t) == 16);

// Queue Element Transfer Descriptor
typedef struct {
  // dword 0
  uint32_t t : 1;             // terminate
  uint32_t : 4;               // reserved
  uint32_t next_ptr : 27;     // next qtd pointer (physical)
  // dword 1
  uint32_t t_alt : 1;         // terminate
  uint32_t nak_count: 4;      // reserved
  uint32_t next_alt_ptr : 27; // next (alternate) qtd pointer
  // dword 2
  // --- status ---
  uint32_t ping_state : 1;     // ping state (0 = do OUT | 1 = do PING)
  uint32_t split_tx_state : 1; // split transaction state (0 = start split | 1 = complete split)
  uint32_t missed_uframe : 1;  // host missed complete-split transaction
  uint32_t tx_err : 1;         // transaction error
  uint32_t babble : 1;         // babble detected
  uint32_t data_buf_err : 1;   // data buffer error
  uint32_t halted : 1;         // halted (serious error)
  uint32_t active : 1;         // active
  // --------------
  uint32_t pid_code : 2;       // token encoding
  uint32_t err_count : 2;      // error counter
  uint32_t cur_page : 3;       // current page (index into buffer ptr list)
  uint32_t ioc : 1;            // interrupt on complete
  uint32_t total_bytes : 15;   // total bytes to transfer
  uint32_t data_toggle : 1;    // data toggle
  // dword 3-7
  uint32_t buffer_ptr[5];      // buffer pointer list
} ehci_qtd_t;
static_assert(sizeof(ehci_qtd_t) == 32);


void ehci_init();
void ehci_host_init();

#endif
