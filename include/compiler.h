/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_COMPILER_H
#define __CROS_EC_COMPILER_H

/*
 * See https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html
 */
#define __GCC_VERSION \
	(__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

/*
 * If TOOLCHAIN_GCC_VERSION is defined, check if it equals to __GCC_VERSION
 * value expected by EC code. Otherwise, define it as __GCC_VERSION.
 *
 * Zephyr defines TOOLCHAIN_GCC_VERSION.
 */
#ifdef TOOLCHAIN_GCC_VERSION
#if TOOLCHAIN_GCC_VERSION != __GCC_VERSION
#error "Zephyr TOOLCHAIN_GCC_VERSION is not equal to __GCC_VERSION"
#endif
#else
#define TOOLCHAIN_GCC_VERSION __GCC_VERSION
#endif /* TOOLCHAIN_GCC_VERSION */

/*
 * The EC codebase assumes that typeof() is available but it is not in Zephyr.
 * We use an #ifdef since arch/arm/include/aarch32/cortex_m/cmse.h defines this
 * macro.
 */
#ifndef typeof
#define typeof(x) __typeof__(x)
#endif

/**
 * ISO C forbids forward references to enum types, but gcc allows it as long as
 * the "-pedantic" flag is not used.
 *
 * In C++11 and newer, forward references to enums are only allowed if the
 * underlying type is specified.
 *
 * New uses of this macro are strongly discouraged; instead of forward
 * declaring the enum, provide the definition.
 *
 * TODO(http://b/187105190): Remove uses of this macro.
 */
#ifdef __cplusplus
#define FORWARD_DECLARE_ENUM(x) enum x : int
#else
#define FORWARD_DECLARE_ENUM(x) enum x
#endif /* __cplusplus */

#define _DO_PRAGMA(x) _Pragma(#x)

#define _DISABLE_COMPILER_WARNING(compiler, warning) \
	_DO_PRAGMA(compiler diagnostic push)         \
	_DO_PRAGMA(compiler diagnostic ignored warning)

#define _ENABLE_COMPILER_WARNING(compiler, warning) \
	_DO_PRAGMA(compiler diagnostic pop)

/**
 * Disable the specified compiler warning for both clang and gcc.
 */
#define DISABLE_COMPILER_WARNING(warning) \
	_DISABLE_COMPILER_WARNING(GCC, warning)

/**
 * Re-enable the specified compiler warning for both clang and gcc. Can only be
 * used after a call to DISABLE_COMPILER_WARNING.
 */
#define ENABLE_COMPILER_WARNING(warning) _ENABLE_COMPILER_WARNING(GCC, warning)

#ifdef __clang__
/**
 * Disable the specified compiler warning for clang.
 */
#define DISABLE_CLANG_WARNING(warning) _DISABLE_COMPILER_WARNING(clang, warning)
/**
 * Re-enable the specified compiler warning for clang. Can only be used after
 * a call to DISABLE_CLANG_WARNING.
 */
#define ENABLE_CLANG_WARNING(warning) _ENABLE_COMPILER_WARNING(clang, warning)
#else
#define DISABLE_CLANG_WARNING(warning)
#define ENABLE_CLANG_WARNING(warning)
#endif

#if defined(__GNUC__) && !defined(__clang__)
/**
 * Disable the specified compiler warning for gcc.
 */
#define DISABLE_GCC_WARNING(warning) _DISABLE_COMPILER_WARNING(GCC, warning)
/**
 * Re-enable the specified compiler warning for clang. Can only be used after
 * a call to DISABLE_GCC_WARNING.
 */
#define ENABLE_GCC_WARNING(warning) _ENABLE_COMPILER_WARNING(GCC, warning)
#else
#define DISABLE_GCC_WARNING(warning)
#define ENABLE_GCC_WARNING(warning)
#endif

#endif /* __CROS_EC_COMPILER_H */
