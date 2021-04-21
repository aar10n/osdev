//
// Created by Aaron Gill-Braun on 2021-04-17.
//

#include <usb/hid-report.h>
#include <usb/hid-usage.h>
#include <mm.h>
#include <printf.h>
#include <string.h>

#define HID_DEBUG

#ifdef HID_DEBUG
#define hid_trace_debug(str, args...) kprintf("[hid] " str "\n", ##args)
#else
#define hid_trace_debug(str, args...)
#endif

#define as_base(n) ((base_node_t *)(n))
#define LL_ADD(p, i) p = link_node(p, i)
#define EXPECT_COUNT(n, args...) \
  if (ptr + (n) > ptr_max) { \
    ptr = ptr_max;           \
    args;            \
    continue;            \
  }

#define PARSER_STACK 10 // maximum levels of nesting

typedef struct {
  uint32_t usage_page;
  uint32_t logical_min;
  uint32_t logical_max;
  uint32_t physical_min;
  uint32_t physical_max;
  uint32_t report_size;
  uint32_t report_id;
  uint32_t report_count;

  collection_node_t *collection;
} parser_state_t;

// string tables

const char *tag_names[][16] = {
  [TYPE_MAIN] = {
    [INPUT_TAG] = "INPUT",
    [OUTPUT_TAG] = "OUTPUT",
    [FEATURE_TAG] = "FEATURE",
    [COLLECTION_TAG] = "COLLECTION",
    [END_COLLECTION_TAG] = "END_COLLECTION",
  },
  [TYPE_GLOBAL] = {
    [USAGE_PAGE_TAG] = "USAGE_PAGE",
    [LOGICAL_MINIMUM_TAG] = "LOGICAL_MINIMUM",
    [LOGICAL_MAXIMUM_TAG] = "LOGICAL_MAXIMUM",
    [PHYSICAL_MINIMUM_TAG] = "PHYSICAL_MINIMUM",
    [PHYSICAL_MAXIMUM_TAG] = "PHYSICAL_MAXIMUM",
    [UNIT_EXPONENT_TAG] = "UNIT_EXPONENT",
    [UNIT_TAG] = "UNIT",
    [REPORT_SIZE_TAG] = "REPORT_SIZE",
    [REPORT_ID_TAG] = "REPORT_ID",
    [REPORT_COUNT_TAG] = "REPORT_COUNT",
    [PUSH_TAG] = "PUSH",
    [POP_TAG] = "POP",
  },
  [TYPE_LOCAL] = {
    [USAGE_TAG] = "USAGE",
    [USAGE_MINIMUM_TAG] = "USAGE_MINIMUM",
    [USAGE_MAXIMUM_TAG] = "USAGE_MAXIMUM",
    [DESIGNATOR_INDEX_TAG] = "DESIGNATOR_INDEX",
    [DESIGNATOR_MINIMUM_TAG] = "DESIGNATOR_MINIMUM",
    [DESIGNATOR_MAXIMUM_TAG] = "DESIGNATOR_MAXIMUM",
    [STRING_INDEX_TAG] = "STRING_INDEX",
    [STRING_MINIMUM_TAG] = "STRING_MINIMUM",
    [STRING_MAXIMUM_TAG] = "STRING_MAXIMUM",
    [DELIMITER_TAG] = "DELIMITER",
  }
};

const char *collection_names[] = {
  [COLLECTION_PHYSICAL] = "Physical",
  [COLLECTION_APPLICATION] = "Application",
  [COLLECTION_LOGICAL] = "Logical",
  [COLLECTION_REPORT] = "Report",
  [COLLECTION_NAMED_ARRAY] = "Named Array",
  [COLLECTION_USAGE_SWITCH] = "Usage Switch",
  [COLLECTION_USAGE_MOD] = "Usage Modifier",
};

//

static inline usage_node_t *alloc_usage_node(uint32_t usage) {
  usage_node_t *node = kmalloc(sizeof(usage_node_t));
  memset(node, 0, sizeof(usage_node_t));
  node->type = USAGE_NODE;
  node->usage = usage;
  return node;
}

static inline item_node_t *alloc_item_node(uint16_t kind, uint16_t data) {
  item_node_t *node = kmalloc(sizeof(item_node_t));
  memset(node, 0, sizeof(item_node_t));
  node->type = ITEM_NODE;
  node->kind = kind;
  node->data = data;
  return node;
}

static inline collection_node_t *alloc_collection_node(uint32_t kind) {
  collection_node_t *node = kmalloc(sizeof(collection_node_t));
  memset(node, 0, sizeof(collection_node_t));
  node->type = COLLECTION_NODE;
  node->kind = kind;
  return node;
}

//

static inline void *find_last(void *node) {
  base_node_t *n = node;
  while (n->next != NULL) {
    n = n->next;
  }
  return n;
}

static inline void *link_node(void *nodes, void *node) {
  if (node == NULL) {
    return nodes;
  }

  if (nodes != NULL) {
    base_node_t *last = find_last(nodes);
    last->next = node;
    return nodes;
  }
  return node;
}

static inline void free_list(void *nodes) {
  base_node_t *n = as_base(nodes);
  while (n) {
    base_node_t *next = n->next;
    kfree(n);
    n = next;
  }
}

static inline void copy_global_state(item_node_t *item, parser_state_t *state) {
  item->usage_page = state->usage_page;
  item->logical_min = state->logical_min;
  item->logical_max = state->logical_max;
  item->physical_min = state->physical_min;
  item->physical_max = state->physical_max;
  item->report_size = state->report_size;
  item->report_id = state->report_id;
  item->report_count = state->report_count;
}

//

static void print_tag(uint8_t type, uint8_t tag, uint32_t data, parser_state_t *state, int indent) {
  if (type == TYPE_MAIN && tag < INPUT_TAG) {
    type = TYPE_GLOBAL;
  }
  const char *tag_name = tag_names[type][tag];
  if (tag_name == NULL) {
    tag_name = "UNKNOWN";
  }

  char buffer[32];
  memset(buffer, 0, 32);

  char spacing[12];
  memset(spacing, 0, 12);
  for (int i = 0; i < indent; i++) {
    spacing[i] = ' ';
  }

  switch (type) {
    case TYPE_MAIN:
      switch (tag) {
        case INPUT_TAG:
        case OUTPUT_TAG:
        case FEATURE_TAG:
          ksprintf(buffer, "(%#x)", data);
          break;
        case COLLECTION_TAG:
          ksprintf(buffer, "(%s)", collection_names[data]);
          break;
        default:
          break;
      }
      break;
    case TYPE_GLOBAL: {
      switch (tag) {
        case USAGE_PAGE_TAG:
          ksprintf(buffer, "(%s)", hid_get_usage_page_name(data));
          break;
        case LOGICAL_MINIMUM_TAG:
        case LOGICAL_MAXIMUM_TAG:
        case PHYSICAL_MINIMUM_TAG:
        case PHYSICAL_MAXIMUM_TAG:
        case REPORT_SIZE_TAG:
        case REPORT_COUNT_TAG:
          ksprintf(buffer, "(%d)", data);
          break;
        case UNIT_EXPONENT_TAG:
        case UNIT_TAG:
          ksprintf(buffer, "(Not Supported)");
          break;
        default:
          break;
      }
      break;
    }
    case TYPE_LOCAL: {
      switch (tag) {
        case USAGE_TAG:
          if (state->usage_page == 0) {
            ksprintf(buffer, "Unknown");
          } else {
            ksprintf(buffer, "(%s)", hid_get_usage_name(state->usage_page, data));
          }
          break;
        case USAGE_MINIMUM_TAG:
        case USAGE_MAXIMUM_TAG:
          ksprintf(buffer, "(%d)", data);
          break;
        default:
          ksprintf(buffer, "(Not Supported)");
          break;
      }
      break;
    }
    default:
      break;
  }

  kprintf("%s%s %s\n", spacing, tag_name, buffer);
}

static void print_node(void *node, int indent) {
  int buflen = PARSER_STACK * 2 + 1;
  char spacing[buflen];
  memset(spacing, 0, buflen);
  for (int i = 0; i < indent; i++) {
    spacing[i] = ' ';
  }

  base_node_t *base = as_base(node);
  if (base->type == COLLECTION_NODE) {
    collection_node_t *n = node;
    if (n->kind != COLLECTION_ROOT) {
      if (n->usage_page > 0) {
        kprintf("%sUSAGE PAGE (%s)\n", spacing, hid_get_usage_page_name(n->usage_page));
        if (n->usage > 0) {
          kprintf("%sUSAGE (%s)\n", spacing, hid_get_usage_name(n->usage_page, n->usage));
        }
      }
      kprintf("%sCOLLECTION %s\n", spacing, collection_names[n->kind]);
    }

    if (n->children != NULL) {
      print_node(n->children, indent + (n->kind == COLLECTION_ROOT ? 0 : 2));
    }
  } else if (base->type == ITEM_NODE) {
    item_node_t *n = node;
    if (n->usage_page > 0) {
      kprintf("%sUSAGE PAGE (%s)\n", spacing, hid_get_usage_page_name(n->usage_page));
    }
    kprintf("%sLOGICAL MIN (%d)\n", spacing, n->logical_min);
    kprintf("%sLOGICAL MAX (%d)\n", spacing, n->logical_max);
    kprintf("%sPHYSICAL MIN (%d)\n", spacing, n->physical_min);
    kprintf("%sPHYSICAL MAX (%d)\n", spacing, n->physical_max);
    kprintf("%sREPORT SIZE (%d)\n", spacing, n->report_size);
    kprintf("%sREPORT COUNT (%d)\n", spacing, n->report_count);

    usage_node_t *usage = n->usages;
    while (usage != NULL) {
      if (usage->usage > 0) {
        kprintf("%sUSAGE (%s)\n", spacing, hid_get_usage_name(n->usage_page, usage->usage));
      } else {
        kprintf("%sUSAGE MIN (%#x)\n", spacing, usage->usage_min);
        kprintf("%sUSAGE MAX (%#x)\n", spacing, usage->usage_max);
      }
      usage = (void *) usage->next;
    }

    kprintf("%s%s %#x\n\n", spacing, tag_names[TYPE_MAIN][n->kind], n->data);
  } else {
    kprintf("invalid node type %d\n", base->type);
    return;
  }

  if (base->next != NULL) {
    print_node(base->next, indent);
  }
}

//

report_format_t *hid_parse_report_descriptor(uint8_t *desc, size_t length) {
  parser_state_t stack[PARSER_STACK];
  parser_state_t *state = &stack[0];
  memset(stack, 0, sizeof(stack));
  int indent = 0;

  collection_node_t *root = alloc_collection_node(COLLECTION_ROOT);
  collection_node_t *collections = root;
  collection_node_t *collection = NULL;
  item_node_t *items = NULL;
  item_node_t *item = NULL;
  usage_node_t *usages = NULL;
  usage_node_t *usage = NULL;

  uint8_t *ptr = desc;
  uint8_t *ptr_max = desc + length;

  uint16_t bits_offset = 0;
  uint8_t report_size = 0;

  hid_trace_debug("parsing report descriptor");
  hid_trace_debug("report descriptor:");
  while (ptr < ptr_max) {
    uint8_t value = *ptr++;
    uint8_t tag = PREFIX_TAG(value);
    uint8_t type = PREFIX_TYPE(value);
    uint8_t size = PREFIX_SIZE(value);
    if (size == 3) {
      size = 4;
    }

    // check if long item
    if (value == 0xFE) {
      EXPECT_COUNT(2);
      size = *ptr++;
      tag = *ptr++;

      // skip bytes
      while (size > 0) {
        if (ptr >= ptr_max) {
          kprintf("[hid] invalid long item size\n");
          continue;
        }
      }

      // do something with long item
      kprintf("[hid] long items are not supported\n");
      continue;
    }

    EXPECT_COUNT(size, kprintf("[hid] invalid item size\n"));
    uint32_t data = 0;
    for (int i = 0; i < size; i++) {
      data |= ((*ptr++) << (i * sizeof(uint8_t)));
    }

    if (type == TYPE_MAIN && tag < INPUT_TAG) {
      type = TYPE_GLOBAL;
    }

    // super hacky way to fix indentation of END_COLLECTION tags
    if (type == TYPE_MAIN && tag == END_COLLECTION_TAG) {
      print_tag(type, tag, data, state, max(indent - 2, 0));
    } else {
      print_tag(type, tag, data, state, indent);
    }

    switch (type) {
      case TYPE_MAIN:
        switch (tag) {
          case INPUT_TAG:
          case OUTPUT_TAG:
          case FEATURE_TAG:
            item = alloc_item_node(tag, data);
            copy_global_state(item, state);
            LL_ADD(item->usages, usages);
            LL_ADD(items, item);
            item = NULL;
            usage = NULL;
            usages = NULL;
            if (state->report_size * state->report_count % 8 == 0 && data & 1) {
              // constant
              break;
            }

            bits_offset += state->report_size * state->report_count;
            if (bits_offset % 8 == 0) {
              report_size += bits_offset / 8;
              bits_offset = 0;
            }
            break;
          case COLLECTION_TAG:
            state->collection = collections;
            LL_ADD(collections->children, items);
            collection = alloc_collection_node(data);
            if (usages != NULL) {
              usage = find_last(usages);
              collection->usage = usage->usage;
              collection->usage_page = state->usage_page;
              free_list(usages);
            }
            LL_ADD(collections->children, collection);
            collections = collection;
            collection = NULL;
            usages = NULL;
            usage = NULL;
            state++;
            indent += 2;
            *state = *(state - 1);
            state->collection = NULL;
            break;
          case END_COLLECTION_TAG:
            LL_ADD(collections->children, items);
            memset(state, 0, sizeof(parser_state_t));
            state--;
            indent -= 2;
            collections = state->collection;
            collection = NULL;
            items = NULL;
            item = NULL;
            usages = NULL;
            usage = NULL;
            break;
          default:
            kprintf("[hid] invalid type/tag %#x %#x\n", type, tag);
            break;
        }
        break;
      case TYPE_GLOBAL:
        switch (tag) {
          case USAGE_PAGE_TAG:
            state->usage_page = data;
            break;
          case LOGICAL_MINIMUM_TAG:
            state->logical_min = data;
            break;
          case LOGICAL_MAXIMUM_TAG:
            state->logical_max = data;
            break;
          case PHYSICAL_MINIMUM_TAG:
            state->physical_min = data;
            break;
          case PHYSICAL_MAXIMUM_TAG:
            state->physical_max = data;
            break;
          case UNIT_EXPONENT_TAG:
          case UNIT_TAG:
            kprintf("[hid] tag not supported\n");
            break;
          case REPORT_SIZE_TAG:
            state->report_size = data;
            break;
          case REPORT_ID_TAG:
            state->report_id = data;
            break;
          case REPORT_COUNT_TAG:
            state->report_count = data;
            break;
          case PUSH_TAG:
          case POP_TAG:
            kprintf("[hid] tag not supported\n");
            break;
          default:
            kprintf("[hid] invalid type/tag %#x %#x\n", type, tag);
            break;
        }
        break;
      case TYPE_LOCAL:
        switch (tag) {
          case USAGE_TAG:
            usage = alloc_usage_node(data);
            LL_ADD(usages, usage);
            usage = NULL;
            break;
          case USAGE_MINIMUM_TAG:
            usage = alloc_usage_node(0);
            usage->usage_min = data;
            LL_ADD(usages, usage);
            break;
          case USAGE_MAXIMUM_TAG:
            usage->usage_max = data;
            usage = NULL;
            break;
          case DESIGNATOR_INDEX_TAG:
          case DESIGNATOR_MINIMUM_TAG:
          case DESIGNATOR_MAXIMUM_TAG:
          case STRING_INDEX_TAG:
          case STRING_MINIMUM_TAG:
          case STRING_MAXIMUM_TAG:
            kprintf("[hid] unsupported tag %#x\n", tag);
            break;
          default:
            kprintf("[hid] invalid type/tag %#x %#x\n", type, tag);
            break;
        }
        break;
      default:
        kprintf("[hid] invalid tag %#x\n", tag);
        break;
    }
  }

  hid_trace_debug("descriptor tree:");
  print_node(root, 0);

  report_format_t *format = kmalloc(sizeof(report_format_t));
  format->root = root;
  format->size = report_size;
  return format;
}

bool is_usage(item_node_t *item, uint32_t usage_page, uint32_t usage) {
  return is_usage_range(item, usage_page, usage, usage);
}

bool is_usage_range(item_node_t *item, uint32_t usage_page, uint32_t usage_min, uint32_t usage_max) {
  if (item == NULL || item->usage_page != usage_page || item->usages == NULL) {
    return false;
  }

  usage_node_t *node = item->usages;
  while (node != NULL) {
    if ((usage_min == usage_max && node->usage == usage_min) ||
        (node->usage_min <= usage_min && node->usage_max >= usage_max)) {
      return true;
    }
    node = (void *) node->next;
  }
  return false;
}

int get_item_size_bits(item_node_t *node) {
  int bits = node->report_size * node->report_count;
  if (bits % 8 == 0 && node->data & 1) {
    return 0;
  }
  return bits;
}
