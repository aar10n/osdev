//
// Created by Aaron Gill-Braun on 2025-07-23.
//

#ifndef INCLUDE_ABI_KEVENT_H
#define INCLUDE_ABI_KEVENT_H

struct kevent {
  uintptr_t   ident;    // identifier for event
  int16_t     filter;   // filter for event
  uint16_t    flags;    // action flags
  uint32_t    fflags;   // filter flags
  intptr_t    data;     // filter data
  void        *udata;   // user data
};


// event filters
#define EVFILT_READ         (-1)    // descriptor readable
#define EVFILT_WRITE        (-2)    // descriptor writable
#define EVFILT_AIO          (-3)    // aio requests
#define EVFILT_VNODE        (-4)    // vnode events
#define EVFILT_PROC         (-5)    // process events
#define EVFILT_SIGNAL       (-6)    // signals
#define EVFILT_TIMER        (-7)    // timers
#define EVFILT_PROCDESC     (-8)    // process descriptor events
#define EVFILT_FS           (-9)    // filesystem events
#define EVFILT_LIO          (-10)   // lio events
#define EVFILT_USER         (-11)   // user events
#define EVFILT_SENDFILE     (-12)   // sendfile events
#define EVFILT_EMPTY        (-13)   // empty kqueue
#define NEVFILT             14      // number of filters

// actions
#define EV_ADD              0x0001  // add event to kqueue
#define EV_DELETE           0x0002  // delete event from kqueue
#define EV_ENABLE           0x0004  // enable event
#define EV_DISABLE          0x0008  // disable event (not reported)
#define EV_FORCEONESHOT     0x0100  // force EV_ONESHOT on all events

// flags
#define EV_ONESHOT          0x0010  // only report once
#define EV_CLEAR            0x0020  // clear event state after reporting
#define EV_RECEIPT          0x0040  // force immediate return with EV_ERROR
#define EV_DISPATCH         0x0080  // disable after reporting
#define EV_SYSFLAGS         0xF000  // reserved by system
#define EV_DROP             0x1000  // drop kevent
#define EV_FLAG1            0x2000  // filter-specific flag
#define EV_FLAG2            0x4000  // filter-specific flag

// returned values
#define EV_EOF              0x8000  // EOF detected
#define EV_ERROR            0x4000  // error, data contains errno

// filter flags for EVFILT_READ
#define NOTE_LOWAT          0x0001  // low water mark

// filter flags for EVFILT_VNODE
#define NOTE_DELETE         0x0001  // vnode was removed
#define NOTE_WRITE          0x0002  // vnode was written to
#define NOTE_EXTEND         0x0004  // vnode was extended
#define NOTE_ATTRIB         0x0008  // attributes changed
#define NOTE_LINK           0x0010  // link count changed
#define NOTE_RENAME         0x0020  // vnode was renamed
#define NOTE_REVOKE         0x0040  // vnode access was revoked
#define NOTE_OPEN           0x0080  // vnode was opened
#define NOTE_CLOSE          0x0100  // file closed
#define NOTE_CLOSE_WRITE    0x0200  // file closed after writing
#define NOTE_READ           0x0400  // file was read

// filter flags for EVFILT_PROC
#define NOTE_EXIT           0x80000000  // process exited
#define NOTE_FORK           0x40000000  // process forked
#define NOTE_EXEC           0x20000000  // process exec'd
#define NOTE_PCTRLMASK      0xf0000000  // mask for hint bits
#define NOTE_PDATAMASK      0x000fffff  // mask for pid/signal
#define NOTE_TRACK          0x00000001  // follow across forks
#define NOTE_TRACKERR       0x00000002  // could not track child
#define NOTE_CHILD          0x00000004  // am a child process

// filter flags for EVFILT_TIMER
#define NOTE_SECONDS        0x00000001  // data is seconds
#define NOTE_MSECONDS       0x00000002  // data is milliseconds
#define NOTE_USECONDS       0x00000004  // data is microseconds
#define NOTE_NSECONDS       0x00000008  // data is nanoseconds
#define NOTE_ABSTIME        0x00000010  // absolute time

// filter flags for EVFILT_USER
#define NOTE_TRIGGER        0x01000000  // trigger for output
#define NOTE_FFNOP          0x00000000  // ignore input fflags
#define NOTE_FFAND          0x40000000  // and fflags
#define NOTE_FFOR           0x80000000  // or fflags
#define NOTE_FFCOPY         0xc0000000  // copy fflags
#define NOTE_FFCTRLMASK     0xc0000000  // control mask
#define NOTE_FFLAGSMASK     0x00ffffff  // user defined flags

// helper macro to initialize kevent structures
#define EV_SET(kevp, a, b, c, d, e, f) do {    \
   struct kevent *__kevp = (kevp);             \
   __kevp->ident = (a);                        \
   __kevp->filter = (b);                       \
   __kevp->flags = (c);                        \
   __kevp->fflags = (d);                       \
   __kevp->data = (e);                         \
   __kevp->udata = (f);                        \
} while(0)

#endif
