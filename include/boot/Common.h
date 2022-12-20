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
#include <Library/SerialPortLib.h>
#include <ProcessorBind.h>
#include <stdint.h>

#include <boot.h>

#define EFIMAIN EFIAPI __attribute((used))

#define PRE_EXIT_BOOT_SERVICES (PostExitBootServices == FALSE)
#define POST_EXIT_BOOT_SERVICES (PostExitBootServices == TRUE)

#define PRINT(string, ...) \
  ({                            \
    CHAR16 *String = CatSPrint(NULL, string, ##__VA_ARGS__); \
    UINTN StringLen = StrLen(String); \
    Print(L"%s", String); \
    CHAR8 *AsciiString = AllocatePool(StringLen + 1); \
    UnicodeStrToAsciiStrS(String, AsciiString, StringLen + 1); \
    /*SerialPortWrite((UINT8 *) AsciiString, StringLen);*/ \
    FreePool(String);           \
    FreePool(AsciiString);      \
  })

#define PRINT_INFO(string, ...) PRINT(L"[Loader] INFO:     " L##string L"\n", ##__VA_ARGS__)
#define PRINT_WARN(string, ...) PRINT(L"[Loader] WARN:     " L##string L"\n", ##__VA_ARGS__)
#define PRINT_ERROR(string, ...) PRINT(L"[Loader] ERROR:    " L##string L"\n", ##__VA_ARGS__)
#define PRINT_STATUS(status) PRINT(L"[Loader] Status: %r\n", status)

#define MMAP_MAX_SIZE SIZE_8KB

extern BOOLEAN PostExitBootServices;

static inline VOID WaitForKeypress() {
  gBS->WaitForEvent(1, &(gST->ConIn->WaitForKey), NULL);
  EFI_INPUT_KEY Key;
  gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
}

#endif
