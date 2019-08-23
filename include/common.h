/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* common.h - Common includes for Chrome EC */

#ifndef __CROS_EC_COMMON_H
#define __CROS_EC_COMMON_H

#include <stdint.h>
#include "compile_time_macros.h"

/*
 * Macros to concatenate 2 - 4 tokens together to form a single token.
 * Multiple levels of nesting are required to convince the preprocessor to
 * expand currently-defined tokens before concatenation.
 *
 * For example, if you have
 *     #define FOO 1
 *     #define BAR1 42
 * Then
 *     #define BAZ CONCAT2(BAR, FOO)
 * Will evaluate to BAR1, which then evaluates to 42.
 */
#define CONCAT_STAGE_1(w, x, y, z) w ## x ## y ## z
#define CONCAT2(w, x) CONCAT_STAGE_1(w, x, , )
#define CONCAT3(w, x, y) CONCAT_STAGE_1(w, x, y, )
#define CONCAT4(w, x, y, z) CONCAT_STAGE_1(w, x, y, z)

/*
 * Macros to turn the argument into a string constant.
 *
 * Compared to directly using the preprocessor # operator, this 2-stage macro
 * is safe with regards to using nested macros and defined arguments.
 */
#define STRINGIFY0(name)  #name
#define STRINGIFY(name)  STRINGIFY0(name)

/* Macros to access registers */
#define REG64_ADDR(addr) ((volatile uint64_t *)(addr))
#define REG32_ADDR(addr) ((volatile uint32_t *)(addr))
#define REG16_ADDR(addr) ((volatile uint16_t *)(addr))
#define REG8_ADDR(addr)  ((volatile uint8_t  *)(addr))

#define REG64(addr) (*REG64_ADDR(addr))
#define REG32(addr) (*REG32_ADDR(addr))
#define REG16(addr) (*REG16_ADDR(addr))
#define REG8(addr)  (*REG8_ADDR(addr))

/*
 * Define __aligned(n) and __packed if someone hasn't beat us to it.  Linux
 * kernel style checking prefers these over __attribute__((packed)) and
 * __attribute__((aligned(n))).
 */
#ifndef __aligned
#define __aligned(n) __attribute__((aligned(n)))
#endif

#ifndef __packed
#define __packed __attribute__((packed))
#endif

/*
 * Define __unused in the same manner.
 */
#ifndef __unused
#define __unused __attribute__((unused))
#endif

/*
 * __maybe_unused is equivalent to the Linux kernel definition, so we
 * can follow the Kernel style guide more closely.
 *
 * An example use case is a function which is only used under certain
 * CONFIG options.
 */
#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

/*
 * externally_visible is required by GCC to avoid kicking out memset.
 */
#ifndef __visible
#ifndef __clang__
#define __visible __attribute__((externally_visible))
#else
#define __visible __attribute__((used))
#endif
#endif

/*
 * Force the toolchain to keep a symbol even with Link Time Optimization
 * activated.
 *
 * Useful for C functions called only from assembly or through special sections.
 */
#ifndef __keep
#define __keep __attribute__((used)) __visible
#endif

/*
 * Place the object in the .bss.slow region.
 *
 * On boards with unoptimized RAM there is no penalty and it simply is appended
 * to the .bss section.
 */
#ifndef __bss_slow
#define __bss_slow __attribute__((section(".bss.slow")))
#endif

/* gcc does not support __has_feature */
#ifndef __has_feature
#define __has_feature(x) 0
#endif

/*
 * Use this to prevent AddressSanitizer from putting guards around some global
 * variables (e.g. hook/commands "arrays" that are put together at link time).
 */
#ifndef __no_sanitize_address
#if __has_feature(address_sanitizer)
#define __no_sanitize_address __attribute__((no_sanitize("address")))
#else
#define __no_sanitize_address
#endif
#endif

/* There isn't really a better place for this */
#define C_TO_K(temp_c) ((temp_c) + 273)
#define K_TO_C(temp_c) ((temp_c) - 273)
#define CELSIUS_TO_DECI_KELVIN(temp_c) ((temp_c) * 10 + 2731)
#define DECI_KELVIN_TO_CELSIUS(temp_dk) ((temp_dk - 2731) / 10)

/* Calculate a value with error margin considered. For example,
 * TARGET_WITH_MARGIN(X, 5) returns X' where X' * 100.5% is almost equal to
 * but does not exceed X. */
#define TARGET_WITH_MARGIN(target, tenths_percent) \
	(((target) * 1000) / (1000 + (tenths_percent)))

/* Include top-level configuration file */
#include "config.h"

/* Canonical list of module IDs */
#include "module_id.h"

/* List of common error codes that can be returned */
enum ec_error_list {
	/* Success - no error */
	EC_SUCCESS = 0,
	/* Unknown error */
	EC_ERROR_UNKNOWN = 1,
	/* Function not implemented yet */
	EC_ERROR_UNIMPLEMENTED = 2,
	/* Overflow error; too much input provided. */
	EC_ERROR_OVERFLOW = 3,
	/* Timeout */
	EC_ERROR_TIMEOUT = 4,
	/* Invalid argument */
	EC_ERROR_INVAL = 5,
	/* Already in use, or not ready yet */
	EC_ERROR_BUSY = 6,
	/* Access denied */
	EC_ERROR_ACCESS_DENIED = 7,
	/* Failed because component does not have power */
	EC_ERROR_NOT_POWERED = 8,
	/* Failed because component is not calibrated */
	EC_ERROR_NOT_CALIBRATED = 9,
	/* Failed because CRC error */
	EC_ERROR_CRC = 10,
	/* Invalid console command param (PARAMn means parameter n is bad) */
	EC_ERROR_PARAM1 = 11,
	EC_ERROR_PARAM2 = 12,
	EC_ERROR_PARAM3 = 13,
	EC_ERROR_PARAM4 = 14,
	EC_ERROR_PARAM5 = 15,
	EC_ERROR_PARAM6 = 16,
	EC_ERROR_PARAM7 = 17,
	EC_ERROR_PARAM8 = 18,
	EC_ERROR_PARAM9 = 19,
	/* Wrong number of params */
	EC_ERROR_PARAM_COUNT = 20,
	/* Interrupt event not handled */
	EC_ERROR_NOT_HANDLED = 21,
	/* Data has not changed */
	EC_ERROR_UNCHANGED = 22,
	/* Memory allocation */
	EC_ERROR_MEMORY_ALLOCATION = 23,
	/* Invalid to configure in the current module mode/stage */
	EC_ERROR_INVALID_CONFIG = 24,
	/* something wrong in a HW */
	EC_ERROR_HW_INTERNAL = 25,

	/* Sometimes operation is expected to have to be repeated. */
	EC_ERROR_TRY_AGAIN = 26,

	/* Verified boot errors */
	EC_ERROR_VBOOT_SIGNATURE = 0x1000, /* 4096 */
	EC_ERROR_VBOOT_SIG_MAGIC = 0x1001,
	EC_ERROR_VBOOT_SIG_SIZE = 0x1002,
	EC_ERROR_VBOOT_SIG_ALGORITHM = 0x1003,
	EC_ERROR_VBOOT_HASH_ALGORITHM = 0x1004,
	EC_ERROR_VBOOT_SIG_OFFSET = 0x1005,
	EC_ERROR_VBOOT_DATA_SIZE = 0x1006,

	/* Verified boot key errors */
	EC_ERROR_VBOOT_KEY = 0x1100,
	EC_ERROR_VBOOT_KEY_MAGIC = 0x1101,
	EC_ERROR_VBOOT_KEY_SIZE = 0x1102,

	/* Verified boot data errors */
	EC_ERROR_VBOOT_DATA = 0x1200,
	EC_ERROR_VBOOT_DATA_VERIFY = 0x1201,

	/* Module-internal error codes may use this range.   */
	EC_ERROR_INTERNAL_FIRST = 0x10000,
	EC_ERROR_INTERNAL_LAST = 0x1FFFF
};

/*
 * Define test_mockable and test_mockable_static for mocking
 * functions.
 */
#ifdef TEST_BUILD
#define test_mockable __attribute__((weak))
#define test_mockable_static __attribute__((weak))
#define test_mockable_static_inline __attribute__((weak))
#define test_export_static
#else
#define test_mockable
#define test_mockable_static static
#define test_mockable_static_inline static inline
#define test_export_static static
#endif

/*
 * Weak symbol markers
 *
 * These macros are used to annotate weak definitions, their declarations, and
 * overriding definitions.
 *
 * __override_proto: declarations
 * __override: definitions which take precedence
 * __overridable: default (weak) definitions
 *
 * For example, in foo.h:
 *   __override_proto void foo(void);
 *
 * and in foo.c:
 *   __overridable void foo(void) {
 *     ...
 *   }
 *
 * and in board.c:
 *   __override void foo(void) {
 *     ...
 *   }
 */
#define __override_proto
#define __override
#define __overridable	__attribute__((weak))

/*
 * Mark functions that collide with stdlib so they can be hidden when linking
 * against libraries that require stdlib. HIDE_EC_STDLIB should be defined
 * before including common.h from code that links to cstdlib.
 */
#ifdef TEST_FUZZ
#define __stdlib_compat __attribute__((visibility("hidden")))
#else /* TEST_FUZZ */
#define __stdlib_compat
#endif /* TEST_FUZZ */

/* find the most significant bit. Not defined in n == 0. */
#define __fls(n) (31 - __builtin_clz(n))

/*
 * __cfg_select(CONFIG_NAME, EMPTY, OTHERWISE) is a macro used for
 * defining other macros which conditionally select code based on a
 * config option. It will generate the argument passed as EMPTY
 * when CONFIG_NAME was defined to the empty string, and OTHERWISE
 * when the argument was not defined or defined to something
 * non-empty.
 *
 * Generally speaking, macros which use this should make some sort of
 * context-dependent assertion in OTHERWISE that CONFIG_NAME is
 * undefined, rather than defined to something else. This usually
 * involves tricks with __builtin_strcmp.
 */
#define __cfg_select(cfg, empty, otherwise)     \
	__cfg_select_1(cfg, empty, otherwise)
#define __cfg_select_placeholder_ _,
#define __cfg_select_1(value, empty, otherwise)			  \
	__cfg_select_2(__cfg_select_placeholder_##value, empty, otherwise)
#define __cfg_select_2(arg1_or_junk, empty, otherwise)		\
	__cfg_select_3(arg1_or_junk _, empty, otherwise)
#define __cfg_select_3(_ignore1, _ignore2, select, ...) select

/*
 * This version concatenates a BUILD_ASSERT(...); before OTHERWISE,
 * handling the __builtin_strcmp trickery where a BUILD_ASSERT is
 * appropriate in the context.
 */
#define __cfg_select_build_assert(cfg, value, empty, undef)	\
	__cfg_select(						\
		value,						\
		empty,						\
		BUILD_ASSERT(					\
			__builtin_strcmp(cfg, #value) == 0);	\
		undef)

/*
 * Attribute for generating an error if a function is used.
 *
 * Clang does not have a function attribute to do this. Rely on linker
 * errors. :(
 */
#ifdef __clang__
#define __error(msg)
#else
#define __error(msg) __attribute__((error(msg)))
#endif

/*
 * Getting something that works in C and CPP for an arg that may or may
 * not be defined is tricky.
 *
 * Compare the option name with the value string in the OTHERWISE to
 * __cfg_select. If they are identical we assume that the value was
 * undefined and return 0. If the value happens to be anything else we
 * call an undefined method that will raise a compiler error. This
 * technique requires that the optimizer be enabled so it can remove
 * the undefined function call.
 */
#define __config_enabled(cfg, value)					      \
	__cfg_select(							      \
		value, 1, ({						      \
			int __undefined = __builtin_strcmp(cfg, #value) == 0; \
			extern int IS_ENABLED_BAD_ARGS(void) __error(	      \
				cfg " must be <blank>, or not defined.");     \
			if (!__undefined)				      \
				IS_ENABLED_BAD_ARGS();			      \
			0;						      \
		}))

/**
 * Checks if a config option is enabled or disabled
 *
 * Enabled examples:
 *     #define CONFIG_FOO
 *
 * Disabled examples:
 *     #undef CONFIG_FOO
 *
 * If the option is defined to any value a compiler error will be thrown.
 *
 * Note: This macro will only function inside a code block due to the way
 * it checks for unknown values.
 */
#define IS_ENABLED(option) __config_enabled(#option, option)

/**
 * Makes a global variable static when a config option is enabled,
 * extern otherwise (with the intention to cause linker errors if the
 * variable is used outside of a config context, for example thru
 * IS_ENABLED, that it should be).
 *
 * This follows the same constraints as IS_ENABLED, the config option
 * should be defined to nothing or undefined.
 */
#define STATIC_IF(option)						\
	__cfg_select_build_assert(#option, option, static, extern)

/**
 * STATIC_IF_NOT is just like STATIC_IF, but makes the variable static
 * only if the config option is *not* defined, extern if it is.
 *
 * This is to assert that a variable will go unused with a certain
 * config option.
 */
#define STATIC_IF_NOT(option)						\
	__cfg_select_build_assert(#option, option, extern, static)

#endif  /* __CROS_EC_COMMON_H */
