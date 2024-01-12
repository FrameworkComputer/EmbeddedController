/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "signal_gpio.h"
#include "system.h"

#include <zephyr/drivers/gpio.h>

#include <power_signals.h>

#define DT_DRV_COMPAT intel_ap_pwrseq_gpio

#define INIT_GPIO_SPEC(inst) GPIO_DT_SPEC_INST_GET(inst, gpios),

const static struct gpio_dt_spec spec[] = { DT_INST_FOREACH_STATUS_OKAY(
	INIT_GPIO_SPEC) };

/*
 * Configuration for GPIO inputs.
 */
struct ps_gpio_int {
	gpio_flags_t flags;
	uint8_t signal;
	unsigned int output : 1;
	unsigned int no_enable : 1;
	unsigned int reset_val : 1;
};

#define INIT_GPIO_CONFIG(inst)                                      \
	{                                                           \
		.flags = DT_INST_PROP_OR(inst, interrupt_flags, 0), \
		.signal = PWR_DT_INST_SIGNAL_ENUM(inst),            \
		.no_enable = DT_INST_PROP(inst, no_enable),         \
		.output = DT_INST_PROP(inst, output),               \
		.reset_val = !!DT_INST_PROP(inst, reset_val),       \
	},

const static struct ps_gpio_int gpio_config[] = { DT_INST_FOREACH_STATUS_OKAY(
	INIT_GPIO_CONFIG) };

static struct gpio_callback int_cb[ARRAY_SIZE(gpio_config)];

int power_signal_gpio_enable(enum pwr_sig_gpio index)
{
	gpio_flags_t flags;

	if (index < 0 || index >= ARRAY_SIZE(gpio_config)) {
		return -EINVAL;
	}
	/*
	 * Do not allow interrupts on an output GPIO.
	 */
	if (gpio_config[index].output) {
		return -EINVAL;
	}
	flags = gpio_config[index].flags;

	/* Only enable if flags are present. */
	if (flags) {
		return gpio_pin_interrupt_configure_dt(&spec[index], flags);
	}
	return -EINVAL;
}

int power_signal_gpio_disable(enum pwr_sig_gpio index)
{
	gpio_flags_t flags;

	if (index < 0 || index >= ARRAY_SIZE(gpio_config)) {
		return -EINVAL;
	}
	/*
	 * Do not allow interrupts on an output GPIO.
	 */
	if (gpio_config[index].output) {
		return -EINVAL;
	}
	flags = gpio_config[index].flags;

	/* Disable if flags are present. */
	if (flags) {
		return gpio_pin_interrupt_configure_dt(&spec[index],
						       GPIO_INT_DISABLE);
	}
	return -EINVAL;
}

void power_signal_gpio_interrupt(const struct device *port,
				 struct gpio_callback *cb,
				 gpio_port_pins_t pins)
{
	int index = cb - int_cb;

	power_signal_interrupt(gpio_config[index].signal,
			       gpio_pin_get_dt(&spec[index]));
}

int power_signal_gpio_get(enum pwr_sig_gpio index)
{
	if (index < 0 || index >= ARRAY_SIZE(gpio_config)) {
		return -EINVAL;
	}
	/*
	 * Getting the current value of an output is
	 * done by retrieving the config and checking what the
	 * output state has been set to, not by reading the
	 * physical level of the pin (open drain outputs
	 * may have a low voltage).
	 */
	if (IS_ENABLED(CONFIG_GPIO_GET_CONFIG) && gpio_config[index].output) {
		int rv;
		gpio_flags_t flags;

		rv = gpio_pin_get_config_dt(&spec[index], &flags);
		if (rv == 0) {
			int pin = (flags & GPIO_OUTPUT_INIT_HIGH) ? 1 : 0;
			/* If active low signal, invert it */
			if (spec[index].dt_flags & GPIO_ACTIVE_LOW) {
				pin = !pin;
			}
			return pin;
		}
		/*
		 * -ENOSYS is returned when this API call is not supported,
		 *  so drop into the default method of returning the pin value.
		 */
		if (rv != -ENOSYS) {
			return rv;
		}
	}
	return gpio_pin_get_dt(&spec[index]);
}

int power_signal_gpio_set(enum pwr_sig_gpio index, int value)
{
	if (index < 0 || index >= ARRAY_SIZE(gpio_config)) {
		return -EINVAL;
	}
	if (!gpio_config[index].output) {
		return -EINVAL;
	}
	return gpio_pin_set_dt(&spec[index], value);
}
void power_signal_gpio_init(void)
{
	for (int i = 0; i < ARRAY_SIZE(gpio_config); i++) {
		if (gpio_config[i].output) {
			gpio_flags_t out_flags = GPIO_OUTPUT;
			/*
			 * If there has not been a sysjump, set the output pin
			 * to corresponding assertion reset value.
			 */
			if (!system_jumped_to_this_image()) {
				out_flags = gpio_config[i].reset_val ?
						    GPIO_OUTPUT_ACTIVE :
						    GPIO_OUTPUT_INACTIVE;
			}
			gpio_pin_configure_dt(&spec[i], out_flags);
		} else {
			gpio_pin_configure_dt(&spec[i], GPIO_INPUT);
			/* If interrupt, initialise it */
			if (gpio_config[i].flags) {
				gpio_init_callback(&int_cb[i],
						   power_signal_gpio_interrupt,
						   BIT(spec[i].pin));
				gpio_add_callback(spec[i].port, &int_cb[i]);
				/*
				 * If the interrupt is to be enabled at
				 * startup, enable the interrupt.
				 */
				if (!gpio_config[i].no_enable) {
					power_signal_gpio_enable(i);
				}
			}
		}
	}
}
