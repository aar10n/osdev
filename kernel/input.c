//
// Created by Aaron Gill-Braun on 2022-12-09.
//

#include <kernel/input.h>
#include <kernel/device.h>
#include <kernel/proc.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/vfs_types.h>
#include <kernel/vfs/file.h>
#include <kernel/vfs/vnode.h>
#include <kernel/kevent.h>

#include <fs/devfs/devfs.h>

#include <bitmap.h>
#include <rb_tree.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("input: " x, ##__VA_ARGS__)
#define DPRINTFF(x, ...) kprintf("input: %s: " x, __func__, ##__VA_ARGS__)
#define EPRINTF(x, ...) kprintf("input: %s: " x, __func__, ##__VA_ARGS__)

#define EVSTREAM_KEYBOARD ((void *)1)
#define EVSTREAM_MOUSE    ((void *)2)

static const char *input_code_to_name[] = {
  [KEY_LEFTCTRL] = "KEY_LEFTCTRL",
  [KEY_LEFTSHIFT] = "KEY_LEFTSHIFT",
  [KEY_LEFTALT] = "KEY_LEFTALT",
  [KEY_LEFTMETA] = "KEY_LEFTMETA",
  [KEY_RIGHTCTRL] = "KEY_RIGHTCTRL",
  [KEY_RIGHTSHIFT] = "KEY_RIGHTSHIFT",
  [KEY_RIGHTALT] = "KEY_RIGHTALT",
  [KEY_RIGHTMETA] = "KEY_RIGHTMETA",

  [KEY_A] = "KEY_A",
  [KEY_B] = "KEY_B",
  [KEY_C] = "KEY_C",
  [KEY_D] = "KEY_D",
  [KEY_E] = "KEY_E",
  [KEY_F] = "KEY_F",
  [KEY_G] = "KEY_G",
  [KEY_H] = "KEY_H",
  [KEY_I] = "KEY_I",
  [KEY_J] = "KEY_J",
  [KEY_K] = "KEY_K",
  [KEY_L] = "KEY_L",
  [KEY_M] = "KEY_M",
  [KEY_N] = "KEY_N",
  [KEY_O] = "KEY_O",
  [KEY_P] = "KEY_P",
  [KEY_Q] = "KEY_Q",
  [KEY_R] = "KEY_R",
  [KEY_S] = "KEY_S",
  [KEY_T] = "KEY_T",
  [KEY_U] = "KEY_U",
  [KEY_V] = "KEY_V",
  [KEY_W] = "KEY_W",
  [KEY_X] = "KEY_X",
  [KEY_Y] = "KEY_Y",
  [KEY_Z] = "KEY_Z",

  [KEY_1] = "KEY_1",
  [KEY_2] = "KEY_2",
  [KEY_3] = "KEY_3",
  [KEY_4] = "KEY_4",
  [KEY_5] = "KEY_5",
  [KEY_6] = "KEY_6",
  [KEY_7] = "KEY_7",
  [KEY_8] = "KEY_8",
  [KEY_9] = "KEY_9",
  [KEY_0] = "KEY_0",

  [KEY_F1] = "KEY_F1",
  [KEY_F2] = "KEY_F2",
  [KEY_F3] = "KEY_F3",
  [KEY_F4] = "KEY_F4",
  [KEY_F5] = "KEY_F5",
  [KEY_F6] = "KEY_F6",
  [KEY_F7] = "KEY_F7",
  [KEY_F8] = "KEY_F8",
  [KEY_F9] = "KEY_F9",
  [KEY_F10] = "KEY_F10",
  [KEY_F11] = "KEY_F11",
  [KEY_F12] = "KEY_F12",

  [KEY_ENTER] = "KEY_ENTER",
  [KEY_ESC] = "KEY_ESC",
  [KEY_BACKSPACE] = "KEY_BACKSPACE",
  [KEY_TAB] = "KEY_TAB",
  [KEY_SPACE] = "KEY_SPACE",
  [KEY_CAPSLOCK] = "KEY_CAPSLOCK",

  [KEY_MINUS] = "KEY_MINUS",
  [KEY_EQUAL] = "KEY_EQUAL",
  [KEY_LEFTBRACE] = "KEY_LEFTBRACE",
  [KEY_RIGHTBRACE] = "KEY_RIGHTBRACE",
  [KEY_BACKSLASH] = "KEY_BACKSLASH",
  [KEY_SEMICOLON] = "KEY_SEMICOLON",
  [KEY_APOSTROPHE] = "KEY_APOSTROPHE",
  [KEY_GRAVE] = "KEY_GRAVE",
  [KEY_COMMA] = "KEY_COMMA",
  [KEY_DOT] = "KEY_DOT",
  [KEY_SLASH] = "KEY_SLASH",

  [KEY_RIGHT] = "KEY_RIGHT",
  [KEY_LEFT] = "KEY_LEFT",
  [KEY_DOWN] = "KEY_DOWN",
  [KEY_UP] = "KEY_UP",

  [KEY_SYSRQ] = "KEY_SYSRQ",
  [KEY_SCROLLLOCK] = "KEY_SCROLLLOCK",
  [KEY_PAUSE] = "KEY_PAUSE",
  [KEY_INSERT] = "KEY_INSERT",
  [KEY_HOME] = "KEY_HOME",
  [KEY_END] = "KEY_END",
  [KEY_PAGEUP] = "KEY_PAGEUP",
  [KEY_PAGEDOWN] = "KEY_PAGEDOWN",
  [KEY_DELETE] = "KEY_DELETE",

  [BTN_LEFT] = "BTN_LEFT",
  [BTN_RIGHT] = "BTN_RIGHT",
  [BTN_MIDDLE] = "BTN_MIDDLE",
};

static inline char key_to_char_lower(uint16_t key) {
  switch (key) {
    case KEY_A: return 'a';
    case KEY_B: return 'b';
    case KEY_C: return 'c';
    case KEY_D: return 'd';
    case KEY_E: return 'e';
    case KEY_F: return 'f';
    case KEY_G: return 'g';
    case KEY_H: return 'h';
    case KEY_I: return 'i';
    case KEY_J: return 'j';
    case KEY_K: return 'k';
    case KEY_L: return 'l';
    case KEY_M: return 'm';
    case KEY_N: return 'n';
    case KEY_O: return 'o';
    case KEY_P: return 'p';
    case KEY_Q: return 'q';
    case KEY_R: return 'r';
    case KEY_S: return 's';
    case KEY_T: return 't';
    case KEY_U: return 'u';
    case KEY_V: return 'v';
    case KEY_W: return 'w';
    case KEY_X: return 'x';
    case KEY_Y: return 'y';
    case KEY_Z: return 'z';

    case KEY_1: return '1';
    case KEY_2: return '2';
    case KEY_3: return '3';
    case KEY_4: return '4';
    case KEY_5: return '5';
    case KEY_6: return '6';
    case KEY_7: return '7';
    case KEY_8: return '8';
    case KEY_9: return '9';
    case KEY_0: return '0';

    case KEY_ENTER: return '\n';
    case KEY_BACKSPACE: return '\b';
    case KEY_TAB: return '\t';
    case KEY_SPACE: return ' ';

    case KEY_MINUS: return '-';
    case KEY_EQUAL: return '=';
    case KEY_LEFTBRACE: return '[';
    case KEY_RIGHTBRACE: return ']';
    case KEY_BACKSLASH: return '\\';
    case KEY_SEMICOLON: return ';';
    case KEY_APOSTROPHE: return '\'';
    case KEY_GRAVE: return '`';
    case KEY_COMMA: return ',';
    case KEY_DOT: return '.';
    case KEY_SLASH: return '/';
    default: return 0;
  }
}

static inline char key_to_char_upper(uint16_t key) {
  switch (key) {
    case KEY_A: return 'A';
    case KEY_B: return 'B';
    case KEY_C: return 'C';
    case KEY_D: return 'D';
    case KEY_E: return 'E';
    case KEY_F: return 'F';
    case KEY_G: return 'G';
    case KEY_H: return 'H';
    case KEY_I: return 'I';
    case KEY_J: return 'J';
    case KEY_K: return 'K';
    case KEY_L: return 'L';
    case KEY_M: return 'M';
    case KEY_N: return 'N';
    case KEY_O: return 'O';
    case KEY_P: return 'P';
    case KEY_Q: return 'Q';
    case KEY_R: return 'R';
    case KEY_S: return 'S';
    case KEY_T: return 'T';
    case KEY_U: return 'U';
    case KEY_V: return 'V';
    case KEY_W: return 'W';
    case KEY_X: return 'X';
    case KEY_Y: return 'Y';
    case KEY_Z: return 'Z';

    case KEY_1: return '!';
    case KEY_2: return '@';
    case KEY_3: return '#';
    case KEY_4: return '$';
    case KEY_5: return '%';
    case KEY_6: return '^';
    case KEY_7: return '&';
    case KEY_8: return '*';
    case KEY_9: return '(';
    case KEY_0: return ')';

    case KEY_ENTER: return '\n';
    case KEY_BACKSPACE: return '\b';
    case KEY_TAB: return '\t';
    case KEY_SPACE: return ' ';

    case KEY_MINUS: return '_';
    case KEY_EQUAL: return '+';
    case KEY_LEFTBRACE: return '{';
    case KEY_RIGHTBRACE: return '}';
    case KEY_BACKSLASH: return '|';
    case KEY_SEMICOLON: return ':';
    case KEY_APOSTROPHE: return '"';
    case KEY_GRAVE: return '~';
    case KEY_COMMA: return '<';
    case KEY_DOT: return '>';
    case KEY_SLASH: return '?';
    default: return 0;
  }
}

static inline uint8_t key_to_modifier_bit(uint16_t key) {
  switch (key) {
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
      return MOD_CTRL;
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
      return MOD_SHIFT;
    case KEY_LEFTALT:
    case KEY_RIGHTALT:
      return MOD_ALT;
    case KEY_LEFTMETA:
    case KEY_RIGHTMETA:
      return MOD_META;
    case KEY_CAPSLOCK:
      return MOD_CAPS;
    default:
      return 0;
  }
}

static bitmap_t *key_states;
static uint8_t key_modifiers;
chan_t *key_event_stream;
chan_t *mouse_event_stream;

//

void input_static_init() {
  key_states = create_bitmap(KEY_MAX + 1);
  key_event_stream = chan_alloc(128, sizeof(struct input_event), 0, "key_event_stream_ch");
  mouse_event_stream = chan_alloc(256, sizeof(struct input_event), 0, "mouse_event_stream_ch");
}
STATIC_INIT(input_static_init);


static int input_process_key_event(uint16_t code, int32_t value) {
  uint8_t bit = key_to_modifier_bit(code);
  if (code >= KEY_MAX) {
    return -1;
  }

  DPRINTF("input: key event %s (%d) [value = %d]\n", input_code_to_name[code], code, value);

  // update keymap state
  if (value) {
    bitmap_set(key_states, code);
    if (bit)
      key_modifiers |= bit;
  } else {
    bitmap_clear(key_states, code);
    if (bit)
      key_modifiers &= ~bit; // clear modifier
  }

  if (bit) {
    // update modifier state
    if (value) {
      key_modifiers |= bit;
    } else {
      key_modifiers &= ~bit;
    }
  }

  // format and send key event to stream
  struct input_event event = {
    .time = {0},
    .type = EV_KEY,
    .code = code,
    .value = value,
  };
  int res;
  if ((res = chan_send(key_event_stream, &event)) < 0) {
    DPRINTF("input: failed to send key event to stream: {:err}\n", res);
    return res;
  }
  return 0;
}

//

static int input_forward_mouse_event(uint16_t type, uint16_t code, int32_t value) {
  struct input_event event = {
    .time = {0},
    .type = type,
    .code = code,
    .value = value,
  };
  int res = chan_send(mouse_event_stream, &event);
  if (res < 0) {
    DPRINTF("failed to send mouse event to stream: {:err}\n", res);
  }
  return 0;
}

int input_event(uint16_t type, uint16_t code, int32_t value) {
  switch (type) {
    case EV_KEY:
      if (code >= BTN_MOUSE && code < BTN_JOYSTICK)
        return input_forward_mouse_event(type, code, value);
      return input_process_key_event(code, value);
    case EV_SYN:
    case EV_REL:
    case EV_ABS:
      return input_forward_mouse_event(type, code, value);
    default:
      return 0;
  }
}

int input_getkey(uint16_t key) {
  if (key >= KEY_MAX) {
    return 0;
  }
  return bitmap_get(key_states, key);
}

int input_key_event_to_char(input_key_event_t *event) {
  kassert(event);
  if (event->modifiers & MOD_SHIFT || event->modifiers & MOD_CAPS) {
    return key_to_char_upper(event->key);
  }
  return key_to_char_lower(event->key);
}

//
// MARK: Event Streams
//

static LIST_HEAD(struct event_stream) ev_streams_list;
static mtx_t ev_streams_lock;

struct event_stream {
  void *stream; // identifier for the event stream (keyboard or mouse)
  chan_t *chan; // channel for this event stream
  vnode_t *vn; // vnode for kqueue notification
  LIST_ENTRY(struct event_stream) list; // event streams list entry
};

static void distribute_event(struct input_event *event, void *stream_type) {
  mtx_lock(&ev_streams_lock);
  LIST_FOR_IN(evs, &ev_streams_list, list) {
    if (evs->stream == stream_type) {
      int res = chan_send(evs->chan, event);
      if (res < 0) {
        DPRINTF("failed to send event to stream %p: {:err}\n", evs, res);
      } else if (evs->vn) {
        knlist_activate_notes(&evs->vn->knlist, 0);
      }
    }
  }
  mtx_unlock(&ev_streams_lock);
}

static int notify_key_event_streams() {
  struct input_event event;
  while (chan_recv(key_event_stream, &event) == 0) {
    distribute_event(&event, EVSTREAM_KEYBOARD);
  }
  return 0;
}

static int notify_mouse_event_streams() {
  struct input_event event;
  while (chan_recv(mouse_event_stream, &event) == 0) {
    distribute_event(&event, EVSTREAM_MOUSE);
  }
  return 0;
}

static void spawn_kthread(uintptr_t entry, const char *name) {
  __ref proc_t *p = proc_alloc_new(getref(curproc->creds));
  proc_setup_add_thread(p, thread_alloc(TDF_KTHREAD, SIZE_16KB));
  proc_setup_entry(p, entry, 0);
  proc_setup_name(p, cstr_make(name));
  proc_finish_setup_and_submit_all(moveref(p));
}

static void event_streams_init() {
  mtx_init(&ev_streams_lock, 0, "ev_streams_lock");
  LIST_INIT(&ev_streams_list);
  spawn_kthread((uintptr_t)notify_key_event_streams, "key_event_notify");
  spawn_kthread((uintptr_t)notify_mouse_event_streams, "mouse_event_notify");
}
MODULE_INIT(event_streams_init);

//
// MARK: Event Streams File API
//

int event_stream_f_open(file_t *file, int flags) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(file->nopen == 0);
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
  device_t *device = vn->v_dev;
  void *event_stream = device->data;
  ASSERT(event_stream == EVSTREAM_KEYBOARD || event_stream == EVSTREAM_MOUSE);

  struct event_stream *evs = kmallocz(sizeof(struct event_stream));
  evs->stream = event_stream;
  evs->chan = chan_alloc(128, sizeof(struct input_event), 0, "event_stream_ch");
  evs->vn = vn;
  LIST_ENTRY_INIT(&evs->list);
  
  // add to event streams list
  mtx_lock(&ev_streams_lock);
  LIST_ADD(&ev_streams_list, evs, list);
  mtx_unlock(&ev_streams_lock);
  
  file->udata = evs;

  DPRINTFF("opening file %p with flags 0x%x [evs %p, event_stream %p]\n", file, flags, evs, event_stream);
  return 0;
}

int event_stream_f_close(file_t *file) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(file->nopen == 1);
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));

  struct event_stream *evs = moveptr(file->udata);
  DPRINTFF("closing file %p [evs %p, event_stream %p]\n", file, evs, evs->stream);

  // remove from event streams list
  mtx_lock(&ev_streams_lock);
  LIST_REMOVE(&ev_streams_list, evs, list);
  mtx_unlock(&ev_streams_lock);

  chan_close(evs->chan);
  chan_free(evs->chan);
  kfree(evs);
  return 0;
}

ssize_t event_stream_f_read(file_t *file, kio_t *kio) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));
  if (file->flags & O_WRONLY)
    return -EBADF; // file is not open for reading
  if (kio_remaining(kio) < sizeof(struct input_event))
    return -ENOSPC; // not enough space in the buffer to read an event

  struct event_stream *evs = file->udata;
//  DPRINTFF("reading from file %p at offset %lld [evs %p, event_stream %#x]\n", file, file->offset, evs, evs->stream);

  ssize_t nbytes = 0;
  struct input_event event;
  // read all events in the channel without blocking
  while (chan_recv_noblock(evs->chan, &event) >= 0) {
    size_t n = kio_write_in(kio, &event, sizeof(event), 0);
    nbytes += (ssize_t) n;
    if (n == 0 || kio_remaining(kio) < sizeof(struct input_event)) {
      // buffer is full
      return nbytes;
    }
  }

  if (nbytes > 0) {
    // return data already read without blocking
    DPRINTFF("read %zu bytes from file %p [evs %p, event_stream %p]\n", nbytes, file, evs, evs->stream);
    return nbytes;
  }

  if (file->flags & O_NONBLOCK) {
    return -EAGAIN;
  }

  // no data available, block until at least one event arrives
  int res = chan_recv(evs->chan, &event);
  if (res < 0) {
    EPRINTF("failed to read from event stream: {:err}\n", res);
    return res;
  }
  size_t n = kio_write_in(kio, &event, sizeof(event), 0);
  ASSERT(n > 0);
  nbytes = (ssize_t) n;

  // drain any additional events without blocking
  while (kio_remaining(kio) >= sizeof(struct input_event) &&
         chan_recv_noblock(evs->chan, &event) >= 0) {
    n = kio_write_in(kio, &event, sizeof(event), 0);
    nbytes += (ssize_t) n;
    if (n == 0) break;
  }

  DPRINTFF("read %zu bytes from file %p [evs %p, event_stream %p]\n", nbytes, file, evs, evs->stream);
  return nbytes;
}

int event_stream_f_ioctl(file_t *file, unsigned int request, void *arg) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));

  struct event_stream *evs = file->udata;
  DPRINTFF("ioctl on file %p with request %#llx [evs %p, event_stream %p]\n",
           file, request, evs, evs->stream);

  size_t req_size = _IOC_SIZE(request);
  unsigned int nr = _IOC_NR(request);

  if ((request & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT)) == (EVIOCGNAME(0) & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT))) {
    const char *name;
    if (evs->stream == EVSTREAM_KEYBOARD) {
      name = "keyboard";
    } else {
      name = "mouse";
    }

    size_t name_len = strlen(name) + 1;
    if (req_size < name_len)
      name_len = req_size;
    memcpy(arg, name, name_len);
    return (int) name_len;
  } else if (request == EVIOCGRAB) {
    return 0;
  } else if (nr >= 0x20 && nr < 0x40) {
    // EVIOCGBIT
    unsigned int ev = nr - 0x20;
    if (req_size == 0 || arg == NULL)
      return -EINVAL;
    memset(arg, 0, req_size);

    if (ev == EV_SYN) {
      uint8_t *bits = arg;
      if (req_size > EV_SYN / 8) bits[EV_SYN / 8] |= (1 << (EV_SYN % 8));
      if (req_size > EV_KEY / 8) bits[EV_KEY / 8] |= (1 << (EV_KEY % 8));
      if (evs->stream == EVSTREAM_KEYBOARD) {
        if (req_size > EV_REP / 8) bits[EV_REP / 8] |= (1 << (EV_REP % 8));
      }
      if (evs->stream == EVSTREAM_MOUSE) {
        if (req_size > EV_REL / 8) bits[EV_REL / 8] |= (1 << (EV_REL % 8));
      }
    } else if (ev == EV_KEY && evs->stream == EVSTREAM_KEYBOARD) {
      uint8_t *bits = arg;
      size_t max_code = req_size * 8;
      for (size_t i = 0; i < ARRAY_SIZE(input_code_to_name); i++) {
        if (input_code_to_name[i] != NULL && i < max_code)
          bits[i / 8] |= (1 << (i % 8));
      }
    } else if (ev == EV_KEY && evs->stream == EVSTREAM_MOUSE) {
      uint8_t *bits = arg;
      size_t max_bit = req_size * 8;
      uint16_t btns[] = { BTN_LEFT, BTN_RIGHT, BTN_MIDDLE };
      for (size_t i = 0; i < ARRAY_SIZE(btns); i++) {
        if (btns[i] < max_bit)
          bits[btns[i] / 8] |= (1 << (btns[i] % 8));
      }
    } else if (ev == EV_REL && evs->stream == EVSTREAM_MOUSE) {
      uint8_t *bits = arg;
      if (req_size > REL_X / 8) bits[REL_X / 8] |= (1 << (REL_X % 8));
      if (req_size > REL_Y / 8) bits[REL_Y / 8] |= (1 << (REL_Y % 8));
    }
    return 0;
  } else if (nr >= 0x40 && nr < 0x80) {
    // EVIOCGABS
    if (req_size < sizeof(struct input_absinfo))
      return -EINVAL;
    memset(arg, 0, sizeof(struct input_absinfo));
    return 0;
  }

  DPRINTFF("unhandled ioctl %#x\n", request);
  return -ENOTTY;
}

int event_stream_f_kqevent(file_t *file, knote_t *kn) {
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));
  ASSERT(kn->event.filter == EVFILT_READ);

  struct event_stream *evs = file->udata;
  uint16_t count = chan_length(evs->chan);
  if (count > 0) {
    kn->event.data = count * sizeof(struct input_event);
    return 1;
  }
  return 0;
}

struct file_ops event_stream_f_ops = {
  .f_open = event_stream_f_open,
  .f_close = event_stream_f_close,
  .f_read = event_stream_f_read,
  .f_ioctl = event_stream_f_ioctl,
  .f_stat = dev_f_stat,
  .f_kqevent = event_stream_f_kqevent,
  .f_cleanup = dev_f_cleanup,
};

// MARK: Device Registration

static void event_stream_module_init() {
  devfs_register_class(dev_major_by_name("input"), -1, "input/event", DEVFS_NUMBERED);

  kprintf("input: registering keyboard event_stream\n");
  if (register_dev("input", alloc_device(EVSTREAM_KEYBOARD, NULL, &event_stream_f_ops)) < 0) {
    panic("failed to register keyboard event stream");
  }

  kprintf("input: registering mouse event_stream\n");
  if (register_dev("input", alloc_device(EVSTREAM_MOUSE, NULL, &event_stream_f_ops)) < 0) {
    panic("failed to register mouse event stream");
  }
}
MODULE_INIT(event_stream_module_init);

