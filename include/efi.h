//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#ifndef KERNEL_EFI_H
#define KERNEL_EFI_H

typedef uint32_t efi_status_t;

// Time

typedef struct {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t pad1;
  uint32_t nanosecond;
  int16_t time_zone;
  uint8_t daylight;
  uint8_t pad2;
} efi_time_t;

typedef efi_status_t (*efi_get_time_t)(efi_time_t *time);

#define EFI_TIME_ADJUST_DAYLIGHT 0x01
#define EFI_TIME_IN_DAYLIGHT 0x02

// Reset

typedef enum {
  EFI_RESET_COLD,
  EFI_RESET_WARM,
  EFI_RESET_SHUTDOWN,
  EFI_RESET_PLATFORM_SPECIFIC,
} efi_reset_type_t;

typedef void __cdecl (*efi_system_reset_t)(efi_reset_type_t reset_type);

// RuntimeServices

typedef struct {

} efi_runtime_services_t;

#endif
