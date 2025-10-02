//
// Created by Aaron Gill-Braun on 2025-10-01.
//

#ifndef BOOT_FWCFG_H
#define BOOT_FWCFG_H

#include <Base.h>

/**
 * FwCfgIsPresent - Check if QEMU fw_cfg is available
 *
 * Returns: TRUE if running under QEMU with fw_cfg, FALSE otherwise
 */
BOOLEAN FwCfgIsPresent();

/**
 * FwCfgReadCmdline - Read kernel command line from QEMU
 *
 * Returns: Pointer to NUL-terminated command line string, or NULL if not present.
 *          Caller should NOT free the returned pointer.
 */
CHAR8 *FwCfgReadCmdline();

#endif
