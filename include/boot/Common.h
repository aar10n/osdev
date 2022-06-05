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
#include <Library/IoLib.h>
#include <ProcessorBind.h>
#include <stdint.h>

#include <boot.h>

#define EFIMAIN EFIAPI __attribute((used))

#define PRE_EXIT_BOOT_SERVICES (PostExitBootServices == FALSE)
#define POST_EXIT_BOOT_SERVICES (PostExitBootServices == TRUE)

#define PRINT_INFO(string, ...) Print(L"[Loader] INFO:     " L##string L"\n", ##__VA_ARGS__)
#define PRINT_WARN(string, ...) ErrorPrint(L"[Loader] WARNING:  " L##string L"\n", ##__VA_ARGS__)
#define PRINT_ERROR(string, ...) ErrorPrint(L"[Loader] ERROR:    " L##string L"\n", ##__VA_ARGS__)
#define PRINT_STATUS(status) ErrorPrint(L"[Loader] Status: %r\n", status)

#define MMAP_MAX_SIZE SIZE_8KB

extern BOOLEAN PostExitBootServices;

static inline VOID WaitForKeypress() {
  gBS->WaitForEvent(1, &(gST->ConIn->WaitForKey), NULL);
  EFI_INPUT_KEY Key;
  gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
}

#endif
