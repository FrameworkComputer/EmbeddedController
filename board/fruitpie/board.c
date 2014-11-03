/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Fruitpie board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "usb.h"
#include "usb_pd.h"
#include "util.h"

#include "driver/tsu6721.h"

void rohm_event(enum gpio_signal signal)
{
	ccprintf("ROHM!\n");
}

void vbus_event(enum gpio_signal signal)
{
	ccprintf("VBUS!\n");
}

void tsu_event(enum gpio_signal signal)
{
	ccprintf("TSU!\n");
}

#include "gpio_list.h"

/* Initialize board. */
static void board_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= 1 << 0;

	/* Remap SPI2 to DMA channels 6 and 7 */
	STM32_SYSCFG_CFGR1 |= (1 << 24);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_CH_CC1_PD] = {"CC1_PD", 3300, 4096, 0, STM32_AIN(0)},
	[ADC_CH_CC2_PD] = {"CC2_PD", 3300, 4096, 0, STM32_AIN(4)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const void * const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT] = USB_STRING_DESC("FruitPie"),
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
};
BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

int board_set_debug(int enable)
{
	timestamp_t timeout;
	int rv = EC_SUCCESS;

	if (enable) {
		/* Disable the PD module */
		gpio_config_module(MODULE_USB_PD, 0);

		/* Suspend the USB PD task */
		pd_set_suspend(0, 1);

		/* Decrease BCDv1.2 timer to 0.6s */
		tsu6721_write(TSU6721_REG_TIMER, 0x05);

		timeout.val = get_time().val + DEBUG_SWITCH_TIMEOUT_MSEC;
		/* Wait for power to be detected to allow switching debug mux */
		while (!(tsu6721_read(TSU6721_REG_DEV_TYPE3) & 0x74)) {
			if (get_time().val > timeout.val)
				return EC_ERROR_TIMEOUT;

			/* Not already powered by cable, turn on regulator */
			gpio_set_level(GPIO_USB_C_5V_EN, 1);

			ccputs("Sleeping for 1s, waiting for TSU6721...\n");
			usleep(1000*MSEC);
		}

		/* Enable manual switching */
		rv = tsu6721_mux(TSU6721_MUX_USB);
		if (rv)
			return rv;

		/* Switch debug mux */
		tsu6721_set_pins(TSU6721_PIN_MANUAL2_BOOT);

		/* Set pins PD_CLK_IN, PD_TX_DATA, and
		 * VCONN1_EN to alternate function. */
		/* Set pin PD_TX_EN (NSS) to general purpose output mode. */
		STM32_GPIO_MODER(GPIO_B) &= ~0xff000000;
		STM32_GPIO_MODER(GPIO_B) |= 0xa9000000;

		/* Set all four pins to alternate function 0 */
		STM32_GPIO_AFRH(GPIO_B) &= ~(0xffff0000);

		/* Set all four pins to output push-pull */
		STM32_GPIO_OTYPER(GPIO_B) &= ~(0xf000);

		/* Set pullup on PD_TX_EN */
		STM32_GPIO_PUPDR(GPIO_B) |= 0x1000000;

		/* Set all four pins to high speed */
		STM32_GPIO_OSPEEDR(GPIO_B) |= 0xff000000;

		/* Reset SPI2 */
		STM32_RCC_APB1RSTR |= (1 << 14);
		STM32_RCC_APB1RSTR &= ~(1 << 14);

		/* Enable clocks to SPI2 module */
		STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;
	} else {
		/* Reset SPI2 */
		STM32_RCC_APB1RSTR |= (1 << 14);
		STM32_RCC_APB1RSTR &= ~(1 << 14);

		/* Set all but VCONN1_EN to input mode */
		STM32_GPIO_MODER(GPIO_B) &= ~0x3f000000;

		/* Unset pullup on PD_TX_EN/SPI_NSS */
		gpio_set_flags(GPIO_PD_TX_EN, GPIO_OUT_LOW);

		/* Turn off debug mux */
		tsu6721_set_pins(0);

		/* Disable manual switching */
		rv = tsu6721_mux(TSU6721_MUX_AUTO);
		if (rv)
			return rv;

		/* Disable power on USB_C_5V_EN pin */
		gpio_set_level(GPIO_USB_C_5V_EN, 0);

		/* Restore BCDv1.2 timer to 1.6s */
		tsu6721_write(TSU6721_REG_TIMER, 0x15);

		/* Restore the USB PD task */
		pd_set_suspend(0, 0);
	}

	return rv;
}

static int command_debug(int argc, char **argv)
{
        char *e;
        int v;

        if (argc < 2)
                return EC_ERROR_PARAM_COUNT;

        v = strtoi(argv[1], &e, 0);
        if (*e)
                return EC_ERROR_PARAM1;

        ccprintf("Setting debug: %d...\n", v);
        board_set_debug(v);

        return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(debugset, command_debug, NULL, "Set debug mode", NULL);

void board_set_usb_mux(int port, enum typec_mux mux, int polarity)
{
	/* reset everything */
	gpio_set_level(GPIO_SS1_EN_L, 1);
	gpio_set_level(GPIO_SS2_EN_L, 1);
	gpio_set_level(GPIO_DP_MODE, 0);
	gpio_set_level(GPIO_SS1_USB_MODE_L, 1);
	gpio_set_level(GPIO_SS2_USB_MODE_L, 1);

	if (mux == TYPEC_MUX_NONE)
		/* everything is already disabled, we can return */
		return;

	if (mux == TYPEC_MUX_USB || mux == TYPEC_MUX_DOCK) {
		/* USB 3.0 uses 2 superspeed lanes */
		gpio_set_level(polarity ? GPIO_SS2_USB_MODE_L :
					  GPIO_SS1_USB_MODE_L, 0);
	}

	if (mux == TYPEC_MUX_DP || mux == TYPEC_MUX_DOCK) {
		/* DP uses available superspeed lanes (x2 or x4) */
		gpio_set_level(GPIO_DP_POLARITY_L, !polarity);
		gpio_set_level(GPIO_DP_MODE, 1);
	}
	/* switch on superspeed lanes */
	gpio_set_level(GPIO_SS1_EN_L, 0);
	gpio_set_level(GPIO_SS2_EN_L, 0);
}

int board_get_usb_mux(int port, const char **dp_str, const char **usb_str)
{
	int has_ss = !gpio_get_level(GPIO_SS1_EN_L);
	int has_usb = !gpio_get_level(GPIO_SS1_USB_MODE_L) ||
		      !gpio_get_level(GPIO_SS2_USB_MODE_L);
	int has_dp = !!gpio_get_level(GPIO_DP_MODE);

	if (has_dp)
		*dp_str = gpio_get_level(GPIO_DP_POLARITY_L) ? "DP1" : "DP2";
	else
		*dp_str = NULL;

	if (has_usb)
		*usb_str = gpio_get_level(GPIO_SS1_USB_MODE_L) ?
				"USB2" : "USB1";
	else
		*usb_str = NULL;

	return has_ss;
}
