//
// Created by Aaron Gill-Braun on 2023-07-07.
//

#ifndef INCLUDE_KERNEL_TYPES_H
#define INCLUDE_KERNEL_TYPES_H

#define __NEED_int8_t
#define __NEED_int16_t
#define __NEED_int32_t
#define __NEED_int64_t
#define __NEED_intmax_t
#define __NEED_uint8_t
#define __NEED_uint16_t
#define __NEED_uint32_t
#define __NEED_uint64_t
#define __NEED_uintmax_t

#define __NEED_size_t
#define __NEED_uintptr_t
#define __NEED_ptrdiff_t
#define __NEED_ssize_t
#define __NEED_intptr_t
#define __NEED_regoff_t
#define __NEED_register_t
#define __NEED_time_t
#define __NEED_suseconds_t

#define __NEED_mode_t
#define __NEED_nlink_t
#define __NEED_off_t
#define __NEED_ino_t
#define __NEED_dev_t
#define __NEED_blksize_t
#define __NEED_blkcnt_t
#define __NEED_fsblkcnt_t
#define __NEED_fsfilcnt_t

#define __NEED_wint_t
#define __NEED_wctype_t

#define __NEED_timer_t
#define __NEED_clockid_t
#define __NEED_clock_t
#define __NEED_struct_timeval
#define __NEED_struct_timespec

#define __NEED_pid_t
#define __NEED_id_t
#define __NEED_uid_t
#define __NEED_gid_t
#define __NEED_key_t
#define __NEED_useconds_t

#define __NEED_pthread_t
#define __NEED_pthread_once_t
#define __NEED_pthread_key_t

#define __NEED_sigset_t
#define __NEED_struct_iovec
#define __NEED_socklen_t
#define __NEED_sa_family_t
#define __NEED_pthread_attr_t

#include <bits/alltypes.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint16_t char16_t;

#endif
