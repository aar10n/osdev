//
// Created by Aaron Gill-Braun on 2020-10-30.
//

#ifndef LIB_MURMUR3_H
#define LIB_MURMUR3_H

#include <base.h>

void murmur_hash_x86_32(const void *key, int len, uint32_t seed, void *out);
void murmur_hash_x86_128(const void *key, int len, uint32_t seed, void *out);
void murmur_hash_x64_128(const void *key, int len, uint32_t seed, void *out);

static inline uint32_t murmur_hash32(const void *key, int len, uint32_t seed) {
  uint32_t hash = 0;
  murmur_hash_x86_32(key, len, seed, &hash);
  return hash;
}

#endif
