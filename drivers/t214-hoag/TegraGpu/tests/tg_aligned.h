/* SPDX-License-Identifier: MIT */
/* Host-test helper: aligned_alloc() is not in the MSVC CRT (clang on Windows uses it);
 * memory from _aligned_malloc() must be released with _aligned_free(), not free(). */
#ifndef TG_TESTS_ALIGNED_H
#define TG_TESTS_ALIGNED_H
#include <stdlib.h>
#ifdef _WIN32
#include <malloc.h>
#define tg_aligned_alloc(align, size) _aligned_malloc((size), (align))
#define tg_aligned_free(p) _aligned_free(p)
#else
#define tg_aligned_alloc(align, size) aligned_alloc((align), (size))
#define tg_aligned_free(p) free(p)
#endif
#endif
