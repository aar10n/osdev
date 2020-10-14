//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#ifndef KERNEL_CPU_EXCEPTION_H
#define KERNEL_CPU_EXCEPTION_H

#include <base.h>
#include <cpu/cpu.h>

#define EXC_DE  0
#define EXC_DB  1
#define EXC_NMI 2
#define EXC_BP  3
#define EXC_OF  4
#define EXC_BR  5
#define EXC_UD  6
#define EXC_NM  7
#define EXC_DF  8
#define EXC_CSO 9
#define EXC_TS  10
#define EXC_NP  11
#define EXC_SS  12
#define EXC_GP  13
#define EXC_PF  14
#define EXC_MF  16
#define EXC_AC  17
#define EXC_MC  18
#define EXC_XM  19
#define EXC_VE  20
#define EXC_CP  21

#endif
