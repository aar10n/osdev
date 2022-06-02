//
// Created by Aaron Gill-Braun on 2022-05-29.
//

#ifndef BOOT_COMMON_H
#define BOOT_COMMON_H

#include <Base.h>
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <ProcessorBind.h>
#include <stdint.h>

#define EFIMAIN EFIAPI __attribute((used))

#define CHECK_STATUS(Status) \
  if (EFI_ERROR(Status)){ \
    ErrorPrint(L"[Loader] Error code %d\n", Status); \
    return Status; \
  } \
  NULL

// [Loader] INFO:     Loading x
// [Loader] ERROR:    Failed to load x
// [Loader] WARNING:  Failed to load y
// [Loader] Status: EFI_SUCCESS

#define PRINT_INFO(string, ...) Print(L"[Loader] INFO:     " L##string L"\n", ##__VA_ARGS__)
#define PRINT_WARN(string, ...) ErrorPrint(L"[Loader] WARNING:  " L##string L"\n", ##__VA_ARGS__)
#define PRINT_ERROR(string, ...) ErrorPrint(L"[Loader] ERROR:    " L##string L"\n", ##__VA_ARGS__)
#define PRINT_STATUS(status) ErrorPrint(L"[Loader] Status: %r\n", status)

#endif
