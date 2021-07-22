//
// Created by Aaron Gill-Braun on 2020-11-13.
//

#include <loader.h>
#include <cpu/cpu.h>
#include <thread.h>
#include <mm.h>
#include <elf.h>
#include <elf64.h>
#include <printf.h>
#include <string.h>


int load_elf_segment(Elf64_Phdr *pheader, void *buf) {
  uint64_t mem_size = pheader->p_memsz;
  uint64_t file_size = pheader->p_filesz;
  uint64_t vaddr = pheader->p_vaddr;
  uint64_t offset = pheader->p_offset;
  uint32_t hdr_flags = pheader->p_flags;

  kprintf("header type: %d\n", pheader->p_type);
  kprintf("mem size: %u\n", mem_size);
  kprintf("file size: %u\n", file_size);
  kprintf("vaddr: %p\n", vaddr);
  kprintf("offset: %p\n", offset);
  kprintf("flags: %u\n\n", hdr_flags);

  uint16_t flags = PE_USER;
  if (hdr_flags & PF_X)
    flags |= PE_EXEC;
  if (hdr_flags & PF_W)
    flags |= PE_WRITE;

  page_t *pages = alloc_frames(SIZE_TO_PAGES(mem_size), flags);
  void *addr = vm_map_page_vaddr(vaddr, pages);

  // disable write protection just long enough for us to
  // copy over and zero the allocated pages even if the
  // pages are not marked as writable
  uint64_t cr0 = read_cr0();
  write_cr0(cr0 & ~(1 << 16)); // disable cr0.WP

  memcpy(addr, offset_ptr(buf, offset), file_size);
  memset(offset_ptr(addr, file_size), 0, mem_size - file_size);

  write_cr0(cr0); // re-enable cr0.WP
  return 0;
}

int load_elf(void *buf, void **entry) {
  Elf64_Ehdr *elf = buf;
  if (!IS_ELF(*elf)) {
    ERRNO = ENOEXEC;
    return -1;
  }

  Elf64_Phdr *phead = offset_ptr(buf, elf->e_phoff);
  for (uint32_t i = 0; i < elf->e_phnum; i++) {
    if (phead[i].p_type == PT_LOAD) {
      if (load_elf_segment(&(phead[i]), buf) < 0) {
        return -1;
      }
    }
  }

  *entry = (void *) elf->e_entry;
  return 0;
}
