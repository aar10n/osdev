//
// Created by Aaron Gill-Braun on 2021-04-20.
//

#include <event.h>
#include <mutex.h>
#include <thread.h>
#include <atomic.h>
#include <bitmap.h>
#include <printf.h>

#define MAX_HANDLERS 32

static bitmap_t *handler_indexes;
static thread_t *handlers[MAX_HANDLERS];

const char key_to_character_lower[] = {
  [VK_KEYCODE_A] = 'a',
  [VK_KEYCODE_B] = 'b',
  [VK_KEYCODE_C] = 'c',
  [VK_KEYCODE_D] = 'd',
  [VK_KEYCODE_E] = 'e',
  [VK_KEYCODE_F] = 'f',
  [VK_KEYCODE_G] = 'g',
  [VK_KEYCODE_H] = 'h',
  [VK_KEYCODE_I] = 'i',
  [VK_KEYCODE_J] = 'j',
  [VK_KEYCODE_K] = 'k',
  [VK_KEYCODE_L] = 'l',
  [VK_KEYCODE_M] = 'm',
  [VK_KEYCODE_N] = 'n',
  [VK_KEYCODE_O] = 'o',
  [VK_KEYCODE_P] = 'p',
  [VK_KEYCODE_Q] = 'q',
  [VK_KEYCODE_R] = 'r',
  [VK_KEYCODE_S] = 's',
  [VK_KEYCODE_T] = 't',
  [VK_KEYCODE_U] = 'u',
  [VK_KEYCODE_V] = 'v',
  [VK_KEYCODE_W] = 'w',
  [VK_KEYCODE_X] = 'x',
  [VK_KEYCODE_Y] = 'y',
  [VK_KEYCODE_Z] = 'z',

  [VK_KEYCODE_1] = '1',
  [VK_KEYCODE_2] = '2',
  [VK_KEYCODE_3] = '3',
  [VK_KEYCODE_4] = '4',
  [VK_KEYCODE_5] = '5',
  [VK_KEYCODE_6] = '6',
  [VK_KEYCODE_7] = '7',
  [VK_KEYCODE_8] = '8',
  [VK_KEYCODE_9] = '9',
  [VK_KEYCODE_0] = '0',

  [VK_KEYCODE_RETURN] = '\n',
  [VK_KEYCODE_DELETE] = '\b',
  [VK_KEYCODE_TAB] = '\t',
  [VK_KEYCODE_SPACE] = ' ',
  [VK_KEYCODE_MINUS] = '-',
  [VK_KEYCODE_EQUAL] = '=',
  [VK_KEYCODE_LSQUARE] = '[',
  [VK_KEYCODE_RSQUARE] = ']',
  [VK_KEYCODE_BACKSLASH] = '\\',
  [VK_KEYCODE_SEMICOLON] = ';',
  [VK_KEYCODE_APOSTROPHE] = '\'',
  [VK_KEYCODE_TILDE] = '`',
  [VK_KEYCODE_COMMA] = ',',
  [VK_KEYCODE_PERIOD] = '.',
  [VK_KEYCODE_SLASH] = '/',
};

const char key_to_character_upper[] = {
  [VK_KEYCODE_A] = 'A',
  [VK_KEYCODE_B] = 'B',
  [VK_KEYCODE_C] = 'C',
  [VK_KEYCODE_D] = 'D',
  [VK_KEYCODE_E] = 'E',
  [VK_KEYCODE_F] = 'F',
  [VK_KEYCODE_G] = 'G',
  [VK_KEYCODE_H] = 'H',
  [VK_KEYCODE_I] = 'I',
  [VK_KEYCODE_J] = 'J',
  [VK_KEYCODE_K] = 'K',
  [VK_KEYCODE_L] = 'L',
  [VK_KEYCODE_M] = 'M',
  [VK_KEYCODE_N] = 'N',
  [VK_KEYCODE_O] = 'O',
  [VK_KEYCODE_P] = 'P',
  [VK_KEYCODE_Q] = 'Q',
  [VK_KEYCODE_R] = 'R',
  [VK_KEYCODE_S] = 'S',
  [VK_KEYCODE_T] = 'T',
  [VK_KEYCODE_U] = 'U',
  [VK_KEYCODE_V] = 'V',
  [VK_KEYCODE_W] = 'W',
  [VK_KEYCODE_X] = 'X',
  [VK_KEYCODE_Y] = 'Y',
  [VK_KEYCODE_Z] = 'Z',

  [VK_KEYCODE_1] = '!',
  [VK_KEYCODE_2] = '@',
  [VK_KEYCODE_3] = '#',
  [VK_KEYCODE_4] = '$',
  [VK_KEYCODE_5] = '%',
  [VK_KEYCODE_6] = '^',
  [VK_KEYCODE_7] = '&',
  [VK_KEYCODE_8] = '*',
  [VK_KEYCODE_9] = '(',
  [VK_KEYCODE_0] = ')',

  [VK_KEYCODE_RETURN] = '\n',
  [VK_KEYCODE_DELETE] = '\b',
  [VK_KEYCODE_TAB] = '\t',
  [VK_KEYCODE_SPACE] = ' ',
  [VK_KEYCODE_MINUS] = '_',
  [VK_KEYCODE_EQUAL] = '+',
  [VK_KEYCODE_LSQUARE] = '{',
  [VK_KEYCODE_RSQUARE] = '}',
  [VK_KEYCODE_BACKSLASH] = '|',
  [VK_KEYCODE_SEMICOLON] = ':',
  [VK_KEYCODE_APOSTROPHE] = '"',
  [VK_KEYCODE_TILDE] = '~',
  [VK_KEYCODE_COMMA] = '<',
  [VK_KEYCODE_PERIOD] = '>',
  [VK_KEYCODE_SLASH] = '?',
};

//

void events_init() {
  bitmap_t *bmp = create_bitmap(1024);
  handler_indexes = bmp;
}

key_event_t *wait_for_key_event() {
  index_t idx = bitmap_get_set_free(handler_indexes);
  if (idx < 0) {
    ERRNO = EINVAL;
    return NULL;
  }
  handlers[idx] = PERCPU_THREAD;
  cond_wait(&PERCPU_THREAD->data_ready);
  handlers[idx] = NULL;
  bitmap_clear(handler_indexes, idx);
  return PERCPU_THREAD->data;
}

void dispatch_key_event(key_event_t *event) {
  char c = key_event_to_character(event);
  kprintf("> %c\n", c);
  PERCPU_THREAD->preempt_count++;
  for (int i = 0; i < MAX_HANDLERS; i++) {
    thread_t *t = handlers[i];
    if (t == NULL) {
      break;
    }
    t->data = event;
    cond_signal(&t->data_ready);
  }
  PERCPU_THREAD->preempt_count--;
}

bool is_printable_key(key_code_t key) {
  if ((key >= VK_KEYCODE_A && key <= VK_KEYCODE_0) ||
      (key >= VK_KEYCODE_MINUS && key <= VK_KEYCODE_SLASH) ||
      key == VK_KEYCODE_SPACE || key == VK_KEYCODE_RETURN ||
      key == VK_KEYCODE_DELETE) {
    return true;
  }
  return false;
}

char key_event_to_character(key_event_t *event) {
  if (!is_printable_key(event->key_code)) {
    return '\0';
  } else if (event->release) {
    return '\0';
  }

  if (event->modifiers & L_SHIFT || event->modifiers & R_SHIFT) {
    // kprintf("> %c\n", key_to_character_upper[event->key_code]);
    return key_to_character_upper[event->key_code];
  }
  // kprintf("> %c\n", key_to_character_lower[event->key_code]);
  return key_to_character_lower[event->key_code];
}
