//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#include <mm/mm.h>

#define ZONE(typ) \
  [typ] = { .type = typ }


mem_zone_t zones[] = {
  ZONE(ZONE_LOW),
  ZONE(ZONE_DMA),
  ZONE(ZONE_NORMAL)
};

static const char *get_zone_name(zone_type_t zone_type) {
  switch (zone_type) {
    case ZONE_RESERVED:
      return "ZONE_RESERVED";
    case ZONE_LOW:
      return "ZONE_LOW";
    case ZONE_DMA:
      return "ZONE_DMA";
    case ZONE_NORMAL:
      return "ZONE_NORMAL";
    default:
      return "ZONE_UNKNOWN";
  }
}



