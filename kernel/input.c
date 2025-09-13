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

#include <fs/devfs/devfs.h>

#include <uapi/osdev/input.h>

#include <bitmap.h>
#include <rb_tree.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("input: " x, ##__VA_ARGS__)
#define DPRINTFF(x, ...) kprintf("input: %s: " x, __func__, ##__VA_ARGS__)
#define EPRINTF(x, ...) kprintf("input: %s: " x, __func__, ##__VA_ARGS__)

#define EVSTREAM_KEYBOARD ((void *)1)
#define EVSTREAM_MOUSE    ((void *)2)

static const char *input_code_to_name[] = {
  [KEY_LCTRL] = "KEY_LCTRL",
  [KEY_LSHIFT] = "KEY_LSHIFT",
  [KEY_LALT] = "KEY_LALT",
  [KEY_LMETA] = "KEY_LMETA",
  [KEY_RCTRL] = "KEY_RCTRL",
  [KEY_RSHIFT] = "KEY_RSHIFT",
  [KEY_RALT] = "KEY_RALT",
  [KEY_RMETA] = "KEY_RMETA",

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

  [KEY_ENTER] = "KEY_RETURN",
  [KEY_ESCAPE] = "KEY_ESCAPE",
  [KEY_BACKSPACE] = "KEY_DELETE",
  [KEY_TAB] = "KEY_TAB",
  [KEY_SPACE] = "KEY_SPACE",
  [KEY_CAPSLOCK] = "KEY_CAPSLOCK",

  [KEY_MINUS] = "KEY_MINUS",
  [KEY_EQUAL] = "KEY_EQUAL",
  [KEY_LSQUARE] = "KEY_LSQUARE",
  [KEY_RSQUARE] = "KEY_RSQUARE",
  [KEY_BACKSLASH] = "KEY_BACKSLASH",
  [KEY_SEMICOLON] = "KEY_SEMICOLON",
  [KEY_APOSTROPHE] = "KEY_APOSTROPHE",
  [KEY_GRAVE] = "KEY_TILDE",
  [KEY_COMMA] = "KEY_COMMA",
  [KEY_PERIOD] = "KEY_PERIOD",
  [KEY_SLASH] = "KEY_SLASH",

  [KEY_RIGHT] = "KEY_RIGHT",
  [KEY_LEFT] = "KEY_LEFT",
  [KEY_DOWN] = "KEY_DOWN",
  [KEY_UP] = "KEY_UP",

  [KEY_PRINTSCR] = "KEY_PRINTSCR",
  [KEY_SCROLL_LOCK] = "KEY_SCROLL_LOCK",
  [KEY_PAUSE] = "KEY_PAUSE",
  [KEY_INSERT] = "KEY_INSERT",
  [KEY_HOME] = "KEY_HOME",
  [KEY_END] = "KEY_END",
  [KEY_PAGE_UP] = "KEY_PAGE_UP",
  [KEY_PAGE_DOWN] = "KEY_PAGE_DOWN",
  [KEY_DELETE] = "KEY_DELETE_FWD",

  [BTN_MOUSE1] = "BTN_MOUSE1",
  [BTN_MOUSE2] = "BTN_MOUSE2",
  [BTN_MOUSE3] = "BTN_MOUSE3",
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
    case KEY_LSQUARE: return '[';
    case KEY_RSQUARE: return ']';
    case KEY_BACKSLASH: return '\\';
    case KEY_SEMICOLON: return ';';
    case KEY_APOSTROPHE: return '\'';
    case KEY_GRAVE: return '`';
    case KEY_COMMA: return ',';
    case KEY_PERIOD: return '.';
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
    case KEY_LSQUARE: return '{';
    case KEY_RSQUARE: return '}';
    case KEY_BACKSLASH: return '|';
    case KEY_SEMICOLON: return ':';
    case KEY_APOSTROPHE: return '"';
    case KEY_GRAVE: return '~';
    case KEY_COMMA: return '<';
    case KEY_PERIOD: return '>';
    case KEY_SLASH: return '?';
    default: return 0;
  }
}

static inline uint8_t key_to_modifier_bit(uint16_t key) {
  switch (key) {
    case KEY_LCTRL:
    case KEY_RCTRL:
      return MOD_CTRL;
    case KEY_LSHIFT:
    case KEY_RSHIFT:
      return MOD_SHIFT;
    case KEY_LALT:
    case KEY_RALT:
      return MOD_ALT;
    case KEY_LMETA:
    case KEY_RMETA:
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

//

void input_static_init() {
  key_states = create_bitmap(KEY_MAX + 1);
  key_event_stream = chan_alloc(128, sizeof(struct input_event), 0, "key_event_stream_ch");
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

int input_event(uint16_t type, uint16_t code, int32_t value) {
  switch (type) {
    case EV_KEY:
      return input_process_key_event(code, value);
    case EV_REL:
      todo();
    case EV_ABS:
      todo();
    default:
      panic("input_event: unknown event type %d", type);
  }
  return 0;
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
  LIST_ENTRY(struct event_stream) list; // event streams list entry
};

static int notify_event_streams() {
  DPRINTF("starting event stream notification process\n");

  struct input_event event;
  while (chan_recv(key_event_stream, &event) == 0) {
    DPRINTF("distributing key event [type=%d, code=%d, value=0x%x]\n", event.type, event.code, event.value);

    // iterate through all event streams to find keyboard ones
    mtx_lock(&ev_streams_lock);
    LIST_FOR_IN(evs, &ev_streams_list, list) {
      if (evs->stream == EVSTREAM_KEYBOARD) {
        // send event to this event stream
        int res = chan_send(evs->chan, &event);
        if (res < 0) {
          DPRINTF("failed to send event to stream %p: {:err}\n", evs, res);
        }
      }
    }
    mtx_unlock(&ev_streams_lock);
  }
  
  DPRINTF("event stream notification process exiting\n");
  return 0;
}

static void event_streams_init() {
  // initialize event streams list
  mtx_init(&ev_streams_lock, 0, "ev_streams_lock");
  LIST_INIT(&ev_streams_list);
  
  // create event stream notification process
  __ref proc_t *notify_proc = proc_alloc_new(getref(curproc->creds));
  proc_setup_add_thread(notify_proc, thread_alloc(TDF_KTHREAD, SIZE_16KB));
  proc_setup_entry(notify_proc, (uintptr_t) notify_event_streams, 0);
  proc_setup_name(notify_proc, cstr_make("event_stream_notify"));
  proc_finish_setup_and_submit_all(moveref(notify_proc));
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

  if (file->flags & O_NONBLOCK) {
    // file was opened in non-blocking mode
    if (nbytes == 0) {
      // no data available, return EAGAIN
//      DPRINTFF("no data available for file %p [evs %p, event_stream %#x]\n", file, evs, evs->stream);
      return -EAGAIN;
    } else {
      // return the number of bytes read
      DPRINTFF("read %zu bytes from file %p [evs %p, event_stream %p]\n", nbytes, file, evs, evs->stream);
      return nbytes;
    }
  } else if (kio_remaining(kio) < sizeof(struct input_event)) {
    // no more space in the buffer we can return
    DPRINTFF("read %zu bytes from file %p [evs %p, event_stream %p]\n", nbytes, file, evs, evs->stream);
    return nbytes;
  }

  // at this point we can block on the channel
  int res;
  while ((res = chan_recv(evs->chan, &event)) >= 0) {
    size_t n = kio_write_in(kio, &event, sizeof(event), 0);
    ASSERT(n > 0);
    nbytes += (ssize_t) n;
    if (kio_remaining(kio) < sizeof(struct input_event)) {
      // no more space in the buffer we can return
      break;
    }
  }

  if (res < 0) {
    // an error occurred while reading from the channel
    EPRINTF("failed to read from event stream file %p [evs %p, event_stream %p] {:err}\n",
            file, evs, evs->stream, res);
    return res;
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
  if ((unsigned int)(request & ~_IOC_SIZEMASK) == EVIOCGNAME(0)) {
    // get name
    const char *name;
    if (evs->stream == EVSTREAM_KEYBOARD) {
      name = "keyboard";
    } else {
      name = "mouse";
    }

    size_t name_len = sizeof(name)+1;
    if (req_size < name_len) {
      EPRINTF("ioctl request %#llx size %zu too small for name\n", request, req_size);
      return -EINVAL; // buffer too small
    }

    memcpy(arg, name, name_len);
    return (int) name_len;
  } else {

  }
  return 0;
}

int event_stream_f_kqevent(file_t *file, knote_t *kn) {
  ASSERT(F_ISVNODE(file));
  ASSERT(V_ISDEV((vnode_t *)file->data));
  ASSERT(kn->event.filter == EVFILT_READ);

  // called from the file `event` filter_ops method
  vnode_t *vn = file->data;
  struct event_stream *evs = file->udata;
  DPRINTFF("checking kqevent for file %p [evs %p, event_stream %p, filter %s]\n",
           file, evs, evs->stream, evfilt_to_string(kn->event.filter));

  int res;
  todo();

  if (res < 0) {
    EPRINTF("failed to get kqevent for file %p [evs %p, event_stream %p] {:err}\n",
            file, evs, evs->stream, res);
  } else if (res == 0) {
    kn->event.data = 0;
    DPRINTFF("no data available for file %p [evs %p, event_stream %p]\n",
             file, evs, evs->stream);
  } else {
    DPRINTFF("%d bytes available for file %p [evs %p, event_stream %p]\n",
             res, file, evs, evs->stream);
  }
  return res;
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
  devfs_register_class(dev_major_by_name("input"), -1, "events", DEVFS_NUMBERED);

  kprintf("input: registering keyboard event_stream\n");
  if (register_dev("input", alloc_device(EVSTREAM_KEYBOARD, NULL, &event_stream_f_ops)) < 0) {
    panic("failed to register keyboard event stream");
  }
}
MODULE_INIT(event_stream_module_init);

