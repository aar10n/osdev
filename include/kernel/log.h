#ifndef KERNEL_LOG_H
#define KERNEL_LOG_H

#include <kernel/printf.h>

// LOG_TAG must be defined before including this header.
// Example: #define LOG_TAG tcp

#define _LOG_STR(x) #x
#define _LOG_XSTR(x) _LOG_STR(x)
#define _LOG_CONCAT(a, b) a##b
#define _LOG_CHECK(tag) _LOG_CONCAT(LOG_ENABLED_, tag)
#define _LOG_TAG_HEADER(tag) _LOG_STR(log_tags/tag.h)

#include _LOG_TAG_HEADER(LOG_TAG)

#if defined(LOG_ENABLE_ALL) || _LOG_CHECK(LOG_TAG)
#define DPRINTF(fmt, ...) kprintf(_LOG_XSTR(LOG_TAG) ": " fmt, ##__VA_ARGS__)
#define DPRINTF_FUNC(fmt, ...) kprintf(_LOG_XSTR(LOG_TAG) ": %s: " fmt, __func__, ##__VA_ARGS__)
#else
#define DPRINTF(fmt, ...)
#define DPRINTF_FUNC(fmt, ...)
#endif

#endif
