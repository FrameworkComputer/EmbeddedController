/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Plankton board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ina2xx.h"
#include "ioexpander_pca9534.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "util.h"

/* Debounce time for voltage buttons */
#define BUTTON_DEBOUNCE_US (100 * MSEC)

static enum gpio_signal button_pressed;

static int fake_pd_disconnected;
static int fake_pd_host_mode;
static int fake_pd_disconnect_duration_ms;

enum usbc_action {
	USBC_ACT_5V_TO_DUT,
	USBC_ACT_12V_TO_DUT,
	USBC_ACT_20V_TO_DUT,
	USBC_ACT_DEVICE,
	USBC_ACT_USBDP_TOGGLE,
	USBC_ACT_USB_EN,
	USBC_ACT_DP_EN,
	USBC_ACT_MUX_FLIP,
	USBC_ACT_CABLE_POLARITY0,
	USBC_ACT_CABLE_POLARITY1,

	/* Number of USBC actions */
	USBC_ACT_COUNT
};

enum board_src_cap src_cap_mapping[USBC_ACT_COUNT] =
{
	[USBC_ACT_5V_TO_DUT] = SRC_CAP_5V,
	[USBC_ACT_12V_TO_DUT] = SRC_CAP_12V,
	[USBC_ACT_20V_TO_DUT] = SRC_CAP_20V,
};

static void set_usbc_action(enum usbc_action act)
{
	int need_soft_reset;

	switch (act) {
	case USBC_ACT_5V_TO_DUT:
	case USBC_ACT_12V_TO_DUT:
	case USBC_ACT_20V_TO_DUT:
		need_soft_reset = gpio_get_level(GPIO_VBUS_CHARGER_EN);
		board_set_source_cap(src_cap_mapping[act]);
		pd_set_dual_role(PD_DRP_FORCE_SOURCE);
		if (need_soft_reset)
			pd_soft_reset();
		break;
	case USBC_ACT_DEVICE:
		pd_set_dual_role(PD_DRP_FORCE_SINK);
		break;
	case USBC_ACT_USBDP_TOGGLE:
		gpio_set_level(GPIO_USBC_SS_USB_MODE,
			       !gpio_get_level(GPIO_USBC_SS_USB_MODE));
		break;
	case USBC_ACT_USB_EN:
		gpio_set_level(GPIO_USBC_SS_USB_MODE, 1);
		break;
	case USBC_ACT_DP_EN:
		gpio_set_level(GPIO_USBC_SS_USB_MODE, 0);
		break;
	case USBC_ACT_MUX_FLIP:
		pd_send_vdm(0, USB_VID_GOOGLE, VDO_CMD_FLIP, NULL, 0);
		gpio_set_level(GPIO_USBC_POLARITY,
			       !gpio_get_level(GPIO_USBC_POLARITY));
		break;
	case USBC_ACT_CABLE_POLARITY0:
		gpio_set_level(GPIO_USBC_POLARITY, 0);
		break;
	case USBC_ACT_CABLE_POLARITY1:
		gpio_set_level(GPIO_USBC_POLARITY, 1);
		break;
	default:
		break;
	}
}

/* Handle debounced button press */
static void button_deferred(void)
{
	/* bounce ? */
	if (gpio_get_level(button_pressed) != 0)
		return;

	switch (button_pressed) {
	case GPIO_DBG_5V_TO_DUT_L:
		set_usbc_action(USBC_ACT_5V_TO_DUT);
		break;
	case GPIO_DBG_12V_TO_DUT_L:
		set_usbc_action(USBC_ACT_12V_TO_DUT);
		break;
	case GPIO_DBG_20V_TO_DUT_L:
		set_usbc_action(USBC_ACT_20V_TO_DUT);
		break;
	case GPIO_DBG_CHG_TO_DEV_L:
		set_usbc_action(USBC_ACT_DEVICE);
		break;
	case GPIO_DBG_USB_TOGGLE_L:
		set_usbc_action(USBC_ACT_USBDP_TOGGLE);
		break;
	case GPIO_DBG_MUX_FLIP_L:
		set_usbc_action(USBC_ACT_MUX_FLIP);
		break;
	default:
		break;
	}

	ccprintf("Button %d = %d\n",
		 button_pressed, gpio_get_level(button_pressed));
}
DECLARE_DEFERRED(button_deferred);

void button_event(enum gpio_signal signal)
{
	button_pressed = signal;
	/* reset debounce time */
	hook_call_deferred(button_deferred, BUTTON_DEBOUNCE_US);
}

void vbus_event(enum gpio_signal signal)
{
	ccprintf("VBUS! =%d\n", gpio_get_level(signal));
	task_wake(TASK_ID_PD);
}

#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_CH_CC1_PD] = {"CC1_PD", 3300, 4096, 0, STM32_AIN(0)},
	[ADC_CH_CC2_PD] = {"CC2_PD", 3300, 4096, 0, STM32_AIN(4)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master",  I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

static void board_init(void)
{
	/* Enable interrupts on VBUS transitions. */
	gpio_enable_interrupt(GPIO_VBUS_WAKE);

	/* Enable button interrupts. */
	gpio_enable_interrupt(GPIO_DBG_5V_TO_DUT_L);
	gpio_enable_interrupt(GPIO_DBG_12V_TO_DUT_L);
	gpio_enable_interrupt(GPIO_DBG_20V_TO_DUT_L);
	gpio_enable_interrupt(GPIO_DBG_CHG_TO_DEV_L);
	gpio_enable_interrupt(GPIO_DBG_USB_TOGGLE_L);
	gpio_enable_interrupt(GPIO_DBG_MUX_FLIP_L);

	ina2xx_init(0, 0x399f, INA2XX_CALIB_1MA(10 /* mOhm */));
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static int cmd_usbc_action(int argc, char *argv[])
{
	enum usbc_action act;

	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "5v"))
		act = USBC_ACT_5V_TO_DUT;
	else if (!strcasecmp(argv[1], "12v"))
		act = USBC_ACT_12V_TO_DUT;
	else if (!strcasecmp(argv[1], "20v"))
		act = USBC_ACT_20V_TO_DUT;
	else if (!strcasecmp(argv[1], "dev"))
		act = USBC_ACT_DEVICE;
	else if (!strcasecmp(argv[1], "usb"))
		act = USBC_ACT_USB_EN;
	else if (!strcasecmp(argv[1], "dp"))
		act = USBC_ACT_DP_EN;
	else if (!strcasecmp(argv[1], "flip"))
		act = USBC_ACT_MUX_FLIP;
	else if (!strcasecmp(argv[1], "pol0"))
		act = USBC_ACT_CABLE_POLARITY0;
	else if (!strcasecmp(argv[1], "pol1"))
		act = USBC_ACT_CABLE_POLARITY1;
	else
		return EC_ERROR_PARAM1;

	set_usbc_action(act);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(usbc_action, cmd_usbc_action,
			"<5v|12v|20v|dev|usb|dp|flip|pol0|pol1>",
			"Set Plankton type-C port state",
			NULL);

static int board_usb_hub_reset(void)
{
	int ret;

	ret = pca9534_config_pin(I2C_PORT_MASTER, 0x40, 7, PCA9534_OUTPUT);
	if (ret)
		return ret;
	ret = pca9534_set_level(I2C_PORT_MASTER, 0x40, 7, 0);
	if (ret)
		return ret;
	usleep(100 * MSEC);
	return pca9534_set_level(I2C_PORT_MASTER, 0x40, 7, 1);
}

void board_maybe_reset_usb_hub(void)
{
	int ret;
	int level;

	ret = pca9534_config_pin(I2C_PORT_MASTER, 0x40, 6, PCA9534_INPUT);
	if (ret)
		return;
	ret = pca9534_get_level(I2C_PORT_MASTER, 0x40, 6, &level);
	if (ret)
		return;
	if (level == 1)
		board_usb_hub_reset();
}

static int cmd_usb_hub_reset(int argc, char *argv[])
{
	return board_usb_hub_reset();
}
DECLARE_CONSOLE_COMMAND(hub_reset, cmd_usb_hub_reset,
			NULL, "Reset USB hub", NULL);

static void board_usb_hub_reset_no_return(void)
{
	board_usb_hub_reset();
}
DECLARE_DEFERRED(board_usb_hub_reset_no_return);

static void board_init_usb_hub(void)
{
	if (system_get_reset_flags() & RESET_FLAG_POWER_ON)
		hook_call_deferred(board_usb_hub_reset_no_return, 500 * MSEC);
}
DECLARE_HOOK(HOOK_INIT, board_init_usb_hub, HOOK_PRIO_DEFAULT);

void board_pd_set_host_mode(int enable)
{
	cprintf(CC_USBPD, "Host mode: %d\n", enable);

	if (board_pd_fake_disconnected()) {
		board_update_fake_adc_value(enable);
		return;
	}

	if (enable) {
		/* Source mode, disable charging */
		gpio_set_level(GPIO_USBC_CHARGE_EN, 0);
		/* High Z for no pull-down resistor on CC1 */
		gpio_set_flags_by_mask(GPIO_A, (1 << 9), GPIO_INPUT);
		/* Set pull-up resistor on CC1 */
		gpio_set_flags_by_mask(GPIO_A, (1 << 2), GPIO_OUT_HIGH);
		/* High Z for no pull-down resistor on CC2 */
		gpio_set_flags_by_mask(GPIO_B, (1 << 7), GPIO_INPUT);
		/* Set pull-up resistor on CC2 */
		gpio_set_flags_by_mask(GPIO_B, (1 << 6), GPIO_OUT_HIGH);
	} else {
		/* Device mode, disable VBUS */
		gpio_set_level(GPIO_VBUS_CHARGER_EN, 0);
		gpio_set_level(GPIO_USBC_VSEL_0, 0);
		gpio_set_level(GPIO_USBC_VSEL_1, 0);
		/* High Z for no pull-up resistor on CC1 */
		gpio_set_flags_by_mask(GPIO_A, (1 << 2), GPIO_INPUT);
		/* Set pull-down resistor on CC1 */
		gpio_set_flags_by_mask(GPIO_A, (1 << 9), GPIO_OUT_LOW);
		/* High Z for no pull-up resistor on CC2 */
		gpio_set_flags_by_mask(GPIO_B, (1 << 6), GPIO_INPUT);
		/* Set pull-down resistor on CC2 */
		gpio_set_flags_by_mask(GPIO_B, (1 << 7), GPIO_OUT_LOW);
		/* Set charge enable */
		gpio_set_level(GPIO_USBC_CHARGE_EN, 1);
	}
}

int board_pd_fake_disconnected(void)
{
	return fake_pd_disconnected;
}

int board_fake_pd_adc_read(void)
{
	if (fake_pd_host_mode)
		return 3000; /* mV */
	else
		return 0; /* mV */
}

void board_update_fake_adc_value(int host_mode)
{
	fake_pd_host_mode = host_mode;
}

static void fake_disconnect_end(void)
{
	fake_pd_disconnected = 0;
	board_pd_set_host_mode(fake_pd_host_mode);
}
DECLARE_DEFERRED(fake_disconnect_end);

static void fake_disconnect_start(void)
{
	/* Record the current host mode */
	fake_pd_host_mode = !gpio_get_level(GPIO_USBC_CHARGE_EN);
	/* Disable VBUS */
	gpio_set_level(GPIO_VBUS_CHARGER_EN, 0);
	gpio_set_level(GPIO_USBC_VSEL_0, 0);
	gpio_set_level(GPIO_USBC_VSEL_1, 0);
	/* High Z for no pull-up resistor on CC1 */
	gpio_set_flags_by_mask(GPIO_A, (1 << 2), GPIO_INPUT);
	/* High Z for no pull-up resistor on CC2 */
	gpio_set_flags_by_mask(GPIO_B, (1 << 6), GPIO_INPUT);
	/* High Z for no pull-down resistor on CC1 */
	gpio_set_flags_by_mask(GPIO_A, (1 << 9), GPIO_INPUT);
	/* High Z for no pull-down resistor on CC2 */
	gpio_set_flags_by_mask(GPIO_B, (1 << 7), GPIO_INPUT);

	fake_pd_disconnected = 1;

	hook_call_deferred(fake_disconnect_end,
			   fake_pd_disconnect_duration_ms * MSEC);
}
DECLARE_DEFERRED(fake_disconnect_start);

static int cmd_fake_disconnect(int argc, char *argv[])
{
	int delay_ms, duration_ms;
	char *e;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	delay_ms = strtoi(argv[1], &e, 0);
	if (*e || delay_ms < 0)
		return EC_ERROR_PARAM1;
	duration_ms = strtoi(argv[2], &e, 0);
	if (*e || duration_ms < 0)
		return EC_ERROR_PARAM2;

	/* Cancel any pending function calls */
	hook_call_deferred(fake_disconnect_start, -1);
	hook_call_deferred(fake_disconnect_end, -1);

	fake_pd_disconnect_duration_ms = duration_ms;
	hook_call_deferred(fake_disconnect_start, delay_ms * MSEC);

	ccprintf("Fake disconnect for %d ms starting in %d ms.\n",
		 duration_ms, delay_ms);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fake_disconnect, cmd_fake_disconnect,
			"<delay_ms> <duration_ms>", NULL, NULL);
