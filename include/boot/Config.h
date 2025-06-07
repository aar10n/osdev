//
// Created by Aaron Gill-Braun on 2020-09-27.
//

#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include <Common.h>
#include <Memory.h>

#define INI_MAP_LEN 128
#define INI_MAX_KEY_LEN 32
#define INI_MAX_VALUE_LEN 256

typedef struct _INI_VARIABLE {
  CHAR8 *Key;
  CHAR8 *Value;
} INI_VARIABLE;


EFI_STATUS EFIAPI InitializeConfig();
CHAR8 EFIAPI *ConfigGet(CHAR8 *Key);
EFI_STATUS EFIAPI ConfigSet(CHAR8 *Key, CHAR8 *Value);

BOOLEAN EFIAPI ConfigGetBooleanD(CHAR8 *Key, BOOLEAN Default);
EFI_STATUS EFIAPI ConfigGetBooleanS(CHAR8 *Key, BOOLEAN *Value);
CHAR16 *EFIAPI ConfigGetStringD(CHAR8 *Key, CONST CHAR16 *Default);
EFI_STATUS EFIAPI ConfigGetStringS(CHAR8 *Key, CHAR16 **String);
UINT64 EFIAPI ConfigGetNumericD(CHAR8 *Key, UINT64 Default);
EFI_STATUS EFIAPI ConfigGetNumericS(CHAR8 *Key, UINT64 *Result);
UINT64 EFIAPI ConfigGetDecimalD(CHAR8 *Key, UINT64 Default);
EFI_STATUS EFIAPI ConfigGetDecimalS(CHAR8 *Key, UINT64 *Result);
UINT64 EFIAPI ConfigGetHexD(CHAR8 *Key, UINT64 Default);
EFI_STATUS EFIAPI ConfigGetHexS(CHAR8 *Key, UINT64 *Result);
EFI_STATUS EFIAPI ConfigGetDimensions(CHAR8 *Key, UINT32 *X, UINT32 *Y);
EFI_STATUS EFIAPI ConfigGetDuration(CHAR8 *Key, UINT64 *Result);

#endif
