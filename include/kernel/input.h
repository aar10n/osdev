//
// Created by Aaron Gill-Braun on 2022-12-09.
//

#ifndef KERNEL_INPUT_H
#define KERNEL_INPUT_H

#include <kernel/base.h>
#include <kernel/chan.h>
#include <abi/ioctl.h>

#include <uapi/osdev/input.h>
#include <uapi/osdev/input-event-codes.h>


//
// event flags
//

// key events

// mouse events
#define MOUSE_EV_REL (1 << 0)   // mouse event is relative
#define MOUSE_EV_ABS (1 << 1)   // mouse event is absolute

//
// event values
//



//

typedef union input_key_event {
  struct {
    uint16_t key;
    uint8_t modifiers;
  };
  // to make it easy to send/receive thru chan
  uint64_t raw;
} input_key_event_t;
static_assert(sizeof(input_key_event_t) == sizeof(uint64_t));

#define MOD_CTRL   (1 << 0)
#define MOD_SHIFT  (1 << 1)
#define MOD_ALT    (1 << 2)
#define MOD_META   (1 << 3)
#define MOD_CAPS   (1 << 4)


/**
 * Called by input device drivers to notify the kernel of an event.
 *
 * @param type The type of event (EV_<type> value)
 * @param code Flags for the given event type (<type>_EV_ bitmask)
 * @param value The event payload (use the <type>_VALUE function)
 * @return
 */
int input_event(uint16_t type, uint16_t code, int32_t value);

/**
 * Returns the current state of the given key.
 * @param key The key code
 * @return
 */
int input_getkey(uint16_t key);

int input_key_event_to_char(input_key_event_t *event);

#endif
