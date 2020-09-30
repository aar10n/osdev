//
// Created by Aaron Gill-Braun on 2020-09-27.
//

#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#define INI_MAP_LEN 128
#define INI_MAX_KEY_LEN 32
#define INI_MAX_VALUE_LEN 128

typedef struct _INI_VARIABLE {
  CHAR8 *Key;
  CHAR8 *Value;
} INI_VARIABLE;

typedef INT8 CONFIG_RETURN;

EFI_STATUS EFIAPI ConfigParse(void *Buffer, UINTN BufferSize);
CHAR8 EFIAPI *ConfigGet(CHAR8 *Key);
EFI_STATUS EFIAPI ConfigSet(CHAR8 *Key, CHAR8 *Value);

UINT64 EFIAPI ConfigGetNumeric(CHAR8 *Key);
EFI_STATUS EFIAPI ConfigGetNumericS(CHAR8 *Key, UINT64 *Result);
UINT64 EFIAPI ConfigGetDecimal(CHAR8 *Key);
EFI_STATUS EFIAPI ConfigGetDecimalS(CHAR8 *Key, UINT64 *Result);
UINT64 EFIAPI ConfigGetHex(CHAR8 *Key);
EFI_STATUS EFIAPI ConfigGetHexS(CHAR8 *Key, UINT64 *Result);
EFI_STATUS EFIAPI ConfigGetDimensions(CHAR8 *Key, UINT32 *X, UINT32 *Y);

#endif
