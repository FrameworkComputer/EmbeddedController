/*
 * Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_IOEXPANDER_H
#define __CROS_EC_IOEXPANDER_H

#ifdef CONFIG_ZEPHYR
#define ioex_signal gpio_signal
#include "gpio.h"
#else
enum ioex_signal; /* from gpio_signal.h */
#endif

#ifdef __cplusplus
extern "C" {
#endif

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
extern void (*const ioex_irq_handlers[])(enum ioex_signal signal);
extern const int ioex_ih_count;

/* Get ioex_info structure for specified signal */
#define IOEX_GET_INFO(signal) (ioex_list + (signal)-IOEX_SIGNAL_START)

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
#ifdef CONFIG_IO_EXPANDER_SUPPORT_GET_PORT
	/* Read levels for whole IOEX port */
	int (*get_port)(int ioex, int port, int *val);
#endif
};

/* IO expander default init disabled. No I2C communication will be attempted. */
#define IOEX_FLAGS_DEFAULT_INIT_DISABLED BIT(0)
/* IO Expander has been initialized */
#define IOEX_FLAGS_INITIALIZED BIT(1)

/*
 * BITS 24 to 31 are used by io-expander drivers that need to control multiple
 * devices
 */
#define IOEX_FLAGS_CUSTOM_BIT(x) BUILD_CHECK_INLINE(BIT(x), BIT(x) & 0xff000000)

struct ioexpander_config_t {
	/* Physical I2C port connects to the IO expander chip. */
	int i2c_host_port;
	/* I2C address */
	int i2c_addr_flags;
	/*
	 * Pointer to the specific IO expander chip's ops defined in
	 * the struct ioexpander_drv.
	 */
	const struct ioexpander_drv *drv;
	/* Config flags for this IO expander chip. See IOEX_FLAGS_* */
	uint32_t flags;
};

extern struct ioexpander_config_t ioex_config[];

#ifdef CONFIG_ZEPHYR

#define ioex_enable_interrupt gpio_enable_interrupt
#define ioex_disable_interrupt gpio_disable_interrupt

#ifdef CONFIG_GPIO_GET_EXTENDED
static inline int ioex_get_flags(enum gpio_signal signal, int *flags)
{
	*flags = gpio_get_flags(signal);
	return EC_SUCCESS;
}
#endif

static inline int ioex_set_flags(enum gpio_signal signal, int flags)
{
	gpio_set_flags(signal, flags);
	return EC_SUCCESS;
}

static inline int ioex_get_level(enum gpio_signal signal, int *val)
{
	*val = gpio_get_level(signal);
	return EC_SUCCESS;
}

static inline int ioex_set_level(enum gpio_signal signal, int val)
{
	gpio_set_level(signal, val);
	return EC_SUCCESS;
}

int ioex_init(int ioex);

static inline const char *ioex_get_name(enum ioex_signal signal)
{
	return gpio_get_name(signal);
}

#else

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
 * Get io expander flags (IOEX_FLAGS_*) for chip that specified IOEX signal
 * belongs to. They contain information if port was disabled or initialized.
 *
 * @param signal IOEX signal that belongs to chip which flags will be returned
 * @param val	 Pointer to memory where flags will be stored
 * @return	 EC_SUCCESS if successful, non-zero if error.
 */
int ioex_get_ioex_flags(enum ioex_signal signal, int *val);

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

#ifdef CONFIG_IO_EXPANDER_SUPPORT_GET_PORT
/*
 * Get the current levels on the IOEX port
 *
 * @param ioex		Number of I/O expander
 * @param port		Number of port in ioex
 * @param val		Pointer to variable where port will be read
 * @return			EC_SUCCESS if successful, non-zero if error.
 */
int ioex_get_port(int ioex, int port, int *val);
#endif

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

/*
 * Save gpio state of IO expander
 *
 * @param ioex		IO expander chip's port number
 * @param state		Buffer to hold gpio state
 * @param state_len	Length of state buffer, IOEX_COUNT is recommended
 * @return		EC_SUCCESS if successful, non-zero if error.
 */
int ioex_save_gpio_state(int ioex, int *state, int state_len);

/*
 * Restore gpio state of IO expander
 *
 * @param ioex		IO expander chip's port number
 * @param state		Buffer with gpio state saved by ioex_save_gpio_state
 * @param state_len	Length of state buffer, IOEX_COUNT is recommended
 * @return		EC_SUCCESS if successful, non-zero if error.
 */
int ioex_restore_gpio_state(int ioex, const int *state, int state_len);

#endif /* CONFIG_ZEPHYR */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_IOEXPANDER_H */
