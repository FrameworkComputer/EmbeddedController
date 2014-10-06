/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* samus_pd board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "battery.h"
#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "pi3usb9281.h"
#include "power.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "usb.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

/* Chipset power state */
static enum power_state ps;

/* Battery state of charge */
int batt_soc;

/* PWM channels. Must be in the exact same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	{STM32_TIM(15), STM32_TIM_CH(2), 0, GPIO_ILIM_ADJ_PWM, GPIO_ALT_F1},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

void vbus0_evt(enum gpio_signal signal)
{
	ccprintf("VBUS %d, %d!\n", signal, gpio_get_level(signal));
	task_wake(TASK_ID_PD_C0);
}

void vbus1_evt(enum gpio_signal signal)
{
	ccprintf("VBUS %d, %d!\n", signal, gpio_get_level(signal));
	task_wake(TASK_ID_PD_C1);
}

/*
 * Update available charge. Called from deferred task, queued on Pericom
 * interrupt.
 */
static void board_usb_charger_update(int port)
{
	int device_type, charger_status;
	struct charge_port_info charge;
	charge.voltage = USB_BC12_CHARGE_VOLTAGE;

	/* Read interrupt register to clear*/
	pi3usb9281_get_interrupts(port);
	device_type = pi3usb9281_get_device_type(port);
	charger_status = pi3usb9281_get_charger_status(port);

	/* Attachment: decode + update available charge */
	if (device_type || (charger_status & 0x1f))
		charge.current = pi3usb9281_get_ilim(device_type,
						     charger_status);
	/* Detachment: update available charge to 0 */
	else
		charge.current = 0;

	charge_manager_update(CHARGE_SUPPLIER_BC12, port, &charge);

}

/* Pericom USB deferred tasks -- called after USB device insert / removal */
static void usb_port0_charger_update(void)
{
	board_usb_charger_update(0);
}
DECLARE_DEFERRED(usb_port0_charger_update);

static void usb_port1_charger_update(void)
{
	board_usb_charger_update(1);
}
DECLARE_DEFERRED(usb_port1_charger_update);

void usb0_evt(enum gpio_signal signal)
{
	hook_call_deferred(usb_port0_charger_update, 0);
}

void usb1_evt(enum gpio_signal signal)
{
	hook_call_deferred(usb_port1_charger_update, 0);
}

void pch_evt(enum gpio_signal signal)
{
	/* Determine new chipset state, trigger corresponding hook */
	switch (ps) {
	case POWER_S5:
		if (gpio_get_level(GPIO_PCH_SLP_S5_L)) {
			/* S5 -> S3 */
			hook_notify(HOOK_CHIPSET_STARTUP);
			ps = POWER_S3;
		}
		break;
	case POWER_S3:
		if (gpio_get_level(GPIO_PCH_SLP_S3_L)) {
			/* S3 -> S0: disable deep sleep */
			disable_sleep(SLEEP_MASK_AP_RUN);
			hook_notify(HOOK_CHIPSET_RESUME);
			ps = POWER_S0;
		} else if (!gpio_get_level(GPIO_PCH_SLP_S5_L)) {
			/* S3 -> S5 */
			hook_notify(HOOK_CHIPSET_SHUTDOWN);
			ps = POWER_S5;
		}
		break;
	case POWER_S0:
		if (!gpio_get_level(GPIO_PCH_SLP_S3_L)) {
			/* S0 -> S3: enable deep sleep */
			enable_sleep(SLEEP_MASK_AP_RUN);
			hook_notify(HOOK_CHIPSET_SUSPEND);
			ps = POWER_S3;
		}
		break;
	default:
		break;
	}
}

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= 1 << 0;
	/*
	 * the DMA mapping is :
	 *  Chan 2 : TIM1_CH1  (C0 RX)
	 *  Chan 3 : SPI1_TX   (C1 TX)
	 *  Chan 4 : USART1_TX
	 *  Chan 5 : USART1_RX
	 *  Chan 6 : TIM3_CH1  (C1 RX)
	 *  Chan 7 : SPI2_TX   (C0 TX)
	 */

	/*
	 * Remap USART1 RX/TX DMA to match uart driver. Remap SPI2 RX/TX and
	 * TIM3_CH1 for unique DMA channels.
	 */
	STM32_SYSCFG_CFGR1 |= (1 << 9) | (1 << 10) | (1 << 24) | (1 << 30);
}

#include "gpio_list.h"

/* Initialize board. */
static void board_init(void)
{
	int pd_enable;
	int slp_s5 = gpio_get_level(GPIO_PCH_SLP_S5_L);
	int slp_s3 = gpio_get_level(GPIO_PCH_SLP_S3_L);

	/*
	 * Enable CC lines after all GPIO have been initialized. Note, it is
	 * important that this is enabled after the CC_ODL lines are set low
	 * to specify device mode.
	 */
	gpio_set_level(GPIO_USB_C_CC_EN, 1);

	/* Enable interrupts on VBUS transitions. */
	gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE);
	gpio_enable_interrupt(GPIO_USB_C1_VBUS_WAKE);

	/* Enable pericom BC1.2 interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);
	pi3usb9281_set_interrupt_mask(0, 0xff);
	pi3usb9281_set_interrupt_mask(1, 0xff);
	pi3usb9281_enable_interrupts(0);
	pi3usb9281_enable_interrupts(1);

	/* Determine initial chipset state */
	if (slp_s5 && slp_s3) {
		disable_sleep(SLEEP_MASK_AP_RUN);
		hook_notify(HOOK_CHIPSET_RESUME);
		ps = POWER_S0;
	} else if (slp_s5 && !slp_s3) {
		enable_sleep(SLEEP_MASK_AP_RUN);
		hook_notify(HOOK_CHIPSET_STARTUP);
		ps = POWER_S3;
	} else {
		enable_sleep(SLEEP_MASK_AP_RUN);
		hook_notify(HOOK_CHIPSET_SHUTDOWN);
		ps = POWER_S5;
	}

	/* Enable interrupts on PCH state change */
	gpio_enable_interrupt(GPIO_PCH_SLP_S3_L);
	gpio_enable_interrupt(GPIO_PCH_SLP_S5_L);

	/*
	 * Do not enable PD communication in RO as a security measure.
	 * We don't want to allow communication to outside world until
	 * we jump to RW. This can by overridden with the removal of
	 * the write protect screw to allow for easier testing, and for
	 * booting without a battery.
	 */
	if (system_get_image_copy() != SYSTEM_IMAGE_RW
	    && system_is_locked()) {
		ccprintf("[%T PD communication disabled]\n");
		pd_enable = 0;
	} else {
		pd_enable = 1;
	}
	pd_comm_enable(pd_enable);

	/* Enable ILIM PWM: initial duty cycle 0% = 500mA limit. */
	pwm_enable(PWM_CH_ILIM, 1);
	pwm_set_duty(PWM_CH_ILIM, 0);

	/*
	 * Initialize BC1.2 USB charging, so that charge manager will assign
	 * charge port based upon charger actually present. Charger detection
	 * can take up to 200ms after power-on, so delay the initialization.
	 */
	hook_call_deferred(usb_port0_charger_update, 200 * MSEC);
	hook_call_deferred(usb_port1_charger_update, 200 * MSEC);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_C0_CC1_PD] = {"C0_CC1_PD", 3300, 4096, 0, STM32_AIN(0)},
	[ADC_C1_CC1_PD] = {"C1_CC1_PD", 3300, 4096, 0, STM32_AIN(2)},
	[ADC_C0_CC2_PD] = {"C0_CC2_PD", 3300, 4096, 0, STM32_AIN(4)},
	[ADC_C1_CC2_PD] = {"C1_CC2_PD", 3300, 4096, 0, STM32_AIN(5)},

	/* Vbus sensing. Converted to mV, full ADC is equivalent to 25.774V. */
	[ADC_BOOSTIN] = {"V_BOOSTIN",  25774, 4096, 0, STM32_AIN(11)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
	{"slave",  I2C_PORT_SLAVE, 100,
		GPIO_SLAVE_I2C_SCL, GPIO_SLAVE_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

struct usb_port_mux
{
	enum gpio_signal ss1_en_l;
	enum gpio_signal ss2_en_l;
	enum gpio_signal dp_mode_l;
	enum gpio_signal dp_polarity;
	enum gpio_signal ss1_dp_mode;
	enum gpio_signal ss2_dp_mode;
};

const struct usb_port_mux usb_muxes[] = {
	{
		.ss1_en_l    = GPIO_USB_C0_SS1_EN_L,
		.ss2_en_l    = GPIO_USB_C0_SS2_EN_L,
		.dp_mode_l   = GPIO_USB_C0_DP_MODE_L,
		.dp_polarity = GPIO_USB_C0_DP_POLARITY,
		.ss1_dp_mode = GPIO_USB_C0_SS1_DP_MODE,
		.ss2_dp_mode = GPIO_USB_C0_SS2_DP_MODE,
	},
	{
		.ss1_en_l    = GPIO_USB_C1_SS1_EN_L,
		.ss2_en_l    = GPIO_USB_C1_SS2_EN_L,
		.dp_mode_l   = GPIO_USB_C1_DP_MODE_L,
		.dp_polarity = GPIO_USB_C1_DP_POLARITY,
		.ss1_dp_mode = GPIO_USB_C1_SS1_DP_MODE,
		.ss2_dp_mode = GPIO_USB_C1_SS2_DP_MODE,
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == PD_PORT_COUNT);

void board_set_usb_mux(int port, enum typec_mux mux, int polarity)
{
	const struct usb_port_mux *usb_mux = usb_muxes + port;

	/* reset everything */
	gpio_set_level(usb_mux->ss1_en_l, 1);
	gpio_set_level(usb_mux->ss2_en_l, 1);
	gpio_set_level(usb_mux->dp_mode_l, 1);
	gpio_set_level(usb_mux->dp_polarity, 1);
	gpio_set_level(usb_mux->ss1_dp_mode, 1);
	gpio_set_level(usb_mux->ss2_dp_mode, 1);

	if (mux == TYPEC_MUX_NONE)
		/* everything is already disabled, we can return */
		return;

	if (mux == TYPEC_MUX_USB || mux == TYPEC_MUX_DOCK) {
		/* USB 3.0 uses 2 superspeed lanes */
		gpio_set_level(polarity ? usb_mux->ss2_dp_mode :
					  usb_mux->ss1_dp_mode, 0);
	}

	if (mux == TYPEC_MUX_DP || mux == TYPEC_MUX_DOCK) {
		/* DP uses available superspeed lanes (x2 or x4) */
		gpio_set_level(usb_mux->dp_polarity, polarity);
		gpio_set_level(usb_mux->dp_mode_l, 0);
	}
	/* switch on superspeed lanes */
	gpio_set_level(usb_mux->ss1_en_l, 0);
	gpio_set_level(usb_mux->ss2_en_l, 0);
}

int board_get_usb_mux(int port, const char **dp_str, const char **usb_str)
{
	const struct usb_port_mux *usb_mux = usb_muxes + port;
	int has_ss, has_usb, has_dp;
	const char *dp, *usb;

	has_ss = !gpio_get_level(usb_mux->ss1_en_l);
	has_usb = !gpio_get_level(usb_mux->ss1_dp_mode) ||
		  !gpio_get_level(usb_mux->ss2_dp_mode);
	has_dp = !gpio_get_level(usb_mux->dp_mode_l);
	dp = gpio_get_level(usb_mux->dp_polarity) ?
			"DP2" : "DP1";
	usb = gpio_get_level(usb_mux->ss1_dp_mode) ?
			"USB2" : "USB1";

	*dp_str = has_dp ? dp : NULL;
	*usb_str = has_usb ? usb : NULL;

	return has_ss;
}

void board_flip_usb_mux(int port)
{
	const struct usb_port_mux *usb_mux = usb_muxes + port;
	int usb_polarity;

	/* Flip DP polarity */
	gpio_set_level(usb_mux->dp_polarity,
		       !gpio_get_level(usb_mux->dp_polarity));

	/* Flip USB polarity if enabled */
	if (gpio_get_level(usb_mux->ss1_dp_mode) &&
	    gpio_get_level(usb_mux->ss2_dp_mode))
		return;
	usb_polarity = gpio_get_level(usb_mux->ss1_dp_mode);

	/*
	 * Disable both sides first so that we don't enable both at the
	 * same time accidentally.
	 */
	gpio_set_level(usb_mux->ss1_dp_mode, 1);
	gpio_set_level(usb_mux->ss2_dp_mode, 1);

	gpio_set_level(usb_mux->ss1_dp_mode, !usb_polarity);
	gpio_set_level(usb_mux->ss2_dp_mode, usb_polarity);
}

void board_update_battery_soc(int soc)
{
	batt_soc = soc;
}

int board_get_battery_soc(void)
{
	return batt_soc;
}

enum battery_present battery_is_present(void)
{
	if (batt_soc >= 0)
		return BP_YES;
	return BP_NOT_SURE;
}

/**
 * Set active charge port -- only one port can be active at a time.
 *
 * @param charge_port   Charge port to enable.
 */
void board_set_active_charge_port(int charge_port)
{
	if (charge_port >= 0 && charge_port < PD_PORT_COUNT &&
	    pd_get_role(charge_port) != PD_ROLE_SINK) {
		CPRINTS("Port %d is not a sink, skipping enable", charge_port);
		charge_port = CHARGE_PORT_NONE;
	}
	gpio_set_level(GPIO_USB_C0_CHARGE_EN_L, !(charge_port == 0));
	gpio_set_level(GPIO_USB_C1_CHARGE_EN_L, !(charge_port == 1));
	CPRINTS("Set active charge port %d", charge_port);
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param charge_ma     Desired charge limit (mA).
 */
void board_set_charge_limit(int charge_ma)
{
	int pwm_duty = MA_TO_PWM(charge_ma);
	if (pwm_duty < 0)
		pwm_duty = 0;
	else if (pwm_duty > 100)
		pwm_duty = 100;

	pwm_set_duty(PWM_CH_ILIM, pwm_duty);
	CPRINTS("Set ilim duty %d", pwm_duty);
}
