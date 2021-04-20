//
// Created by Aaron Gill-Braun on 2021-04-17.
//

#ifndef KERNEL_USB_HID_REPORT_H
#define KERNEL_USB_HID_REPORT_H

#include <base.h>

#define PREFIX_SIZE(p) (((p) >> 0) & 0x3)
#define PREFIX_TYPE(p) (((p) >> 2) & 0x3)
#define PREFIX_TAG(p) (((p) >> 4) & 0xF)

#define TYPE_MAIN   0
#define TYPE_GLOBAL 1
#define TYPE_LOCAL  2

//
// ----- main items -----
//

#define INPUT_TAG          0x08
#define OUTPUT_TAG         0x09
#define FEATURE_TAG        0x0B
#define COLLECTION_TAG     0x0A
#define END_COLLECTION_TAG 0x0C

#define MAIN_ITEM_BIT0 // data (0) constant (1)
#define MAIN_ITEM_BIT1 // array (0) variable (1)
#define MAIN_ITEM_BIT2 // absolute (0) relative (1)
#define MAIN_ITEM_BIT3 // no wrap (0) wrap (1)
#define MAIN_ITEM_BIT4 // linear (0) non linear (1)
#define MAIN_ITEM_BIT5 // preferred state (0) no preferred (1)
#define MAIN_ITEM_BIT6 // no null position (0) null state (1)
#define MAIN_ITEM_BIT7 // non volatile (0) volatile (1)
#define MAIN_ITEM_BIT8 // bit field (0) buffered bytes (1)

#define COLLECTION_PHYSICAL     0x00
#define COLLECTION_APPLICATION  0x01
#define COLLECTION_LOGICAL      0x02
#define COLLECTION_REPORT       0x03
#define COLLECTION_NAMED_ARRAY  0x04
#define COLLECTION_USAGE_SWITCH 0x05
#define COLLECTION_USAGE_MOD    0x06
#define COLLECTION_ROOT         0xFF // custom type

//
// ----- global items -----
//

#define USAGE_PAGE_TAG       0x00
#define LOGICAL_MINIMUM_TAG  0x01
#define LOGICAL_MAXIMUM_TAG  0x02
#define PHYSICAL_MINIMUM_TAG 0x03
#define PHYSICAL_MAXIMUM_TAG 0x04
#define UNIT_EXPONENT_TAG    0x05
#define UNIT_TAG             0x06
#define REPORT_SIZE_TAG      0x07
#define REPORT_ID_TAG        0x08
#define REPORT_COUNT_TAG     0x09
#define PUSH_TAG             0x0A
#define POP_TAG              0x0B

//
// ----- local items -----
//

#define USAGE_TAG              0x00
#define USAGE_MINIMUM_TAG      0x01
#define USAGE_MAXIMUM_TAG      0x02
#define DESIGNATOR_INDEX_TAG   0x03
#define DESIGNATOR_MINIMUM_TAG 0x04
#define DESIGNATOR_MAXIMUM_TAG 0x05
#define STRING_INDEX_TAG       0x07
#define STRING_MINIMUM_TAG     0x08
#define STRING_MAXIMUM_TAG     0x09
#define DELIMITER_TAG          0x0A

//

typedef enum {
  COLLECTION_NODE,
  ITEM_NODE,
  USAGE_NODE,
} node_type_t;

typedef struct base_node {
  node_type_t type;
  struct base_node *next;
} base_node_t;

typedef struct usage_node {
  node_type_t type;
  base_node_t *next;

  uint32_t usage;
  uint32_t usage_min;
  uint32_t usage_max;
} usage_node_t;

typedef struct item_node {
  node_type_t type;
  base_node_t *next;

  uint16_t kind;
  uint16_t data;

  uint32_t usage_page;
  uint32_t logical_min;
  uint32_t logical_max;
  uint32_t physical_min;
  uint32_t physical_max;
  uint32_t report_size;
  uint32_t report_id;
  uint32_t report_count;

  usage_node_t *usages;
} item_node_t;

typedef struct collection_node {
  node_type_t type;
  base_node_t *next;

  uint32_t kind;
  uint32_t usage_page;
  uint32_t usage;
  base_node_t *children;
} collection_node_t;


typedef struct {
  collection_node_t *root;
  size_t size;
} report_format_t;


report_format_t *hid_parse_report_descriptor(uint8_t *desc, size_t length);

#endif
