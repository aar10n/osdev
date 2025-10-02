//
// Created by Aaron Gill-Braun on 2025-10-01.
//

#include <Common.h>
#include <FwCfg.h>

#define FW_CFG_PORT_SEL     0x510
#define FW_CFG_PORT_DATA    0x511

#define FW_CFG_SIGNATURE    0x00
#define FW_CFG_CMDLINE_SIZE 0x14
#define FW_CFG_CMDLINE_DATA 0x15

STATIC VOID FwCfgSelect(UINT16 Entry) {
  IoWrite16(FW_CFG_PORT_SEL, Entry);
}

STATIC VOID FwCfgReadBytes(VOID *Buffer, UINTN Size) {
  UINT8 *Ptr = (UINT8 *)Buffer;
  for (UINTN i = 0; i < Size; i++) {
    Ptr[i] = IoRead8(FW_CFG_PORT_DATA);
  }
}

BOOLEAN FwCfgIsPresent() {
  UINT8 Signature[4];

  FwCfgSelect(FW_CFG_SIGNATURE);
  FwCfgReadBytes(Signature, 4);

  return (Signature[0] == 'Q' &&
          Signature[1] == 'E' &&
          Signature[2] == 'M' &&
          Signature[3] == 'U');
}

CHAR8 *FwCfgReadCmdline() {
  UINT32 Size;
  CHAR8 *Cmdline;

  if (!FwCfgIsPresent()) {
    return NULL;
  }

  FwCfgSelect(FW_CFG_CMDLINE_SIZE);
  FwCfgReadBytes(&Size, sizeof(Size));

  if (Size == 0 || Size > 4096) {
    return NULL;
  }

  Cmdline = AllocateRuntimePool(Size);
  if (Cmdline == NULL) {
    return NULL;
  }

  FwCfgSelect(FW_CFG_CMDLINE_DATA);
  FwCfgReadBytes(Cmdline, Size);

  return Cmdline;
}
