/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "host_command.h"
#include "rgb_keyboard.h"
#include "stddef.h"
#include "timer.h"
#include "util.h"

#define WS2812_RGB_OUT_PIN GPIO_DT_FROM_NODELABEL(gpio_rgb_out)
unsigned int ws2812_num_leds = 0; /* we'll extend this at runtime, so we only send the bare minimum */
uint8_t ws2812_led_colors[EC_RGBKBD_MAX_KEY_COUNT][3];

void ws2812_send_buf(const struct gpio_dt_spec *rgb_out_spec, uint8_t *buf, size_t len)
{
	const struct device *rgb_out_gpio_port = rgb_out_spec->port;
	const gpio_port_pins_t rgb_out_mask = BIT(rgb_out_spec->pin);

	BUILD_ASSERT(CONFIG_GPIO_NPCX);
	/*
	 * General-Purpose I/O (GPIO) device registers
	 */
	struct gpio_reg {
		/* 0x000: Port GPIOx Data Out */
		volatile uint8_t PDOUT;
		/* 0x001: Port GPIOx Data In */
		volatile uint8_t PDIN;
		/* 0x002: Port GPIOx Direction */
		volatile uint8_t PDIR;
		/* 0x003: Port GPIOx Pull-Up or Pull-Down Enable */
		volatile uint8_t PPULL;
		/* 0x004: Port GPIOx Pull-Up/Down Selection */
		volatile uint8_t PPUD;
		/* 0x005: Port GPIOx Drive Enable by VDD Present */
		volatile uint8_t PENVDD;
		/* 0x006: Port GPIOx Output Type */
		volatile uint8_t PTYPE;
		/* 0x007: Port GPIOx Lock Control */
		volatile uint8_t PLOCK_CTL;
	};
	/* Driver config */
	struct gpio_npcx_config {
		/* gpio_driver_config needs to be first */
		struct gpio_driver_config common;
		/* GPIO controller base address */
		uintptr_t base;
		/* IO port */
		int port;
		/* .....
		 * Note: There's more stuff here,
		 * but we don't need it and it'll still align
		 */
	};
	#define HAL_INSTANCE(dev)                                                                          \
		((struct gpio_reg *)((const struct gpio_npcx_config *)(dev)->config)->base)
	struct gpio_reg *const inst = HAL_INSTANCE(rgb_out_gpio_port);
	volatile uint8_t *rgb_out_port_reg = &(inst->PDOUT);

	/* Reset */
	gpio_port_clear_bits_raw(rgb_out_gpio_port, rgb_out_mask);
	/* Let's let other tasks schedule first, reset needs to be at least 50us */
	crec_usleep(50);

	/* And right when we come back we'll
	   disable the interrupts and run the tight bitbang loop: */
	unsigned int irq_key = irq_lock();
	const uint8_t rgb_out_port_reg_masked_high = (*rgb_out_port_reg) | rgb_out_mask;
	const uint8_t rgb_out_port_reg_masked_low = (*rgb_out_port_reg) & (~rgb_out_mask);

	while (len--) {
		uint32_t b = *buf++;
		int32_t i;

		/*
		 * Generate signal out of the bits, MSbit first.
		 *
		 * Accumulator maintenance and branching mean the
		 * inter-bit time will be longer than TxL, but the
		 * wp.josh.com blog post says we have at least 5 usec
		 * of slack time between bits before we risk the
		 * signal getting latched, so this will be fine as
		 * long as the compiler does something minimally
		 * reasonable.
		 */

		/*
		 * T1H: 1 bit high pulse delay: 10 cycles == .66 usec
		 * T0H: 0 bit high pulse delay: 4 cycles == .25 usec
		 * TxL: inter-bit low pulse delay: it's supposed to be .5 usec, but
		 * we'll just rely more on the inter-loop time
		 *
		 * We can't use k_busy_wait() here: its argument is in microseconds,
		 * and we need roughly .05 microsecond resolution.
		 */
		#define DELAY_T1H "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
		#define DELAY_T0H "nop\nnop\nnop\nnop\n"
		#define DELAY_TxL "nop\nnop\nnop\nnop\n"

		for (i = 7; i >= 0; i--) {
			if (b & BIT(i)) {
				__asm volatile (
					"str %[high], [%[reg], #0]\n"
					DELAY_T1H
					"str %[low], [%[reg], #0]\n"
					DELAY_TxL
					::
					[reg] "l" (rgb_out_port_reg),
					[high] "l" (rgb_out_port_reg_masked_high),
					[low] "l" (rgb_out_port_reg_masked_low));
			} else {
				__asm volatile (
					"str %[high], [%[reg], #0]\n"
					DELAY_T0H
					"str %[low], [%[reg], #0]\n"
					DELAY_TxL
					::
					[reg] "l" (rgb_out_port_reg),
					[high] "l" (rgb_out_port_reg_masked_high),
					[low] "l" (rgb_out_port_reg_masked_low));
			}
		}
	}

	irq_unlock(irq_key);
}

static enum ec_status hc_rgbkbd_set_color(struct host_cmd_handler_args *args)
{
	const struct ec_params_rgbkbd_set_color *p = args->params;
	int i;

	if (p->start_key + p->length > EC_RGBKBD_MAX_KEY_COUNT)
		return EC_RES_INVALID_PARAM;

	for(i = 0; i < p->length; i++) {
		ws2812_led_colors[p->start_key + i][0] = p->color[i].g;
		ws2812_led_colors[p->start_key + i][1] = p->color[i].r;
		ws2812_led_colors[p->start_key + i][2] = p->color[i].b;
	}

	/* extend the led array if needed */
	if (ws2812_num_leds < (p->start_key + i))
		ws2812_num_leds = p->start_key + i;

	ws2812_send_buf(WS2812_RGB_OUT_PIN, &ws2812_led_colors[0][0], ws2812_num_leds*3);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RGBKBD_SET_COLOR, hc_rgbkbd_set_color,
		     EC_VER_MASK(0));

static int command_ws2812(int argc, const char **argv)
{
	char *e;
	int arg_i = 1;
	argc--;
	int led_position = 0;

	while (argc) {
		if (argc < 3) {
			return EC_ERROR_PARAM_COUNT;
		}

		uint8_t r = strtoi(argv[arg_i++], &e, 0);
		argc--;
		if (*e) return EC_ERROR_PARAM1;

		uint8_t g = strtoi(argv[arg_i++], &e, 0);
		argc--;
		if (*e) return EC_ERROR_PARAM2;

		uint8_t b = strtoi(argv[arg_i++], &e, 0);
		argc--;
		if (*e) return EC_ERROR_PARAM3;

		uint8_t multiplier = 1;
		/* if there's another param and it's a multiplier */
		if ((argc) && (argv[arg_i][0] == '*')) {
			multiplier = strtoi(argv[arg_i++] + 1, &e, 0);
			argc--;
			if (*e) return EC_ERROR_PARAM4;
		}

		for (int i = led_position; i < led_position + multiplier; i++) {
			ws2812_led_colors[i][0] = g;
			ws2812_led_colors[i][1] = r;
			ws2812_led_colors[i][2] = b;
		}
		led_position += multiplier;
	}

	/* extend the led array if needed */
	if (ws2812_num_leds < led_position)
		ws2812_num_leds = led_position;

	ws2812_send_buf(WS2812_RGB_OUT_PIN, &ws2812_led_colors[0][0], ws2812_num_leds*3);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ws2812, command_ws2812,
			"r g b [*multiplier] ...",
			"Set leds");
