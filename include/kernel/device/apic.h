//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#ifndef KERNEL_DEVICE_APIC_H
#define KERNEL_DEVICE_APIC_H

#include <base.h>

#define IA32_APIC_BASE_MSR    0x1B
#define IA32_APIC_BASE_BSP    0x100
#define IA32_APIC_BASE_ENABLE 0x800

#define APIC_BASE_VA 0xFFFFFFFFF8000000

/* --------- Enumerations ---------*/

typedef enum {
  APIC_ID            = 0x020,
  APIC_VERSION       = 0x030,
  APIC_TPR           = 0x080,
  APIC_APR           = 0x090,
  APIC_PPR           = 0x0A0,
  APIC_EOI           = 0x0B0,
  APIC_RRD           = 0x0C0,
  APIC_LDR           = 0x0D0,
  APIC_DFR           = 0x0E0,
  APIC_SVR           = 0x0F0,
  APIC_ERROR         = 0x280,
  APIC_LVT_CMCI      = 0x2F0,
  APIC_ICR_LOW       = 0x300,
  APIC_ICR_HIGH      = 0x310,
  APIC_LVT_TIMER     = 0x320,
  APIC_LVT_LINT0     = 0x350,
  APIC_LVT_LINT1     = 0x360,
  APIC_LVT_ERROR     = 0x370,
  APIC_INITIAL_COUNT = 0x380,
  APIC_CURRENT_COUNT = 0x390,
  APIC_DIVIDE_CONFIG = 0x3E0,
  APIC_REG_END       = 0x400
} apic_reg_t;

#define APIC_FIXED        0
#define APIC_LOWEST_PRIOR 1
#define APIC_SMI          2
#define APIC_NMI          4
#define APIC_INIT         5
#define APIC_START_UP     6
#define APIC_ExtINT       7

#define APIC_DEST_NONE          0
#define APIC_DEST_SELF          1
#define APIC_DEST_ALL_INCL_SELF 2
#define APIC_DEST_ALL_EXCL_SELF 3

#define APIC_DEST_PHYSICAL 0
#define APIC_DEST_LOGICAL  1

#define APIC_IDLE         0
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

/* --------- Registers ---------*/

typedef union {
  uint64_t raw;
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
  ((apic_reg_lint_t){       \
    .vector = vec, .deliv_mode = dm, .dest_mode = sm, .deliv_status = s, \
    .level = lvl, .trigger_mode = tm, .dest_shorthand = dsh, .dest = dst \
  })

typedef union {
  uint32_t raw;
  struct {
    uint32_t vector : 8;
    uint32_t : 4;
    uint32_t deliv_status : 1;
    uint32_t : 3;
    uint32_t mask : 1;
    uint32_t timer_mode : 1;
    uint32_t : 14;
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


void apic_init();
void apic_send_eoi();

#endif