/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_IOEXPANDER_H
#define __CROS_EC_IOEXPANDER_H

/* IO expander signal definition structure */
struct ioex_info {
	/* Signal name */
	const char *name;

	/* IO expander port number */
	uint16_t ioex;

	/* IO port number in IO expander */
	uint16_t port;

	/* Bitmask on that port (1 << N) */
	uint32_t mask;

	/* Flags - the same as the GPIO flags */
	uint32_t flags;
};

/* Signal information from board.c.  Must match order from enum ioex_signal. */
extern const struct ioex_info ioex_list[];
extern void (* const ioex_irq_handlers[])(enum ioex_signal signal);
extern const int ioex_ih_count;

struct ioexpander_drv {
	/* Initialize IO expander chip/driver */
	int (*init)(int ioex);
	/* Get the current level of the IOEX pin */
	int (*get_level)(int ioex, int port, int mask, int *val);
	/* Set the level of the IOEX pin */
	int (*set_level)(int ioex, int port, int mask, int val);
	/* Get flags for the IOEX pin */
	int (*get_flags_by_mask)(int ioex, int port, int mask, int *flags);
	/* Set flags for the IOEX pin */
	int (*set_flags_by_mask)(int ioex, int port, int mask, int flags);
	/* Enable/disable interrupt for the IOEX pin */
	int (*enable_interrupt)(int ioex, int port, int mask, int enable);
};

/* IO expander chip disabled. No I2C communication will be attempted. */
#define IOEX_FLAGS_DISABLED	BIT(0)

struct ioexpander_config_t {
	/* Physical I2C port connects to the IO expander chip. */
	int i2c_host_port;
	/* I2C slave address */
	int i2c_slave_addr;
	/*
	 * Pointer to the specific IO expander chip's ops defined in
	 * the struct ioexpander_drv.
	 */
	const struct ioexpander_drv *drv;
	/* Config flags for this IO expander chip. See IOEX_FLAGS_* */
	uint32_t flags;
};

extern struct ioexpander_config_t ioex_config[];

/*
 * Enable the interrupt for the IOEX signal
 *
 * @param signal	IOEX signal to enable the interrupt
 * @return			EC_SUCCESS if successful, non-zero if error.
 */
int ioex_enable_interrupt(enum ioex_signal signal);

/*
 * Disable the interrupt for the IOEX signal
 *
 * @param signal	IOEX signal to disable the interrupt
 * @return			EC_SUCCESS if successful, non-zero if error.
 */
int ioex_disable_interrupt(enum ioex_signal signal);

/*
 * Get flags for the IOEX signal
 *
 * @param signal	IOEX signal to get flags for
 * @param flags		Pointer to the keep the flags read
 * @return			EC_SUCCESS if successful, non-zero if error.
 */
int ioex_get_flags(enum ioex_signal signal, int *flags);

/*
 * Set flags for the IOEX signal
 *
 * @param signal	IOEX signal to set flags for
 * @param flags		New flags for the IOEX signal
 * @return			EC_SUCCESS if successful, non-zero if error.
 */
int ioex_set_flags(enum ioex_signal signal, int flags);

/*
 * Get the current level of the IOEX signal
 *
 * @param signal	IOEX signal to get the level
 * @param val		Pointer to the keep the level read
 * @return			EC_SUCCESS if successful, non-zero if error.
 */
int ioex_get_level(enum ioex_signal signal, int *val);

/*
 * Set the level of the IOEX signal
 *
 * @param signal	IOEX signal to set the level
 * @param value		New level for the IOEX signal
 * @return			EC_SUCCESS if successful, non-zero if error.
 */
int ioex_set_level(enum ioex_signal signal, int value);

/*
 * Initialize IO expander chip/driver
 *
 * @param ioex		IO expander chip's port number
 * @return			EC_SUCCESS if successful, non-zero if error.
 */
int ioex_init(int ioex);

/*
 * Get the name for the IOEX signal
 *
 * @param signal	IOEX signal to get the name
 * @returns name of the given IOEX signal
 */
const char *ioex_get_name(enum ioex_signal signal);

/*
 * Check if signal is an IO expander signal or GPIO signal.
 *
 * @param signal	GPIO or IOEX signal
 * @return		1 if signal is IOEX else return 0
 */
int signal_is_ioex(int signal);

#endif /* __CROS_EC_IOEXPANDER_H */
