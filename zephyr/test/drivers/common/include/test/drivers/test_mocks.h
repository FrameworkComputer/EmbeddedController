/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __TEST_DRIVERS_TEST_MOCKS_H
#define __TEST_DRIVERS_TEST_MOCKS_H

#include <stdint.h>

#include <zephyr/fff.h>

/*
 * Convenience macros
 */

/**
 * @brief  Helper macro for inspecting the argument history of a given
 *         fake. Counts number of times the fake was called with a given
 *         argument.
 * @param  FAKE - FFF-provided fake structure (no pointers).
 * @param  ARG_NUM - Zero-based index of the argument to compare.
 * @param  VAL - Expression the argument must equal.
 * @return Returns the number of times a call was made to the fake
 *         where the argument `ARG_NUM` equals `VAL`.
 */
#define MOCK_COUNT_CALLS_WITH_ARG_VALUE(FAKE, ARG_NUM, VAL)              \
	({                                                               \
		int count = 0;                                           \
		for (int i = 0; i < (FAKE).call_count; i++) {            \
			if ((FAKE).arg##ARG_NUM##_history[i] == (VAL)) { \
				count++;                                 \
			}                                                \
		}                                                        \
		count;                                                   \
	})

/**
 * @brief  Helper macro for asserting that a certain register write occurred.
 *         Used when wrapping an I2C emulator mock write function in FFF. Prints
 *         useful error messages when the assertion fails.
 * @param  FAKE - name of the fake whose arg history to insepct. Do not include
 *          '_fake' at the end.
 * @param  CALL_NUM - Index in to the call history that this write should have
 *          occurred at. Zero based.
 * @param  EXPECTED_REG - The register address that was supposed to be written.
 * @param  EXPECTED_VAL - The 8-bit value that was supposed to be written, or
 *          `MOCK_IGNORE_VALUE` to suppress this check.
 */
#define MOCK_ASSERT_I2C_WRITE(FAKE, CALL_NUM, EXPECTED_REG, EXPECTED_VAL)                   \
	do {                                                                                \
		zassert_true((CALL_NUM) < FAKE##_fake.call_count,                           \
			     "Call #%d did not occur (%d I2C writes total)",                \
			     (CALL_NUM), FAKE##_fake.call_count);                           \
		zassert_equal(                                                              \
			FAKE##_fake.arg1_history[(CALL_NUM)], (EXPECTED_REG),               \
			"Expected I2C write #%d to register 0x%02x (" #EXPECTED_REG         \
			") but wrote to reg 0x%02x",                                        \
			(CALL_NUM), (EXPECTED_REG),                                         \
			FAKE##_fake.arg1_history[(CALL_NUM)]);                              \
		if ((EXPECTED_VAL) != MOCK_IGNORE_VALUE) {                                  \
			zassert_equal(                                                      \
				FAKE##_fake.arg2_history[(CALL_NUM)],                       \
				(EXPECTED_VAL),                                             \
				"Expected I2C write #%d to register 0x%02x (" #EXPECTED_REG \
				") to write 0x%02x (" #EXPECTED_VAL                         \
				") but wrote 0x%02x",                                       \
				(CALL_NUM), (EXPECTED_REG), (EXPECTED_VAL),                 \
				FAKE##_fake.arg2_history[(CALL_NUM)]);                      \
		}                                                                           \
	} while (0)

/** @brief Value to pass to MOCK_ASSERT_I2C_WRITE to ignore the actual value
 *         written.
 */
#define MOCK_IGNORE_VALUE (-1)

/**
 * @brief  Helper macro for asserting that a certain register read occurred.
 *         Used when wrapping an I2C emulator mock read function in FFF. Prints
 *         useful error messages when the assertion fails.
 * @param  FAKE - name of the fake whose arg history to insepct. Do not include
 *          '_fake' at the end.
 * @param  CALL_NUM - Index in to the call history that this write should have
 *          occurred at. Zero based.
 * @param  EXPECTED_REG - The register address that was supposed to be read
 *          from.
 */
#define MOCK_ASSERT_I2C_READ(FAKE, CALL_NUM, EXPECTED_REG)                           \
	do {                                                                         \
		zassert_true((CALL_NUM) < FAKE##_fake.call_count,                    \
			     "Call #%d did not occur (%d I2C reads total)",          \
			     (CALL_NUM), FAKE##_fake.call_count);                    \
		zassert_equal(                                                       \
			FAKE##_fake.arg1_history[(CALL_NUM)], (EXPECTED_REG),        \
			"Expected I2C read #%d from register 0x%02x (" #EXPECTED_REG \
			") but read from reg 0x%02x",                                \
			(CALL_NUM), (EXPECTED_REG),                                  \
			FAKE##_fake.arg1_history[(CALL_NUM)]);                       \
	} while (0)

/*
 * Mock declarations
 */

/* Mocks for common/init_rom.c */
DECLARE_FAKE_VALUE_FUNC(const void *, init_rom_map, const void *, int);
DECLARE_FAKE_VOID_FUNC(init_rom_unmap, const void *, int);
DECLARE_FAKE_VALUE_FUNC(int, init_rom_copy, int, int, int);

/* Mocks for common/system.c */
DECLARE_FAKE_VALUE_FUNC(int, system_jumped_late);
DECLARE_FAKE_VALUE_FUNC(int, system_is_locked);
DECLARE_FAKE_VOID_FUNC(system_reset, int);
DECLARE_FAKE_VOID_FUNC(software_panic, uint32_t, uint32_t);
DECLARE_FAKE_VOID_FUNC(assert_post_action, const char *, unsigned int);

/* Mocks for common/lid_angle.c */
DECLARE_FAKE_VOID_FUNC(lid_angle_peripheral_enable, int);

/* Mocks for gpio.h */
DECLARE_FAKE_VALUE_FUNC(int, gpio_config_unused_pins);
DECLARE_FAKE_VALUE_FUNC(int, gpio_configure_port_pin, int, int, int);

/* Mocks for drivers */
DECLARE_FAKE_VALUE_FUNC(int, ppc_get_alert_status, int);

#endif /* __TEST_DRIVERS_TEST_MOCKS_H */
