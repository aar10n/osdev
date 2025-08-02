//
// Created by Aaron Gill-Braun on 2025-07-30.
//

#ifndef INCLUDE_ABI_IOCTL_H
#define INCLUDE_ABI_IOCTL_H

#include <bits/ioctl.h>

#define _IOC_SIZE(nr)   (((nr) >> _IOC_SIZESHIFT) & _IOC_SIZEMASK)
#define _IOC_SIZEMASK   ((1 << _IOC_SIZEBITS)-1)
#define _IOC_SIZESHIFT  (_IOC_TYPESHIFT+_IOC_TYPEBITS)
#define _IOC_TYPESHIFT  (_IOC_NRSHIFT+_IOC_NRBITS)
#define _IOC_NRSHIFT    0

#define _IOC_SIZEBITS   14
#define _IOC_NRBITS     8
#define _IOC_TYPEBITS   8

#endif
