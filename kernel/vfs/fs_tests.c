//
// Created by Aaron Gill-Braun on 2023-06-06.
//

#include <fs.h>
#include <mm.h>

#include <vfs/ventry.h>
#include <vfs/vnode.h>
#include <vfs/vfs.h>

#include <printf.h>
#include <panic.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("vfs: %s: " fmt, __func__, ##__VA_ARGS__)

#define EXPECT(x) \
  if (!(x)) { \
    kprintf("vfs: %s: test failed: %s\n", __func__, #x); \
    kassert(x); \
  }


static void test_fs_mount() {
  fs_type_t *ramfs = fs_get_type("ramfs");
  EXPECT(ramfs != NULL);

  vnode_t *root_vn = vn_alloc_empty(V_DIR);
  root_vn->state = V_ALIVE;
  ventry_t *root_ve = ve_alloc_linked(cstr_make("/"), root_vn);
}




//
//

static void (*test_cases[])() = {
  test_fs_mount,
};

void fs_run_tests() {
  for (int i = 0; i < ARRAY_SIZE(test_cases); i++) {
    test_cases[i]();
  }
}
