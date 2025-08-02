//
// Created by Aaron Gill-Braun on 2025-07-27.
//

#ifndef INCLUDE_UAPI_INPUT_H
#define INCLUDE_UAPI_INPUT_H

#define __NEED_struct_timeval
#define __NEED_time_t
#define __NEED_suseconds_t
#include <bits/alltypes.h>
#undef __NEED_struct_timeval
#undef __NEED_time_t
#undef __NEED_suseconds_t
#include <bits/ioctl.h>
#include <stdint.h>

struct input_event {
  struct timeval time;  // timestamp (16 bytes)
  uint16_t type;        // event type
  uint16_t code;        // event code
  int32_t value;        // event value
};

#define EVIOCGVERSION		_IOR('E', 0x01, int)			/* get driver version */
#define EVIOCGID		_IOR('E', 0x02, struct input_id)	/* get device ID */

#define EVIOCGREP		_IOR('E', 0x03, unsigned int[2])	/* get repeat settings */
#define EVIOCSREP		_IOW('E', 0x03, unsigned int[2])	/* set repeat settings */

#define EVIOCGKEYCODE		_IOR('E', 0x04, unsigned int[2])        /* get keycode */
#define EVIOCSKEYCODE		_IOW('E', 0x04, unsigned int[2])        /* set keycode */
#define EVIOCSKEYCODE_V2	_IOW('E', 0x04, struct input_keymap_entry)

#define EVIOCGNAME(len)		_IOC(_IOC_READ, 'E', 0x06, len)		/* get device name */
#define EVIOCGPHYS(len)		_IOC(_IOC_READ, 'E', 0x07, len)		/* get physical location */
#define EVIOCGUNIQ(len)		_IOC(_IOC_READ, 'E', 0x08, len)		/* get unique identifier */
#define EVIOCGPROP(len)		_IOC(_IOC_READ, 'E', 0x09, len)		/* get device properties */

#define EVIOCGKEY(len)		_IOC(_IOC_READ, 'E', 0x18, len)		/* get global key state */
#define EVIOCGLED(len)		_IOC(_IOC_READ, 'E', 0x19, len)		/* get all LEDs */
#define EVIOCGSND(len)		_IOC(_IOC_READ, 'E', 0x1a, len)		/* get all sounds status */
#define EVIOCGSW(len)		_IOC(_IOC_READ, 'E', 0x1b, len)		/* get all switch states */

// Helper Macros

#define BITS_TO_LONGS(x) (((x) + (sizeof(long) * 8 - 1)) / (sizeof(long) * 8))

#endif
