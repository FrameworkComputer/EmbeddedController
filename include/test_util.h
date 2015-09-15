/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Various utility for unit testing */

#ifndef __CROS_EC_TEST_UTIL_H
#define __CROS_EC_TEST_UTIL_H

#include "common.h"
#include "console.h"
#include "stack_trace.h"

#define RUN_TEST(n) \
	do { \
		ccprintf("Running %s...", #n); \
		cflush(); \
		if (n() == EC_SUCCESS) { \
			ccputs("OK\n"); \
		} else { \
			ccputs("Fail\n"); \
			__test_error_count++; \
		} \
	} while (0)

#define TEST_ASSERT(n) \
	do { \
		if (!(n)) { \
			ccprintf("%d: ASSERTION failed: %s\n", __LINE__, #n); \
			task_dump_trace(); \
			return EC_ERROR_UNKNOWN; \
		} \
	} while (0)

#define __ABS(n) ((n) > 0 ? (n) : -(n))

#define TEST_ASSERT_ABS_LESS(n, t) \
	do { \
		if (__ABS(n) >= t) { \
			ccprintf("%d: ASSERT_ABS_LESS failed: abs(%d) is " \
				 "not less than %d\n", __LINE__, n, t); \
			task_dump_trace(); \
			return EC_ERROR_UNKNOWN; \
		} \
	} while (0)

#define TEST_ASSERT_ARRAY_EQ(s, d, n) \
	do { \
		int __i; \
		for (__i = 0; __i < n; ++__i) \
			if ((s)[__i] != (d)[__i]) { \
				ccprintf("%d: ASSERT_ARRAY_EQ failed at " \
					 "index=%d: %d != %d\n", __LINE__, \
					 __i, (int)(s)[__i], (int)(d)[__i]); \
				task_dump_trace(); \
				return EC_ERROR_UNKNOWN; \
			} \
	} while (0)

#define TEST_ASSERT_MEMSET(d, c, n) \
	do { \
		int __i; \
		for (__i = 0; __i < n; ++__i) \
			if ((d)[__i] != (c)) { \
				ccprintf("%d: ASSERT_MEMSET failed at " \
					 "index=%d: %d != %d\n", __LINE__, \
					 __i, (int)(d)[__i], (c)); \
				task_dump_trace(); \
				return EC_ERROR_UNKNOWN; \
			} \
	} while (0)

#define TEST_CHECK(n) \
	do { \
		if (n) \
			return EC_SUCCESS; \
		else \
			return EC_ERROR_UNKNOWN; \
	} while (0)

/* Mutlistep test states */
enum test_state_t {
	TEST_STATE_STEP_1 = 0,
	TEST_STATE_STEP_2,
	TEST_STATE_STEP_3,
	TEST_STATE_STEP_4,
	TEST_STATE_STEP_5,
	TEST_STATE_STEP_6,
	TEST_STATE_STEP_7,
	TEST_STATE_STEP_8,
	TEST_STATE_STEP_9,
	TEST_STATE_PASSED,
	TEST_STATE_FAILED,
};
#define TEST_STATE_MASK(x) (1 << (x))

/* Hooks gcov_flush() for test coverage report generation */
void register_test_end_hook(void);

/*
 * Test initialization. This is called after all _pre_init() calls and before
 * all _init() calls.
 */
void test_init(void);

/* Test entry point */
void run_test(void);

/* Resets test error count */
void test_reset(void);

/* Reports test pass */
void test_pass(void);

/* Reports test failure */
void test_fail(void);

/* Prints test result, including number of failed tests */
void test_print_result(void);

/* Returns the number of failed tests */
int test_get_error_count(void);

/* Simulates host command sent from the host */
int test_send_host_command(int command, int version, const void *params,
			   int params_size, void *resp, int resp_size);

/* Optionally defined interrupt generator entry point */
void interrupt_generator(void);

/*
 * Trigger an interrupt. This function must only be called by interrupt
 * generator.
 */
void task_trigger_test_interrupt(void (*isr)(void));

/*
 * Special implementation of udelay() for interrupt generator. Calls
 * to udelay() from interrupt generator are delegated to this function
 * automatically.
 */
void interrupt_generator_udelay(unsigned us);

#ifdef EMU_BUILD
void wait_for_task_started(void);
#else
static inline void wait_for_task_started(void) { }
#endif

uint32_t prng(uint32_t seed);

uint32_t prng_no_seed(void);

/* Number of failed tests */
extern int __test_error_count;

/* Simulates UART input */
void uart_inject_char(char *s, int sz);

#define UART_INJECT(s) uart_inject_char(s, strlen(s));

/* Simulates chipset power on */
void test_chipset_on(void);

/* Simulates chipset power off */
void test_chipset_off(void);

/* Start/stop capturing console output */
void test_capture_console(int enabled);

/* Get captured console output */
const char *test_get_captured_console(void);

/*
 * Flush emulator status. Must be called before emulator reboots or
 * exits.
 */
void emulator_flush(void);

/*
 * Entry point of multi-step test.
 *
 * Depending on current test state, this function runs the corresponding
 * test step. This function should be called in a dedicated task on every
 * reboot. Also, run_test() is responsible for starting the test by kicking
 * that task.
 */
void test_run_multistep(void);

/*
 * A function that runs the test step specified in 'state'. This function
 * should be defined by all multi-step tests.
 *
 * @param state     TEST_STATE_MASK(x) indicating the step to run.
 */
void test_run_step(uint32_t state);

/* Get the current test state */
uint32_t test_get_state(void);

/*
 * Multistep test clean up. If a multi-step test has this function defined,
 * it will be called on test end. (i.e. when test passes or fails.)
 */
void test_clean_up(void);

/* Set the next step and reboot */
void test_reboot_to_next_step(enum test_state_t step);

struct test_i2c_read_string_dev {
	/* I2C string read handler */
	int (*routine)(int port, int slave_addr, int offset, uint8_t *data,
		       int len);
};

struct test_i2c_xfer {
	/* I2C xfer handler */
	int (*routine)(int port, int slave_addr,
		       const uint8_t *out, int out_size,
		       uint8_t *in, int in_size, int flags);
};

struct test_i2c_write_dev {
	/* I2C write handler */
	int (*routine)(int port, int slave_addr, int offset, int data);
};

/**
 * Register an I2C 8-bit read function.
 *
 * When this function is called, it should either perform the desired
 * mock functionality, or return EC_ERROR_INVAL to indicate it does
 * not respond to the specified port and slave address.
 *
 * @param routine     Function pointer, with the same prototype as i2c_xfer()
 */
#define DECLARE_TEST_I2C_XFER(routine)					\
	const struct test_i2c_xfer __test_i2c_xfer_##routine	\
	__attribute__((section(".rodata.test_i2c.xfer")))		\
		= {routine}

/*
 * Detach an I2C device. Once detached, any read/write command regarding the
 * specified port and slave address returns error.
 *
 * @param port       The port that the detached device is connected to
 * @param slave_addr The address of the detached device
 * @return EC_SUCCESS if detached; EC_ERROR_OVERFLOW if too many devices are
 *         detached.
 */
int test_detach_i2c(int port, int slave_addr);

/*
 * Re-attach an I2C device.
 *
 * @param port       The port that the detached device is connected to
 * @param slave_addr The address of the detached device
 * @return EC_SUCCESS if re-attached; EC_ERROR_INVAL if the specified device
 *         is not a detached device.
 */
int test_attach_i2c(int port, int slave_addr);

#endif /* __CROS_EC_TEST_UTIL_H */
