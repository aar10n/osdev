//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#ifndef KERNEL_DEVICE_APIC_H
#define KERNEL_DEVICE_APIC_H

#include <base.h>

#define IA32_APIC_BASE_MSR    0x1B
#define IA32_APIC_BASE_BSP    0x100
#define IA32_APIC_BASE_ENABLE 0x800
#define IA32_TSC_DEADLINE_MSR 0x6E0

/* --------- Enumerations ---------*/

typedef enum {
  APIC_ID_MSR            = 0x802,
  APIC_VERSION_MSR       = 0x803,
  APIC_TPR_MSR           = 0x808,
  APIC_PPR_MSR           = 0x80A,
  APIC_EOI_MSR           = 0x80B,
  APIC_LDR_MSR           = 0x80D,
  APIC_SVR_MSR           = 0x80F,
  APIC_ESR_MSR           = 0x828,
  APIC_LVT_CMCI_MSR      = 0x82F,
  APIC_ICR_MSR           = 0x830,
  APIC_LVT_TIMER_MSR     = 0x832,
  APIC_LVT_THERMAL_MSR   = 0x833,
  APIC_LVT_PERFC_MSR     = 0x834,
  APIC_LVT_LINT0_MSR     = 0x835,
  APIC_LVT_LINT1_MSR     = 0x836,
  APIC_LVT_ERROR_MSR     = 0x837,
  APIC_INITIAL_COUNT_MSR = 0x838,
  APIC_CURRENT_COUNT_MSR = 0x839,
  APIC_TIMER_DCR_MSR     = 0x83E,
  APIC_SELF_IPI_MSR      = 0x83F,
} apicx2_reg_t;


// delivery mode
#define APIC_DM_FIXED       0x00000
#define APIC_DM_LOWEST      0x00100
#define APIC_DM_SMI         0x00200
#define APIC_DM_NMI         0x00400
#define APIC_DM_INIT        0x00500
#define APIC_DM_STARTUP     0x00600
#define APIC_DM_EXTINT      0x00700
// level
#define APIC_LVL_DEASSERT   0x00000
#define APIC_LVL_ASSERT     0x04000
// trigger mode
#define APIC_TM_EDGE        0x00000
#define APIC_TM_LEVEL       0x08000
// dest shorthand
#define APIC_DS_SELF        0x40000
#define APIC_DS_ALLINC      0x80000
#define APIC_DS_ALLBUT      0xC0000


#define APIC_FIXED        0
#define APIC_LOWEST_PRIOR 1
#define APIC_SMI          2
#define APIC_NMI          4
#define APIC_INIT         5
#define APIC_START_UP     6
#define APIC_ExtINT       7

#define APIC_DEST_TARGET        0
#define APIC_DEST_SELF          1
#define APIC_DEST_ALL_INCL_SELF 2
#define APIC_DEST_ALL_EXCL_SELF 3

#define APIC_DEST_PHYSICAL 0
#define APIC_DEST_LOGICAL  1

#define APIC_IDLE    0
#define APIC_PENDING 1

#define APIC_DEASSERT 0
#define APIC_ASSERT   1

#define APIC_EDGE  0
#define APIC_LEVEL 1

#define APIC_ONE_SHOT     0
#define APIC_PERIODIC     1
#define APIC_TSC_DEADLINE 2

#define APIC_CLUSTER_MODEL 0x0
#define APIC_FLAT_MODEL    0xF

#define APIC_UNMASK 0
#define APIC_MASK   1

#define APIC_DIVIDE_2   0
#define APIC_DIVIDE_4   1
#define APIC_DIVIDE_8   2
#define APIC_DIVIDE_16  3
#define APIC_DIVIDE_32  4
#define APIC_DIVIDE_64  5
#define APIC_DIVIDE_128 6
#define APIC_DIVIDE_1   7

/* --------- Registers ---------*/

typedef union {
  uint32_t raw;
  struct {
    uint32_t : 24;
    uint32_t id : 8;
  };
} apic_reg_id_t;

typedef union {
  uint32_t raw;
  struct {
    uint32_t version : 8;
    uint32_t : 8;
    uint32_t max_lvt_entry : 8;
    uint32_t can_suppress_eoi : 1;
    uint32_t : 7;
  };
} apic_reg_version_t;

typedef union {
  uint64_t raw;
  struct {
    uint32_t raw_low;
    uint32_t raw_high;
  };
  struct {
    uint64_t vector : 8;
    uint64_t deliv_mode : 3;
    uint64_t dest_mode : 1;
    uint64_t deliv_status : 1;
    uint64_t : 1;
    uint64_t level : 1;
    uint64_t trigger_mode : 1;
    uint64_t : 2;
    uint64_t dest_shorthand : 2;
    uint64_t : 36;
    uint64_t dest : 8;
  };
} apic_reg_icr_t;
#define apic_reg_icr(vec, dm, sm, s, lvl, tm, dsh, dst)  \
  ((apic_reg_icr_t){       \
    .vector = vec, .deliv_mode = dm, .dest_mode = sm, .deliv_status = s, \
    .level = lvl, .trigger_mode = tm, .dest_shorthand = dsh, .dest = dst \
  })
#define apic_icr_status(low) \
  (((low) >> 12) & 1)


typedef union {
  uint32_t raw;
  struct {
    uint32_t vector : 8;
    uint32_t : 4;
    uint32_t deliv_status : 1;
    uint32_t : 3;
    uint32_t mask : 1;
    uint32_t timer_mode : 2;
    uint32_t : 13;
  };
} apic_reg_lvt_timer_t;
#define apic_reg_lvt_timer(vec, ds, m, md)  \
  ((apic_reg_lvt_timer_t){       \
    .vector = vec, .deliv_status = ds, .mask = m, .timer_mode = md \
  })

typedef union {
  uint32_t raw;
  struct {
    uint32_t vector : 8;
    uint32_t deliv_mode : 3;
    uint32_t : 1;
    uint32_t deliv_status : 1;
    uint32_t : 3;
    uint32_t mask : 1;
    uint32_t : 15;
  };
} apic_reg_lvt_perfc_t;
#define apic_reg_lvt_perfc(vec, dm, ds, m)  \
  ((apic_reg_lvt_perfc_t){       \
    .vector = vec, .deliv_mode = dm, .deliv_status = ds, .mask = m \
  })

typedef union {
  uint32_t raw;
  struct {
    uint32_t vector : 8;
    uint32_t deliv_mode : 3;
    uint32_t : 1;
    uint32_t deliv_status : 1;
    uint32_t polarity : 1;
    uint32_t mask : 1;
    uint32_t remote_irr : 1;
    uint32_t trigger_mode : 1;
    uint32_t : 15;
  };
} apic_reg_lvt_lint_t;
#define apic_reg_lvt_lint(vec, dm, ds, p, m, i, tm)  \
  ((apic_reg_lvt_lint_t){       \
    .vector = vec, .deliv_mode = dm, .deliv_status = ds, \
    .polarity = p, .mask = m, .remote_irr = i, .trigger_mode = tm \
  })

typedef union {
  uint32_t raw;
  struct {
    uint32_t divide0 : 2;
    uint32_t : 1;
    uint32_t divide1 : 1;
    uint32_t : 28;
  };
} apic_reg_div_config_t;
#define apic_reg_div_config(div)  \
  ((apic_reg_div_config_t){       \
    .divide0 = (div) & 0b11, .divide1 = (div) >> 2 \
  })

typedef union {
  uint32_t raw;
  struct {
    uint32_t : 24;
    uint32_t logical_id : 8;
  };
} apic_reg_ldr_t;
#define apic_reg_ldr(id)  \
  ((apic_reg_ldr_t){        \
    .logical_id = id      \
  })

typedef union {
  uint32_t raw;
  struct {
    uint32_t : 28;
    uint32_t model : 4;
  };
} apic_reg_dfr_t;
#define apic_reg_dfr(modl)  \
  ((apic_reg_dfr_t){        \
    .model = modl \
  })

typedef struct {
  union {
    uint32_t raw;
    struct {
      uint32_t apr_subclass : 3;
      uint32_t apr_class : 3;
      uint32_t : 25;
    };
  };
} apic_reg_apr_t;
#define apic_reg_apr(cls, subcls)  \
  ((apic_reg_apr_t){               \
    .apr_subclass = subcls, .apr_class = cls \
  })

typedef union {
  uint32_t raw;
  struct {
    uint32_t tpr_subclass : 3;
    uint32_t tpr_class : 3;
    uint32_t : 25;
  };
} apic_reg_tpr_t;
#define apic_reg_tpr(cls, subcls)  \
  ((apic_reg_tpr_t){               \
    .tpr_subclass = subcls, .tpr_class = cls \
  })

typedef union packed {
  uint32_t raw;
  struct {
    uint32_t ppr_subclass : 3;
    uint32_t ppr_class : 3;
    uint32_t : 25;
  };
} apic_reg_ppr_t;
#define apic_reg_ppr(cls, subcls)  \
  ((apic_reg_ppr_t){               \
    .ppr_subclass = subcls, .ppr_class = cls \
  })

typedef union packed {
  uint32_t raw;
  struct {
    uint32_t vector : 8;
    uint32_t enabled : 1;
    uint32_t focus : 1;
    uint32_t : 22;
  };
} apic_reg_svr_t;
#define apic_reg_svr(vec, en, f) \
  ((apic_reg_svr_t){             \
    .vector = vec, .enabled = en, .focus = f \
  })

static_assert(sizeof(apic_reg_id_t) == sizeof(uint32_t));
static_assert(sizeof(apic_reg_version_t) == sizeof(uint32_t));
static_assert(sizeof(apic_reg_icr_t) == sizeof(uint64_t));
static_assert(sizeof(apic_reg_lvt_timer_t) == sizeof(uint32_t));
static_assert(sizeof(apic_reg_lvt_perfc_t) == sizeof(uint32_t));
static_assert(sizeof(apic_reg_lvt_lint_t) == sizeof(uint32_t));
static_assert(sizeof(apic_reg_div_config_t) == sizeof(uint32_t));
static_assert(sizeof(apic_reg_ldr_t) == sizeof(uint32_t));
static_assert(sizeof(apic_reg_dfr_t) == sizeof(uint32_t));
static_assert(sizeof(apic_reg_apr_t) == sizeof(uint32_t));
static_assert(sizeof(apic_reg_tpr_t) == sizeof(uint32_t));
static_assert(sizeof(apic_reg_ppr_t) == sizeof(uint32_t));
static_assert(sizeof(apic_reg_svr_t) == sizeof(uint32_t));

uint8_t apic_get_id();
uint8_t apic_get_version();

void register_apic(uint8_t id);

void apic_init();
void apic_init_periodic(uint64_t ms);
void apic_init_oneshot();
void apic_oneshot(uint64_t ms);
void apic_udelay(uint64_t us);
void apic_mdelay(uint64_t ms);
void apic_send_eoi();

void apic_broadcast_init_ipi(bool assert);
void apic_send_init_ipi(uint8_t dest_id, bool assert);
void apic_send_startup_ipi(uint8_t dest_id, uint8_t vector);

int apic_write_icr(uint32_t low, uint8_t dest_id);

#endif
