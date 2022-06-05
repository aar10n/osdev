//
// Created by Aaron Gill-Braun on 2022-06-03.
//

#include <System.h>

#include <IndustryStandard/Acpi.h>

#include <Guid/Acpi.h>
#include <Guid/SmBios.h>

#define OFFSET_PTR(ptr, offset) ((VOID *)((UINTN)(ptr) + (offset)))

typedef struct __attribute__((packed)) _DEBUG_DEVICE_INFORMATION {
  UINT8 Revision;
  UINT16 Length;
  UINT8 NumberOfGenericAddressRegisters;
  UINT16 NameSpaceStringLength;
  UINT16 NameSpaceStringOffset;
  UINT16 OemDataLength;
  UINT16 OemDataOffset;
  UINT16 PortType;
  UINT16 PortSubtype;
  UINT16 Reserved;
  UINT16 BaseAddressRegisterOffset;
  UINT16 AddressSizeOffset;
} DEBUG_DEVICE_INFORMATION;

typedef struct __attribute__((packed)) _EFI_ACPI_DEBUG_PORT_2_TABLE {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32 OffsetDbgDeviceInfo;
  UINT32 NumberDbgDeviceInfo;
} EFI_ACPI_DEBUG_PORT_2_TABLE;


VOID EFIAPI *MakeNullTerminatedStr(CHAR8 *Destination, VOID *Source, UINTN Length) {
  for (UINTN Index = 0; Index < Length; Index++) {
    Destination[Index] = ((CHAR8 *)Source)[Index];
  }
  Destination[Length] = '\0';
  return Destination;
}

EFI_STATUS EFIAPI LocateSystemACPITable(OUT VOID **Pointer) {
  if (Pointer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  EFI_GUID AcpiTableGuid = EFI_ACPI_20_TABLE_GUID;
  return EfiGetSystemConfigurationTable(&AcpiTableGuid, Pointer);
}

EFI_STATUS EFIAPI LocateSystemSMBIOSTable(OUT VOID **Pointer) {
  if (Pointer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  EFI_GUID SmbiosTableGuid = SMBIOS3_TABLE_GUID;
  return EfiGetSystemConfigurationTable(&SmbiosTableGuid, Pointer);
}

//

void EFIAPI PrintAcpiGenericAddress(IN EFI_ACPI_2_0_GENERIC_ADDRESS_STRUCTURE *GenericAddress) {
  PRINT_INFO("  AddressSpaceId: 0x%p", GenericAddress->AddressSpaceId);
  PRINT_INFO("  RegisterBitWidth: %d", GenericAddress->RegisterBitWidth);
  PRINT_INFO("  RegisterBitOffset: %d", GenericAddress->RegisterBitOffset);
  PRINT_INFO("  Address: 0x%p", GenericAddress->Address);
}

VOID EFIAPI PrintAcpiCommonHeader(IN EFI_ACPI_COMMON_HEADER *Header) {
  CHAR8 Buffer[64];
  PRINT_INFO("  Signature: %a | Length: %d", MakeNullTerminatedStr(Buffer, &Header->Signature, 4), Header->Length);
}

VOID EFIAPI PrintAcpiDescriptionHeader(IN EFI_ACPI_DESCRIPTION_HEADER *Header) {
  CHAR8 Buffer[64];
  PRINT_INFO("  Signature: %a", MakeNullTerminatedStr(Buffer, &Header->Signature, 4));
  PRINT_INFO("  Length: %d", Header->Length);
  PRINT_INFO("  Revision: %d", Header->Revision);
  PRINT_INFO("  Checksum: 0x%x", Header->Checksum);
  PRINT_INFO("  OemId: %a", MakeNullTerminatedStr(Buffer, &Header->OemId, 6));
  PRINT_INFO("  OemTableId: %a", MakeNullTerminatedStr(Buffer, &Header->OemTableId, 8));
  PRINT_INFO("  OemRevision: %d", Header->OemRevision);
  PRINT_INFO("  CreatorId: 0x%x", Header->CreatorId);
  PRINT_INFO("  CreatorRevision: %d", Header->CreatorRevision);
}

VOID EFIAPI PrintDebugDeviceInformation(IN DEBUG_DEVICE_INFORMATION *DeviceInfo) {
  // UINT8 Revision;
  // UINT16 Length;
  // UINT8 NumberOfGenericAddressRegisters;
  // UINT16 NameSpaceStringLength;
  // UINT16 NameSpaceStringOffset;
  // UINT16 OemDataLength;
  // UINT16 OemDataOffset;
  // UINT16 PortType;
  // UINT16 PortSubtype;
  // UINT16 Reserved;
  // UINT16 BaseAddressRegisterOffset;
  // UINT16 AddressSizeOffset;
  PRINT_INFO("  Revision: %d", DeviceInfo->Revision);
  PRINT_INFO("  Length: %d", DeviceInfo->Length);
  PRINT_INFO("  NumberOfGenericAddressRegisters: %d", DeviceInfo->NumberOfGenericAddressRegisters);
  PRINT_INFO("  NameSpaceStringLength: %d", DeviceInfo->NameSpaceStringLength);
  PRINT_INFO("  NameSpaceStringOffset: %d", DeviceInfo->NameSpaceStringOffset);
  PRINT_INFO("  OemDataLength: %d", DeviceInfo->OemDataLength);
  PRINT_INFO("  OemDataOffset: %d", DeviceInfo->OemDataOffset);
  PRINT_INFO("  PortType: 0x%p", DeviceInfo->PortType);
  PRINT_INFO("  PortSubtype: 0x%p", DeviceInfo->PortSubtype);
  PRINT_INFO("  BaseAddressRegisterOffset: %d", DeviceInfo->BaseAddressRegisterOffset);
  PRINT_INFO("  AddressSizeOffset: %d", DeviceInfo->AddressSizeOffset);
  PRINT_INFO("  NamespaceString: %a", OFFSET_PTR(DeviceInfo, DeviceInfo->NameSpaceStringOffset));

  if (DeviceInfo->NameSpaceStringOffset == 0) {
    PRINT_WARN("Bad DebugDeviceInformation");
    return;
  }

  PRINT_INFO("--- Base Address Registers ---");
  EFI_ACPI_2_0_GENERIC_ADDRESS_STRUCTURE *Addresses = OFFSET_PTR(DeviceInfo, DeviceInfo->BaseAddressRegisterOffset);
  for (UINTN Index = 0; Index < DeviceInfo->NumberOfGenericAddressRegisters; Index++) {
    PrintAcpiGenericAddress(Addresses + Index);
  }
  PRINT_INFO("--- Address Size Registers ---");
  UINT32 *AddressSizes = OFFSET_PTR(DeviceInfo, DeviceInfo->AddressSizeOffset);
  for (UINTN Index = 0; Index < DeviceInfo->NumberOfGenericAddressRegisters; Index++) {
    PRINT_INFO("  %d", AddressSizes[Index]);
  }
}

EFI_STATUS EFIAPI PrintDebugAcpiTables() {
  EFI_STATUS Status;
  EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER *Xdst;
  CHAR8 Buffer[64];

  Status = EfiGetSystemConfigurationTable(&gEfiAcpi20TableGuid, (VOID **)&Rsdp);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  // UINT8 Revision;
  // UINT16 Length;
  // UINT8 NumberOfGenericAddressRegisters;
  // UINT16 NameSpaceStringLength;
  // UINT16 NameSpaceStringOffset;
  // UINT16 OemDataLength;
  // UINT16 OemDataOffset;
  // UINT16 PortType;
  // UINT16 PortSubtype;
  // UINT16 Reserved;
  // UINT16 BaseAddressRegisterOffset;
  // UINT16 AddressSizeOffset;
  PRINT_INFO("DEBUG_DEVICE_INFORMATION:");
  PRINT_INFO("  Revision: %d", OFFSET_OF(DEBUG_DEVICE_INFORMATION, Revision));
  PRINT_INFO("  Length: %d", OFFSET_OF(DEBUG_DEVICE_INFORMATION, Length));
  PRINT_INFO("  NumberOfGenericAddressRegisters: %d", OFFSET_OF(DEBUG_DEVICE_INFORMATION, NumberOfGenericAddressRegisters));
  PRINT_INFO("  NameSpaceStringLength: %d", OFFSET_OF(DEBUG_DEVICE_INFORMATION, NameSpaceStringLength));
  PRINT_INFO("  NameSpaceStringOffset: %d", OFFSET_OF(DEBUG_DEVICE_INFORMATION, NameSpaceStringOffset));

  PRINT_INFO("RSDP:");
  PRINT_INFO("  Signature: %a", MakeNullTerminatedStr(Buffer, &Rsdp->Signature, 8));
  PRINT_INFO("  Checksum: %d", Rsdp->Checksum);
  PRINT_INFO("  OemId: %a", MakeNullTerminatedStr(Buffer, Rsdp->OemId, 6));
  PRINT_INFO("  Revision: %d", Rsdp->Revision);
  PRINT_INFO("  RsdtAddress: 0x%p", Rsdp->RsdtAddress);
  PRINT_INFO("  Length: %d", Rsdp->Length);
  PRINT_INFO("  XsdtAddress: 0x%p", Rsdp->XsdtAddress);
  PRINT_INFO("  ExtendedChecksum: %d", Rsdp->ExtendedChecksum);

  Xdst = (VOID *) Rsdp->XsdtAddress;
  PRINT_INFO("XDST:");
  PrintAcpiDescriptionHeader(Xdst);

  EFI_ACPI_COMMON_HEADER *Table;
  UINTN EntryCount = (Xdst->Length - sizeof(EFI_ACPI_DESCRIPTION_HEADER)) / sizeof(UINT64);
  UINT64 *Entries = (UINT64 *) ((UINT8 *) Xdst + sizeof(EFI_ACPI_DESCRIPTION_HEADER));
  for (UINTN Index = 0; Index < EntryCount; Index++) {
    Table = (VOID *) Entries[Index];
    PRINT_INFO("Table %d:", Index);
    PrintAcpiCommonHeader(Table);

    if (Table->Signature == SIGNATURE_32('D', 'B', 'G', '2')) {
      EFI_ACPI_DEBUG_PORT_2_TABLE *DebugPort2 = (VOID *) Table;
      PRINT_INFO("  OffsetDbgDeviceInfo: %d", DebugPort2->OffsetDbgDeviceInfo);
      PRINT_INFO("  NumberDbgDeviceInfo: %d\n", DebugPort2->NumberDbgDeviceInfo);

      DEBUG_DEVICE_INFORMATION *DeviceInfo = OFFSET_PTR(DebugPort2, DebugPort2->OffsetDbgDeviceInfo);
      for (UINTN DeviceIndex = 0; DeviceIndex < DebugPort2->NumberDbgDeviceInfo; DeviceIndex++) {
        PRINT_INFO("=> Device %d:", DeviceIndex);
        PrintDebugDeviceInformation(&DeviceInfo[DeviceIndex]);
      }
    }

    WaitForKeypress();
  }

  return EFI_SUCCESS;
}

