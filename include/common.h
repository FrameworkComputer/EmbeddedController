/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* common.h - Common includes for Chrome EC */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#ifndef __CROS_EC_COMMON_H
#define __CROS_EC_COMMON_H

#include "compile_time_macros.h"

#include <inttypes.h>
#include <stdint.h>

#ifdef CONFIG_ZEPHYR
#include "fpu.h"

#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>
#ifdef CONFIG_TEST
#define TEST_BUILD
#endif /* CONFIG_ZTEST */
#endif /* CONFIG_ZEPHYR */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 37

#ifndef __THROW
#define __THROW
#endif

/*
 * Define a new macro (FIXED_SECTION) to abstract away the linker details
 * between platform/ec builds and Zephyr. Each build has a slightly different
 * way of ensuring that the given section is in the same relative location in
 * both the RO/RW images.
 */
#if defined(CONFIG_ZEPHYR) && !defined(CONFIG_SOC_FAMILY_INTEL_ISH)
#define FIXED_SECTION(name) __attribute__((section(".fixed." name)))
#else
#define FIXED_SECTION(name) __attribute__((section(".rodata." name)))
#endif

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
#define CONCAT_STAGE_1(w, x, y, z) w##x##y##z
#define CONCAT2(w, x) CONCAT_STAGE_1(w, x, , )
#define CONCAT3(w, x, y) CONCAT_STAGE_1(w, x, y, )
#define CONCAT4(w, x, y, z) CONCAT_STAGE_1(w, x, y, z)

/*
 * Macros to turn the argument into a string constant.
 *
 * Compared to directly using the preprocessor # operator, this 2-stage macro
 * is safe with regards to using nested macros and defined arguments.
 */
#ifndef CONFIG_ZEPHYR
#define STRINGIFY0(name) #name
#define STRINGIFY(name) STRINGIFY0(name)
#endif /* CONFIG_ZEPHYR */

/* Macros to access registers */
#define REG64_ADDR(addr) ((volatile uint64_t *)(addr))
#define REG32_ADDR(addr) ((volatile uint32_t *)(addr))
#define REG16_ADDR(addr) ((volatile uint16_t *)(addr))
#define REG8_ADDR(addr) ((volatile uint8_t *)(addr))

#define REG64(addr) (*REG64_ADDR(addr))
#define REG32(addr) (*REG32_ADDR(addr))
#define REG16(addr) (*REG16_ADDR(addr))
#define REG8(addr) (*REG8_ADDR(addr))

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
#define __keep __attribute__((used))
#endif

/*
 * Place a read-only object into a ROM resident section. If supported by the
 * EC chip, the object is part of the flash image but not copied into RAM
 * automatically. Users may only access the data using the include/init_rom.h
 * module.
 *
 * Requires CONFIG_CHIP_INIT_ROM_REGION is defined, otherwise the object is
 * linked into the .rodata section.
 */
#ifndef __init_rom
#ifndef CONFIG_ZEPHYR
#define __init_rom __attribute__((section(".init.rom")))
#else
#define __init_rom
#endif
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
#define __overridable __attribute__((weak))

/*
 * Mark a symbol that is provided by a precompiled static library, without
 * source code.
 */
#define __staticlib extern

/*
 * Mark a symbol that is defined purely as a hook to be used by a static
 * library.
 */
#define __staticlib_hook __unused

/*
 * Attribute that will generate a compiler warning if the return value is not
 * used.
 */
#define __warn_unused_result __attribute__((warn_unused_result))

/**
 * @brief Attribute used to annotate intentional fallthrough between switch
 * labels.
 *
 * See https://clang.llvm.org/docs/AttributeReference.html#fallthrough and
 * https://gcc.gnu.org/onlinedocs/gcc/Statement-Attributes.html.
 */
#ifndef __fallthrough
#define __fallthrough __attribute__((fallthrough))
#endif

/**
 * @brief Attribute used to annotate functions that will never return, like
 * panic.
 *
 * See https://clang.llvm.org/docs/AttributeReference.html#noreturn-noreturn and
 * https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-noreturn-function-attribute.
 */
#define __noreturn __attribute__((noreturn))

/*
 * Macros for combining bytes into larger integers. _LE and _BE signify little
 * and big endian versions respectively.
 */
#define UINT16_FROM_BYTES(lsb, msb) ((lsb) | (msb) << 8)
#define UINT16_FROM_BYTE_ARRAY_LE(data, lsb_index) \
	UINT16_FROM_BYTES((data)[(lsb_index)], (data)[(lsb_index) + 1])
#define UINT16_FROM_BYTE_ARRAY_BE(data, msb_index) \
	UINT16_FROM_BYTES((data)[(msb_index) + 1], (data)[(msb_index)])

#define UINT32_FROM_BYTES(lsb, byte1, byte2, msb) \
	((lsb) | (byte1) << 8 | (byte2) << 16 | (msb) << 24)
#define UINT32_FROM_BYTE_ARRAY_LE(data, lsb_index)                      \
	UINT32_FROM_BYTES((data)[(lsb_index)], (data)[(lsb_index) + 1], \
			  (data)[(lsb_index) + 2], (data)[(lsb_index) + 3])
#define UINT32_FROM_BYTE_ARRAY_BE(data, msb_index)                          \
	UINT32_FROM_BYTES((data)[(msb_index) + 3], (data)[(msb_index) + 2], \
			  (data)[(msb_index) + 1], (data)[(msb_index)])

/* There isn't really a better place for this */
#define C_TO_K(temp_c) ((temp_c) + 273)
#define K_TO_C(temp_c) ((temp_c)-273)
/*
 * round_divide is part of math_utils, so you may need to import math_utils.h
 * and link math_utils.o if you use the following macros.
 */
#define CELSIUS_TO_DECI_KELVIN(temp_c) \
	(round_divide(CELSIUS_TO_MILLI_KELVIN(temp_c), 100))
#define DECI_KELVIN_TO_CELSIUS(temp_dk) \
	(MILLI_KELVIN_TO_CELSIUS((temp_dk) * 100))
#define MILLI_KELVIN_TO_MILLI_CELSIUS(temp_mk) ((temp_mk)-273150)
#define MILLI_CELSIUS_TO_MILLI_KELVIN(temp_mc) ((temp_mc) + 273150)
#define MILLI_KELVIN_TO_KELVIN(temp_mk) (round_divide((temp_mk), 1000))
#define KELVIN_TO_MILLI_KELVIN(temp_k) ((temp_k) * 1000)
#define CELSIUS_TO_MILLI_KELVIN(temp_c) \
	(MILLI_CELSIUS_TO_MILLI_KELVIN((temp_c) * 1000))
#define MILLI_KELVIN_TO_CELSIUS(temp_mk) \
	(round_divide(MILLI_KELVIN_TO_MILLI_CELSIUS(temp_mk), 1000))

/* Calculate a value with error margin considered. For example,
 * TARGET_WITH_MARGIN(X, 5) returns X' where X' * 100.5% is almost equal to
 * but does not exceed X. */
#define TARGET_WITH_MARGIN(target, tenths_percent) \
	(((target) * 1000) / (1000 + (tenths_percent)))

/* Call a function, and return the error value unless it returns EC_SUCCESS. */
#define RETURN_ERROR(fn)                 \
	do {                             \
		int error = (fn);        \
		if (error != EC_SUCCESS) \
			return error;    \
	} while (0)

/*
 * Define test_mockable and test_mockable_static for mocking
 * functions. Don't use test_mockable in .h files.
 */
#ifdef TEST_BUILD
#define test_mockable __attribute__((weak))
#define test_mockable_static __attribute__((weak))
#define test_mockable_static_inline __attribute__((weak))
/*
 * Tests implemented with ztest add mock implementations that actually return,
 * so they should not be marked "noreturn". See
 * test/drivers/default/src/panic_output.c.
 */
#ifdef CONFIG_ZTEST
#define test_mockable_noreturn __attribute__((weak))
#define test_mockable_static_noreturn __attribute__((weak))
#else
#define test_mockable_noreturn __noreturn __attribute__((weak))
#define test_mockable_static_noreturn __noreturn __attribute__((weak))
#endif
#define test_export_static
#define test_overridable_const
#else
#define test_mockable
#define test_mockable_static static
#define test_mockable_static_inline static inline
#define test_mockable_noreturn __noreturn
#define test_mockable_static_noreturn static __noreturn
#define test_export_static static
#define test_overridable_const const
#endif

/* Include top-level configuration file */
#include "config.h"

/*
 * When CONFIG_CHIP_DATA_IN_INIT_ROM is enabled the .data section is linked
 * into an unused are of flash and excluded from the executable portion of
 * the RO and RW images to save space.
 *
 * The __const_data attribute can be used to force constant data objects
 * into the .data section instead of the .rodata section for additional
 * savings.
 */
#ifdef CONFIG_CHIP_DATA_IN_INIT_ROM
#define __const_data __attribute__((section(".data#")))
#else
#define __const_data
#endif

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

	/* Operation was successful but completion is pending. */
	EC_SUCCESS_IN_PROGRESS = 27,

	/* No response available */
	EC_ERROR_UNAVAILABLE = 28,

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
 * Attribute to define functions to only be used in test code, causing
 * a compiler error if used without TEST_BUILD defined.
 *
 * Example usage (add to prototype in header):
 * __test_only void foo(void);
 */
#ifdef TEST_BUILD
#define __test_only
#else
#define __test_only __error("This function should only be used by tests")
#endif

/*
 * Mark functions that collide with stdlib so they can be hidden when linking
 * against libraries that require stdlib.
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
#define __cfg_select(cfg, empty, otherwise) \
	__cfg_select_1(cfg, empty, otherwise)
#define __cfg_select_placeholder_ _,
#define __cfg_select_1(value, empty, otherwise) \
	__cfg_select_2(__cfg_select_placeholder_##value, empty, otherwise)
#define __cfg_select_2(arg1_or_junk, empty, otherwise) \
	__cfg_select_3(arg1_or_junk _, empty, otherwise)
#define __cfg_select_3(_ignore1, _ignore2, select, ...) select

/*
 * This version concatenates a BUILD_ASSERT(...); before OTHERWISE,
 * handling the __builtin_strcmp trickery where a BUILD_ASSERT is
 * appropriate in the context.
 */
#define __cfg_select_build_assert(cfg, value, empty, undef)            \
	__cfg_select(value, empty,                                     \
		     BUILD_ASSERT(__builtin_strcmp(cfg, #value) == 0); \
		     undef)

/*
 * Attribute for generating an error if a function is used.
 *
 * Clang does not have a function attribute to do this. Rely on linker
 * errors. :(
 */
#ifdef __clang__
#define __error(msg) __attribute__((section("/DISCARD/")))
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
#define __config_enabled(cfg, value)                                           \
	__cfg_select(value, 1, ({                                              \
			     int __undefined =                                 \
				     __builtin_strcmp(cfg, #value) == 0;       \
			     extern int IS_ENABLED_BAD_ARGS(void) __error(     \
				     cfg " must be <blank>, or not defined."); \
			     if (!__undefined)                                 \
				     IS_ENABLED_BAD_ARGS();                    \
			     0;                                                \
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
#ifndef CONFIG_ZEPHYR
#define IS_ENABLED(option) __config_enabled(#option, option)
#else
/* IS_ENABLED previously defined in sys/util.h */
#undef IS_ENABLED
/*
 * For Zephyr, we must create a new version of IS_ENABLED which is
 * compatible with both Kconfig enables (for Zephyr code), which have
 * the value defined to 1 upon enablement, and CrOS EC defines (which
 * are defined to the empty string).
 *
 * To do this, we use __cfg_select from this codebase to determine if
 * the option was defined to nothing ("enabled" in CrOS EC terms).  If
 * not, we then check using Zephyr's Z_IS_ENABLED1 macro to determine
 * if the config option is enabled by Zephyr's definition.
 */
#define IS_ENABLED(option) __cfg_select(option, 1, Z_IS_ENABLED1(option))
#endif /* CONFIG_ZEPHYR */

/**
 * Makes a global variable static when a config option is enabled,
 * extern otherwise (with the intention to cause linker errors if the
 * variable is used outside of a config context, for example thru
 * IS_ENABLED, that it should be).
 *
 * This follows the same constraints as IS_ENABLED, the config option
 * should be defined to nothing or undefined.
 */
#ifndef CONFIG_ZEPHYR
#define STATIC_IF(option) \
	__cfg_select_build_assert(#option, option, static, extern)
#else
/*
 * Version of STATIC_IF for Zephyr, with similar considerations to IS_ENABLED.
 *
 * Note, if __cfg_select fails, then we check using Zephyr's COND_CODE_1 macro
 * to determine if the config option is enabled by Zephyr's definition.
 */
#define STATIC_IF(option) \
	__cfg_select(option, static, COND_CODE_1(option, (static), (extern)))
#endif /* CONFIG_ZEPHYR */

/**
 * STATIC_IF_NOT is just like STATIC_IF, but makes the variable static
 * only if the config option is *not* defined, extern if it is.
 *
 * This is to assert that a variable will go unused with a certain
 * config option.
 */
#ifndef CONFIG_ZEPHYR
#define STATIC_IF_NOT(option) \
	__cfg_select_build_assert(#option, option, extern, static)
#else
/*
 * Version of STATIC_IF_NOT for Zephyr, with similar considerations to STATIC_IF
 * and IS_ENABLED.
 */
#define STATIC_IF_NOT(option) \
	__cfg_select(option, extern, COND_CODE_1(option, (extern), (static)))
#endif /* CONFIG_ZEPHYR */

#endif /* __CROS_EC_COMMON_H */
