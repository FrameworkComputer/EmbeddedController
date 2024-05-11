/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Button API for Chrome EC */

#ifndef __CROS_EC_BUTTON_H
#define __CROS_EC_BUTTON_H

#include "common.h"
#include "compile_time_macros.h"
#include "ec_commands.h"
#include "gpio_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_FLAG_ACTIVE_HIGH BIT(0)
#define BUTTON_FLAG_DISABLED BIT(1) /* Button disabled */

#define BUTTON_DEBOUNCE_US CONFIG_BUTTON_DEBOUNCE

struct button_config {
	const char *name;
	enum keyboard_button_type type;
	enum gpio_signal gpio;
	uint32_t debounce_us;
	int flags;
};

enum button {
#ifdef CONFIG_VOLUME_BUTTONS
	BUTTON_VOLUME_UP,
	BUTTON_VOLUME_DOWN,
#endif /* defined(CONFIG_VOLUME_BUTTONS) */
#ifdef CONFIG_DEDICATED_RECOVERY_BUTTON
	BUTTON_RECOVERY,
#ifdef CONFIG_DEDICATED_RECOVERY_BUTTON_2
	BUTTON_RECOVERY_2,
#endif /* defined(CONFIG_DEDICATED_RECOVERY_BUTTON_2) */
#endif /* defined(CONFIG_DEDICATED_RECOVERY_BUTTON) */
	BUTTON_COUNT,
};

/* Table of buttons for the board. */
#ifndef CONFIG_BUTTONS_RUNTIME_CONFIG
extern const struct button_config buttons[];
#else
extern struct button_config buttons[];
#endif

/*
 * Buttons used to decide whether recovery is requested or not
 */
extern const struct button_config *recovery_buttons[];
extern const int recovery_buttons_count;

/*
 * Button initialization, called from main.
 */
void button_init(void);

/*
 * Reassign a button GPIO signal at runtime.
 *
 * @param button_type	Button type to reassign
 * @param gpio		GPIO to assign to the button
 *
 * Returns EC_SUCCESS if button change is accepted and made active,
 * EC_ERROR_* otherwise.
 */
int button_reassign_gpio(enum button button_type, enum gpio_signal gpio);

/*
 * Disable a button GPIO signal at runtime.
 *
 * @param button_type	Button type to reassign
 *
 * Returns EC_SUCCESS if the button is disabled,
 * EC_ERROR_* otherwise.
 */
int button_disable_gpio(enum button button_type);

/*
 * Interrupt handler for button.
 *
 * @param signal	Signal which triggered the interrupt.
 */
void button_interrupt(enum gpio_signal signal);

/*
 * Is this button using ADC voltages to detect state?
 *
 * @param gpio	The GPIO of interest.
 * Returns 1 if button state is detected by ADC, 0 if not.
 */
int button_is_adc_detected(enum gpio_signal gpio);

/*
 * Sample the ADC voltage and convert to a physical pressed/not pressed state.
 *
 * @param gpio	ADC detected GPIO.
 * Returns the physical state of the button.
 */
int adc_to_physical_value(enum gpio_signal gpio);

/**
 * Get states of buttons pressed on POR.
 *
 * @return button states where bit positions correspond to enum button.
 */
uint32_t button_get_boot_button(void);

/* Public for testing purposes only, undocumented. */
enum debug_state {
	STATE_DEBUG_NONE,
	STATE_DEBUG_CHECK,
	STATE_STAGING,
	STATE_DEBUG_MODE_ACTIVE,
	STATE_SYSRQ_PATH,
	STATE_WARM_RESET_PATH,
	STATE_SYSRQ_EXEC,
	STATE_WARM_RESET_EXEC,
};

__test_only void reset_button_debug_state(void);

__test_only enum debug_state get_button_debug_state(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_BUTTON_H */
