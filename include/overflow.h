/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_OVERFLOW_H
#define __CROS_EC_OVERFLOW_H

#include "compiler.h"

/*
 * __builtin_add_overflow, __builtin_sub_overflow and __builtin_mul_overflow
 * were added in gcc 5.1: https://gcc.gnu.org/gcc-5/changes.html
 */
#if TOOLCHAIN_GCC_VERSION > 50100
#define COMPILER_HAS_GENERIC_BUILTIN_OVERFLOW 1
#endif

/*
 * __has_builtin available in
 * clang 10 and newer: https://clang.llvm.org/docs/LanguageExtensions.html
 */
#ifdef __clang__
#if __has_builtin(__builtin_add_overflow) &&     \
	__has_builtin(__builtin_sub_overflow) && \
	__has_builtin(__builtin_mul_overflow)
#define COMPILER_HAS_GENERIC_BUILTIN_OVERFLOW 1
#endif
#endif /* __clang__ */

#include "third_party/linux/overflow.h"

#endif /* __CROS_EC_OVERFLOW_H */
