//
// Created by Aaron Gill-Braun on 2025-05-30.
//

#include <kernel/tty/ttyqueue.h>
#include <kernel/mm.h>

#include <kernel/printf.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)
//#define DPRINTF(fmt, ...) kprintf("tty_queue: " fmt, ##__VA_ARGS__)
#define DPRINTF(fmt, ...)
#define EPRINTF(fmt, ...) kprintf("tty_queue: %s: " fmt, __func__, ##__VA_ARGS__)

static void tty_queue_free(void **self, uintptr_t *data_buf, size_t data_size, uint32_t **quote_buf) {
  ASSERT(data_buf != NULL);
  ASSERT(data_size > 0);

  if (*data_buf != 0) {
    if (vmap_free(*data_buf, data_size) < 0) {
      EPRINTF("failed to free input queue buffer of size %zu bytes\n", data_size);
    }
    *data_buf = 0;
  }

  if (quote_buf != NULL) {
    kfreep(quote_buf);
  }
  kfreep(self);
}

static int tty_queue_setsize(size_t size, size_t *data_size, uintptr_t *data_buf, uint32_t **quote_buf) {
  ASSERT(data_size != NULL);
  ASSERT(data_buf != NULL);

  size = align(size, PAGE_SIZE); // align to page size
  size_t quotesz = (size + 31) / 32; // number of 32-bit words needed
  if (size == 0 && *data_buf != 0) {
    // free the buffer
    if (vmap_free(*data_buf, *data_size) < 0) {
      EPRINTF("failed to free input queue buffer of size %zu bytes\n", *data_size);
      return -1;
    }

    *data_buf = 0;
    *data_size = 0;
    if (quote_buf != NULL) {
      kfreep(quote_buf);
    }
  } else if (*data_buf != 0) {
    if (vmap_resize(*data_buf, *data_size, size, /*allow_move=*/true, data_buf) < 0) {
      EPRINTF("failed to resize input queue buffer to %zu bytes\n", size);
      return -1;
    }

    *data_size = size;
    if (quote_buf != NULL) {
      kfreep(quote_buf);
      *quote_buf = kmallocz(quotesz * sizeof(uint32_t));
    }
  } else {
    void *buf = vmalloc(size, VM_RDWR);
    if (buf == NULL) {
      EPRINTF("failed to allocate input queue buffer of size %zu bytes\n", size);
      return -1;
    }

    *data_buf = (uintptr_t)buf;
    *data_size = size;

    if (quote_buf != NULL) {
      ASSERT(*quote_buf == NULL);
      *quote_buf = kmallocz(quotesz);
    }
  }
  return 0;
}

//
// MARK: Input Queue
//

#define INQ_QUOTE_GET(inq, pos) ((inq)->quote_buf[(pos) / 32] & (1U << ((pos) % 32)))
#define INQ_QUOTE_SET(inq, pos) ((inq)->quote_buf[(pos) / 32] |= (1U << ((pos) % 32)))
#define INQ_QUOTE_CLEAR(inq, pos) ((inq)->quote_buf[(pos) / 32] &= ~(1U << ((pos) % 32)))


struct ttyinq *ttyinq_alloc() {
  struct ttyinq *inq = kmallocz(sizeof(struct ttyinq));
  ASSERT(inq != NULL);
  return inq;
}

void ttyinq_free(struct ttyinq **inqp) {
  struct ttyinq *inq = moveptr(*inqp);
  ASSERT(inq != NULL);
  tty_queue_free((void **)&inq, &inq->data_buf, inq->data_size, &inq->quote_buf);
}

int ttyinq_setsize(struct ttyinq *inq, size_t size) {
  ASSERT(inq != NULL);
  return tty_queue_setsize(size, &inq->data_size, &inq->data_buf, &inq->quote_buf);
}

void ttyinq_flush(struct ttyinq *inq) {
  ASSERT(inq != NULL);
  inq->read_pos = 0;
  inq->write_pos = 0;
  inq->next_line = 0;
}

void ttyinq_canonizalize(struct ttyinq *inq) {
  ASSERT(inq != NULL);
  inq->next_line = inq->write_pos;
  DPRINTF("ttyinq_canonizalize: canonicalized input queue (read_pos=%u, write_pos=%u, next_line=%u)\n",
         inq->read_pos, inq->write_pos, inq->next_line);
}

size_t ttyinq_find_ch(struct ttyinq *inq, const char *chars, char *lastc) {
  ASSERT(inq != NULL);
  ASSERT(inq->data_size > 0);
  ASSERT(chars != NULL);

  size_t chars_len = strlen(chars);
  if (chars_len == 0) {
    return 0; // no characters to find
  }

  for (size_t i = inq->read_pos; i != inq->write_pos; i = (i + 1) % inq->data_size) {
    uintptr_t buf_addr = inq->data_buf + i;
    char ch = *(char *)buf_addr;

    for (size_t j = 0; j < chars_len; j++) {
      if (ch == chars[j]) {
        if (lastc != NULL) {
          *lastc = ch;
        }

        // return the length relative to the read position
        return i - inq->read_pos + 1; // inclusive of the found character
      }
    }
  }
  return 0;
}

int ttyinq_write_ch(struct ttyinq *inq, char ch, bool quote) {
  ASSERT(inq != NULL);
  ASSERT(inq->data_size > 0);
  DPRINTF("ttyinq_write_ch: writing character '%#c' (%#x) (quote=%d)\n", (char) ch, ch, quote);

  // check if we have space in the queue
  if ((inq->write_pos + 1) % inq->data_size == inq->read_pos) {
    // no space left, cannot write
    EPRINTF("input queue is full, cannot write character '%#c'\n", ch);
    return -1;
  }

  // write the character to the buffer
  uintptr_t buf_addr = inq->data_buf + inq->write_pos;
  *(char *)buf_addr = ch;
  inq->write_pos = (inq->write_pos + 1) % inq->data_size;
  if (quote) {
    INQ_QUOTE_SET(inq, inq->write_pos);
  } else {
    INQ_QUOTE_CLEAR(inq, inq->write_pos);
  }

  return 0;
}

ssize_t ttyinq_write(struct ttyinq *inq, kio_t *kio, bool quote) {
  ASSERT(inq != NULL);
  ASSERT(inq->data_size > 0);

  size_t bytes_written = 0;
  while (kio->size > 0 && (inq->write_pos + 1) % inq->data_size != inq->read_pos) {
    // write the next character to the queue
    char ch;
    if (kio_read_ch(&ch, kio) <= 0) {
      break; // no more characters to write
    }

    if (ttyinq_write_ch(inq, ch, quote) < 0) {
      return -1; // error writing to the queue
    }
    bytes_written++;
  }
  return (ssize_t) bytes_written;
}

ssize_t ttyinq_read(struct ttyinq *inq, kio_t *kio, size_t n) {
  ASSERT(inq != NULL);
  ASSERT(inq->data_size > 0);
  ASSERT(n > 0);

  size_t bytes_read = 0;
  while (kio_remaining(kio) > 0 && inq->read_pos != inq->write_pos && bytes_read < n) {
    // get the next character from the queue
    uintptr_t buf_addr = inq->data_buf + inq->read_pos;
    inq->read_pos = (inq->read_pos + 1) % inq->data_size;
    char ch = *((char *)buf_addr);
    if (kio_write_ch(kio, ch) < 0) {
      return (ssize_t) bytes_read; // no more space
    }
    bytes_read++;
  }
  return (ssize_t) bytes_read;
}

size_t ttyinq_drop(struct ttyinq *inq, size_t n) {
  ASSERT(inq != NULL);
  ASSERT(inq->data_size > 0);
  ASSERT(n > 0);

  size_t bytes_dropped = 0;
  while (n-- > 0 && inq->read_pos != inq->write_pos) {
    // drop the next character from the queue
    inq->read_pos = (inq->read_pos + 1) % inq->data_size;
    bytes_dropped++;
  }
  return bytes_dropped;
}

int ttyinq_del_ch(struct ttyinq *inq) {
  ASSERT(inq != NULL);
  ASSERT(inq->data_size > 0);
  
  // in canonical mode, we only delete from the current line being edited
  // (i.e., between read_pos and write_pos, but not past next_line)
  if (inq->write_pos == inq->read_pos) {
    return -1;
  }
  
  // check if the last character is quoted
  uint32_t last_pos = (inq->write_pos - 1 + inq->data_size) % inq->data_size;
  bool is_quoted = INQ_QUOTE_GET(inq, last_pos);
  
  if (is_quoted) {
    // quoted characters may be multi-byte sequences
    // we need to determine how many bytes to remove
    uintptr_t buf_addr = inq->data_buf + last_pos;
    char last_ch = *(char *)buf_addr;
    
    if ((unsigned)last_ch == 0xff) {
      // this could be part of a 0xff 0xff sequence or 0xff 0x00 <char> sequence
      // check if there's a preceding 0xff
      if (inq->write_pos >= 2) {
        uint32_t prev_pos = (inq->write_pos - 2 + inq->data_size) % inq->data_size;
        uintptr_t prev_buf_addr = inq->data_buf + prev_pos;
        char prev_ch = *(char *)prev_buf_addr;
        
        if ((unsigned)prev_ch == 0xff) {
          // this is a 0xff 0xff sequence, remove both bytes
          inq->write_pos = (inq->write_pos - 2 + inq->data_size) % inq->data_size;
          INQ_QUOTE_CLEAR(inq, last_pos);
          INQ_QUOTE_CLEAR(inq, prev_pos);
          DPRINTF("ttyinq_del_ch: deleted quoted 0xff 0xff sequence\n");
          return 0xff;
        }
      }
    } else {
      // this might be part of a 0xff 0x00 <char> sequence
      // check if there are two preceding bytes: 0xff 0x00
      if (inq->write_pos >= 3) {
        uint32_t prev_pos1 = (inq->write_pos - 2 + inq->data_size) % inq->data_size;
        uint32_t prev_pos2 = (inq->write_pos - 3 + inq->data_size) % inq->data_size;
        uintptr_t prev_buf_addr1 = inq->data_buf + prev_pos1;
        uintptr_t prev_buf_addr2 = inq->data_buf + prev_pos2;
        char prev_ch1 = *(char *)prev_buf_addr1;
        char prev_ch2 = *(char *)prev_buf_addr2;
        
        if ((unsigned)prev_ch2 == 0xff && prev_ch1 == 0x00) {
          // this is a 0xff 0x00 <char> sequence, remove all three bytes
          inq->write_pos = (inq->write_pos - 3 + inq->data_size) % inq->data_size;
          INQ_QUOTE_CLEAR(inq, last_pos);
          INQ_QUOTE_CLEAR(inq, prev_pos1);
          INQ_QUOTE_CLEAR(inq, prev_pos2);
          DPRINTF("ttyinq_del_ch: deleted quoted 0xff 0x00 <char> sequence\n");
          return (int) last_ch;
        }
      }
    }
  }
  
  // default case: delete a single character
  char last_ch = *(char *)(inq->data_buf + last_pos);
  inq->write_pos = (inq->write_pos - 1 + inq->data_size) % inq->data_size;
  INQ_QUOTE_CLEAR(inq, inq->write_pos);
  return (int) last_ch;
}


//
// MARK: Output Queue
//

struct ttyoutq *ttyoutq_alloc() {
  struct ttyoutq *outq = kmallocz(sizeof(struct ttyoutq));
  ASSERT(outq != NULL);
  return outq;
}

void ttyoutq_free(struct ttyoutq **outqp) {
  struct ttyoutq *outq = moveptr(*outqp);
  ASSERT(outq != NULL);
  tty_queue_free((void **)&outq, &outq->data_buf, outq->data_size, NULL);
}

int ttyoutq_setsize(struct ttyoutq *outq, size_t size) {
  ASSERT(outq != NULL);
  return tty_queue_setsize(size, &outq->data_size, &outq->data_buf, NULL);
}

int ttyoutq_peek_ch(struct ttyoutq *outq) {
  ASSERT(outq->data_size > 0);

  if (outq->read_pos == outq->write_pos) {
    // queue is empty
    return -1;
  }

  // read the character from the buffer without removing it
  uintptr_t buf_addr = outq->data_buf + outq->read_pos;
  return *(char *)buf_addr;
}

void ttyoutq_flush(struct ttyoutq *outq) {
  ASSERT(outq != NULL);

  outq->read_pos = 0;
  outq->write_pos = 0;
}

int ttyoutq_get_ch(struct ttyoutq *outq) {
  ASSERT(outq->data_size > 0);
  if (outq->read_pos == outq->write_pos) {
    // queue is empty
    return -1;
  }

  // read the character from the buffer and advance the read position
  uintptr_t buf_addr = outq->data_buf + outq->read_pos;
  int ch = (int) *(char *)buf_addr;
  outq->read_pos = (outq->read_pos + 1) % outq->data_size;
  return ch;
}

int ttyoutq_read(struct ttyoutq *outq, kio_t *kio) {
  ASSERT(outq != NULL);
  ASSERT(outq->data_size > 0);

  size_t n = kio_remaining(kio);
  while (n > 0 && outq->read_pos != outq->write_pos) {
    // get the next character from the queue
    uintptr_t buf_addr = outq->data_buf + outq->read_pos;
    char ch = *(char *)buf_addr;
    outq->read_pos = (outq->read_pos + 1) % outq->data_size;

    if (kio_write_ch(kio, ch) < 0) {
      return -1; // error writing to kio
    }
    n--;
  }

  if (n == 0 && outq->read_pos != outq->write_pos) {
    // we still have characters left in the queue, but kio is full
    return -EAGAIN;
  }
  return 0;
}

int ttyoutq_write_ch(struct ttyoutq *outq, char ch) {
  ASSERT(outq != NULL);
  ASSERT(outq->data_size > 0);
  DPRINTF("ttyoutq_write_ch: writing character '%#c' (%#x)\n", (char) ch, ch);

  // check if we have space in the queue
  if ((outq->write_pos + 1) % outq->data_size == outq->read_pos) {
    // no space left, cannot write
    EPRINTF("output queue is full, cannot write character '%#c'\n", ch);
    return -1;
  }

  // write the character to the buffer
  uintptr_t buf_addr = outq->data_buf + outq->write_pos;
  *(char *)buf_addr = ch;
  outq->write_pos = (outq->write_pos + 1) % outq->data_size;

  return 0;
}

int ttyoutq_write(struct ttyoutq *outq, kio_t *kio) {
  ASSERT(outq != NULL);
  ASSERT(outq->data_size > 0)

  size_t bytes_written = 0;
  while (kio->size > 0 && (outq->write_pos + 1) % outq->data_size != outq->read_pos) {
    // write the next character to the queue
    char ch;
    if (kio_read_ch(&ch, kio) <= 0) {
      break; // no more characters to write
    }

    if (ttyoutq_write_ch(outq, ch) < 0) {
      return -1; // error writing to the queue
    }
    bytes_written++;
  }

  if (bytes_written == 0 && outq->write_pos != outq->read_pos) {
    // we still have characters left in the kio, but the queue is full
    return -EAGAIN;
  }
  return 0;
}
