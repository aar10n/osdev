//
// Created by Aaron Gill-Braun on 2023-12-19.
//

#include <kernel/cpu/tcb.h>


#include <kernel/mm.h>
#include <kernel/panic.h>


struct fpu_area *fpu_area_alloc() {
  struct fpu_area *fpa = kmallocz(sizeof(struct fpu_area));
  return fpa;
}

void fpu_area_free(struct fpu_area **pfpa) {
  if (*pfpa == NULL) {
    kfree(*pfpa);
    *pfpa = NULL;
  }
}

//

struct tcb *tcb_alloc(int flags) {
  struct tcb *tcb = kmallocz(sizeof(struct tcb));
  if (flags & TCB_FPU) {
    tcb->fpu = fpu_area_alloc();
  }
  tcb->tcb_flags = flags;
  return tcb;
}

void tcb_free(struct tcb **ptcb) {
  if (*ptcb == NULL) {
    return;
  }

  struct tcb *tcb = *ptcb;
  if (tcb->tcb_flags & TCB_FPU) {
    fpu_area_free(&tcb->fpu);
  }
  kfree(tcb);
  *ptcb = NULL;
}
