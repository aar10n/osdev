//
// Created by Aaron Gill-Braun on 2025-08-02.
//

#include <kernel/device.h>
#include <kernel/input.h>
#include <kernel/irq.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#include <uapi/osdev/input-event-codes.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("keyboard: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("keyboard: %s: " fmt, __func__, ##__VA_ARGS__)

// AT keyboard controller ports
#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64
#define KEYBOARD_COMMAND_PORT 0x64

// keyboard controller status register bits
#define KBD_STATUS_OUTPUT_FULL  0x01
#define KBD_STATUS_INPUT_FULL   0x02
#define KBD_STATUS_SYSTEM       0x04
#define KBD_STATUS_COMMAND      0x08
#define KBD_STATUS_ENABLED      0x10
#define KBD_STATUS_MOUSE_DATA   0x20
#define KBD_STATUS_TIMEOUT      0x40
#define KBD_STATUS_PARITY       0x80

// keyboard commands
#define KBD_CMD_SET_LEDS        0xED
#define KBD_CMD_ECHO            0xEE
#define KBD_CMD_SET_SCANCODE    0xF0
#define KBD_CMD_IDENTIFY        0xF2
#define KBD_CMD_SET_RATE        0xF3
#define KBD_CMD_ENABLE          0xF4
#define KBD_CMD_DISABLE         0xF5
#define KBD_CMD_RESET           0xFF

// keyboard responses
#define KBD_RESP_ACK            0xFA
#define KBD_RESP_RESEND         0xFE
#define KBD_RESP_ERROR          0xFF

// LED bits
#define KBD_LED_SCROLL_LOCK     0x01
#define KBD_LED_NUM_LOCK        0x02
#define KBD_LED_CAPS_LOCK       0x04

// keyboard irq number (standard pc irq 1)
#define KEYBOARD_IRQ 1

// special scan codes
#define SCANCODE_EXTENDED     0xE0
#define SCANCODE_RELEASE_MASK 0x80

// scan code translation table (PS/2 scan code set 1 to key codes)
static uint16_t scancode_to_keycode[256] = {
  [0x01] = KEY_ESCAPE,
  [0x02] = KEY_1,
  [0x03] = KEY_2,
  [0x04] = KEY_3,
  [0x05] = KEY_4,
  [0x06] = KEY_5,
  [0x07] = KEY_6,
  [0x08] = KEY_7,
  [0x09] = KEY_8,
  [0x0A] = KEY_9,
  [0x0B] = KEY_0,
  [0x0C] = KEY_MINUS,
  [0x0D] = KEY_EQUAL,
  [0x0E] = KEY_BACKSPACE,
  [0x0F] = KEY_TAB,
  [0x10] = KEY_Q,
  [0x11] = KEY_W,
  [0x12] = KEY_E,
  [0x13] = KEY_R,
  [0x14] = KEY_T,
  [0x15] = KEY_Y,
  [0x16] = KEY_U,
  [0x17] = KEY_I,
  [0x18] = KEY_O,
  [0x19] = KEY_P,
  [0x1A] = KEY_LSQUARE,
  [0x1B] = KEY_RSQUARE,
  [0x1C] = KEY_ENTER,
  [0x1D] = KEY_LCTRL,
  [0x1E] = KEY_A,
  [0x1F] = KEY_S,
  [0x20] = KEY_D,
  [0x21] = KEY_F,
  [0x22] = KEY_G,
  [0x23] = KEY_H,
  [0x24] = KEY_J,
  [0x25] = KEY_K,
  [0x26] = KEY_L,
  [0x27] = KEY_SEMICOLON,
  [0x28] = KEY_APOSTROPHE,
  [0x29] = KEY_GRAVE,
  [0x2A] = KEY_LSHIFT,
  [0x2B] = KEY_BACKSLASH,
  [0x2C] = KEY_Z,
  [0x2D] = KEY_X,
  [0x2E] = KEY_C,
  [0x2F] = KEY_V,
  [0x30] = KEY_B,
  [0x31] = KEY_N,
  [0x32] = KEY_M,
  [0x33] = KEY_COMMA,
  [0x34] = KEY_PERIOD,
  [0x35] = KEY_SLASH,
  [0x36] = KEY_RSHIFT,
  [0x37] = KEY_KP_ASTERISK, // keypad *
  [0x38] = KEY_LALT,
  [0x39] = KEY_SPACE,
  [0x3A] = KEY_CAPSLOCK,
  [0x3B] = KEY_F1,
  [0x3C] = KEY_F2,
  [0x3D] = KEY_F3,
  [0x3E] = KEY_F4,
  [0x3F] = KEY_F5,
  [0x40] = KEY_F6,
  [0x41] = KEY_F7,
  [0x42] = KEY_F8,
  [0x43] = KEY_F9,
  [0x44] = KEY_F10,
  [0x45] = KEY_NUM_LOCK,
  [0x46] = KEY_SCROLL_LOCK,
  [0x47] = KEY_KP_7,      // keypad 7
  [0x48] = KEY_KP_8,      // keypad 8
  [0x49] = KEY_KP_9,      // keypad 9
  [0x4A] = KEY_KP_MINUS,  // keypad minus
  [0x4B] = KEY_KP_4,      // keypad 4
  [0x4C] = KEY_KP_5,      // keypad 5
  [0x4D] = KEY_KP_6,      // keypad 6
  [0x4E] = KEY_KP_PLUS,   // keypad plus
  [0x4F] = KEY_KP_1,      // keypad 1
  [0x50] = KEY_KP_2,      // keypad 2
  [0x51] = KEY_KP_3,      // keypad 3
  [0x52] = KEY_KP_0,      // keypad 0
  [0x53] = KEY_KP_PERIOD, // keypad decimal
  [0x57] = KEY_F11,
  [0x58] = KEY_F12,
};

// extended scan codes (prefixed with 0xE0)
static uint16_t extended_scancode_to_keycode[256] = {
  [0x1C] = KEY_KP_ENTER,  // keypad enter
  [0x1D] = KEY_RCTRL,     // right control
  [0x35] = KEY_KP_SLASH,  // keypad /
  [0x37] = KEY_PRINTSCR,  // print screen
  [0x38] = KEY_RALT,      // right alt
  [0x47] = KEY_HOME,      // home (not keypad)
  [0x48] = KEY_UP,        // up arrow (not keypad)
  [0x49] = KEY_PAGE_UP,   // page up (not keypad)
  [0x4B] = KEY_LEFT,      // left arrow (not keypad)
  [0x4D] = KEY_RIGHT,     // right arrow (not keypad)
  [0x4F] = KEY_END,       // end (not keypad)
  [0x50] = KEY_DOWN,      // down arrow (not keypad)
  [0x51] = KEY_PAGE_DOWN, // page down (not keypad)
  [0x52] = KEY_INSERT,    // insert (not keypad)
  [0x53] = KEY_DELETE,    // delete (not keypad)
  [0x5B] = KEY_LMETA,     // left windows key
  [0x5C] = KEY_RMETA,     // right windows key
};

static inline uint8_t io_inb(uint16_t port) {
  uint8_t value;
  asm volatile("in al, dx" : "=a"(value) : "Nd"(port));
  return value;
}

static inline void io_outb(uint16_t port, uint8_t value) {
  asm volatile("out dx, al" : : "a"(value), "Nd"(port));
}

// keyboard state
static bool extended_scancode = false;
static bool caps_lock_state = false;
static bool num_lock_state = true;   // typically starts enabled
static bool scroll_lock_state = false;

static void keyboard_irq_handler(struct trapframe *frame);
static void keyboard_send_command(uint8_t command);
static void keyboard_update_leds(void);

static void keyboard_static_init() {
  // reserve and register keyboard irq
  irq_must_reserve_irqnum(KEYBOARD_IRQ);
  irq_register_handler(KEYBOARD_IRQ, keyboard_irq_handler, NULL);
  irq_enable_interrupt(KEYBOARD_IRQ);
  
  // initialize LEDs to reflect initial state
  keyboard_update_leds();
  
  DPRINTF("AT keyboard driver initialized\n");
}
STATIC_INIT(keyboard_static_init);

static uint8_t keyboard_read_data() {
  while (!(io_inb(KEYBOARD_STATUS_PORT) & KBD_STATUS_OUTPUT_FULL)) {
    // wait for data to be available
  }
  return io_inb(KEYBOARD_DATA_PORT);
}

static void keyboard_wait_input_ready() {
  while (io_inb(KEYBOARD_STATUS_PORT) & KBD_STATUS_INPUT_FULL) {
    // wait for input buffer to be empty
  }
}

static void keyboard_send_command(uint8_t command) {
  keyboard_wait_input_ready();
  io_outb(KEYBOARD_DATA_PORT, command);
}

static void keyboard_update_leds(void) {
  uint8_t led_state = 0;
  
  if (scroll_lock_state) led_state |= KBD_LED_SCROLL_LOCK;
  if (num_lock_state) led_state |= KBD_LED_NUM_LOCK;
  if (caps_lock_state) led_state |= KBD_LED_CAPS_LOCK;
  
  // send set LED command
  keyboard_send_command(KBD_CMD_SET_LEDS);
  
  // wait for acknowledgment and send LED state
  uint8_t response = keyboard_read_data();
  if (response == KBD_RESP_ACK) {
    keyboard_send_command(led_state);
    // wait for final acknowledgment
    response = keyboard_read_data();
    if (response != KBD_RESP_ACK) {
      EPRINTF("failed to set LEDs, response: 0x%02x\n", response);
    }
  } else {
    EPRINTF("keyboard did not acknowledge LED command, response: 0x%02x\n", response);
  }
}

static void keyboard_process_scancode(uint8_t scancode) {
  uint16_t keycode = 0;
  
  // handle extended scan code prefix
  if (scancode == SCANCODE_EXTENDED) {
    extended_scancode = true;
    return;
  }
  
  // determine if this is a key press or release (bit 7 set = release)
  bool key_released = (scancode & SCANCODE_RELEASE_MASK) != 0;
  uint8_t base_scancode = scancode & ~SCANCODE_RELEASE_MASK;
  
  // determine key code based on scan code and extended flag
  if (extended_scancode) {
    keycode = extended_scancode_to_keycode[base_scancode];
    extended_scancode = false;
  } else {
    keycode = scancode_to_keycode[base_scancode];
  }
  
  if (keycode == 0) {
    DPRINTF("unknown scancode: 0x%02x%s\n", base_scancode, extended_scancode ? " (extended)" : "");
    return;
  }
  
  // determine key state
  int32_t value = key_released ? 0 : 1;
  
  // handle lock keys (only on key press, not release)
  if (value == 1) {
    bool update_leds = false;
    
    switch (keycode) {
      case KEY_CAPSLOCK:
        caps_lock_state = !caps_lock_state;
        update_leds = true;
        DPRINTF("caps lock %s\n", caps_lock_state ? "on" : "off");
        break;
      case KEY_NUM_LOCK:
        num_lock_state = !num_lock_state;
        update_leds = true;
        DPRINTF("num lock %s\n", num_lock_state ? "on" : "off");
        break;
      case KEY_SCROLL_LOCK:
        scroll_lock_state = !scroll_lock_state;
        update_leds = true;
        DPRINTF("scroll lock %s\n", scroll_lock_state ? "on" : "off");
        break;
      default:
        // no special handling for other keys
        break;
    }
    
    if (update_leds) {
      keyboard_update_leds();
    }
  }
  
  DPRINTF("key %s: 0x%03x (scancode 0x%02x)\n", value ? "press" : "release", keycode, scancode);
  
  // send input event
  input_event(EV_KEY, keycode, value);
}

static void keyboard_irq_handler(struct trapframe *frame) {
  uint8_t status = io_inb(KEYBOARD_STATUS_PORT);
  
  if (!(status & KBD_STATUS_OUTPUT_FULL)) {
    // no data available
    return;
  }
  
  if (status & KBD_STATUS_MOUSE_DATA) {
    // this is mouse data, not keyboard data - read and discard
    io_inb(KEYBOARD_DATA_PORT);
    return;
  }
  
  uint8_t scancode = io_inb(KEYBOARD_DATA_PORT);
  keyboard_process_scancode(scancode);
}
