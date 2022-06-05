//
// Created by Aaron Gill-Braun on 2022-06-03.
//

#ifndef BOOT_SYSTEM_H
#define BOOT_SYSTEM_H

#include <Common.h>

EFI_STATUS EFIAPI LocateSystemACPITable(OUT VOID **Pointer);
EFI_STATUS EFIAPI LocateSystemSMBIOSTable(OUT VOID **Pointer);

EFI_STATUS EFIAPI PrintDebugAcpiTables();

#endif
