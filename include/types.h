//
// Created by Aaron Gill-Braun on 2020-10-28.
//

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef int64_t blkcnt_t;    // Used for file block counts.
typedef int64_t blksize_t;   // Used for block sizes.
typedef uint64_t clock_t;    // Used for system times in clock ticks or CLOCKS_PER_SEC
typedef uint32_t clockid_t;  // Used for clock ID type in the clock and timer functions.
typedef uint64_t dev_t;      // Used for device IDs.
typedef uint64_t fsblkcnt_t; // Used for file system block counts.
typedef uint64_t fsfilcnt_t; // Used for file system file counts.
typedef uint32_t gid_t;      // Used for group IDs.
typedef uint32_t id_t;       // Used as a general identifier.
typedef uint64_t ino_t;      // Used for file serial numbers.
typedef uint32_t key_t;      // Used for XSI interprocess communication.
typedef uint32_t mode_t;     // Used for some file attributes.
typedef uint16_t nlink_t;    // Used for link counts.
typedef int64_t off_t;       // Used for file sizes.
typedef int32_t pid_t;       // Used for process IDs and process group IDs.
/* pthread typedefs */
typedef uint64_t size_t;     // Used for sizes of objects.
typedef int64_t ssize_t;     // Used for a count of bytes or an error indication.
typedef int32_t suseconds_t; // Used for time in microseconds.
typedef uint32_t time_t;     // Used for time in seconds.
typedef uint32_t timer_t;    // Used for timer ID returned by timer_create().
typedef uint32_t uid_t;      // Used for user IDs.

#endif
