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
#include "host_command.h"
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

/* Amount to offset the input current limit when sending to EC */
#define INPUT_CURRENT_LIMIT_OFFSET_MA 192

/* Chipset power state */
static enum power_state ps;

/* Battery state of charge */
static int batt_soc;
static int fake_state_of_charge = -1; /* use real soc by default */

/* Last charge port override when charging turned off due to full battery */
static int chg_override_port = OVERRIDE_OFF;
static int chg_is_cutoff;

/* PD MCU status and host event status for host command */
static struct ec_response_pd_status pd_status;
static struct ec_response_host_event_status host_event_status;

/*
 * Store the state of our USB data switches so that they can be restored
 * after pericom reset.
 */
static int usb_switch_state[PD_PORT_COUNT];
static struct mutex usb_switch_lock[PD_PORT_COUNT];

/* PWM channels. Must be in the exact same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	{STM32_TIM(15), STM32_TIM_CH(2), 0, GPIO_ILIM_ADJ_PWM, GPIO_ALT_F1},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Charge supplier priority: lower number indicates higher priority. */
const int supplier_priority[] = {
	[CHARGE_SUPPLIER_PD] = 0,
	[CHARGE_SUPPLIER_TYPEC] = 1,
	[CHARGE_SUPPLIER_PROPRIETARY] = 1,
	[CHARGE_SUPPLIER_BC12_DCP] = 1,
	[CHARGE_SUPPLIER_BC12_CDP] = 2,
	[CHARGE_SUPPLIER_BC12_SDP] = 3,
	[CHARGE_SUPPLIER_OTHER] = 3
};
BUILD_ASSERT(ARRAY_SIZE(supplier_priority) == CHARGE_SUPPLIER_COUNT);

static void pericom_port0_reenable_interrupts(void)
{
	CPRINTS("VBUS p0 %d", gpio_get_level(GPIO_USB_C0_VBUS_WAKE));
	pi3usb9281_enable_interrupts(0);
}
DECLARE_DEFERRED(pericom_port0_reenable_interrupts);

static void pericom_port1_reenable_interrupts(void)
{
	CPRINTS("VBUS p1 %d", gpio_get_level(GPIO_USB_C1_VBUS_WAKE));
	pi3usb9281_enable_interrupts(1);
}
DECLARE_DEFERRED(pericom_port1_reenable_interrupts);

void vbus0_evt(enum gpio_signal signal)
{
	/*
	 * Re-enable interrupts on pericom charger detector since the
	 * chip may periodically reset itself, and come back up with
	 * registers in default state. TODO(crosbug.com/p/33823): Fix
	 * these unwanted resets.
	 */
	hook_call_deferred(pericom_port0_reenable_interrupts, 0);
	if (task_start_called())
		task_wake(TASK_ID_PD_C0);
}

void vbus1_evt(enum gpio_signal signal)
{
	/*
	 * Re-enable interrupts on pericom charger detector since the
	 * chip may periodically reset itself, and come back up with
	 * registers in default state. TODO(crosbug.com/p/33823): Fix
	 * these unwanted resets.
	 */
	hook_call_deferred(pericom_port1_reenable_interrupts, 0);
	if (task_start_called())
		task_wake(TASK_ID_PD_C1);
}

void set_usb_switches(int port, int open)
{
	mutex_lock(&usb_switch_lock[port]);
	usb_switch_state[port] = open;
	pi3usb9281_set_switches(port, open);
	mutex_unlock(&usb_switch_lock[port]);
}

/* Wait after a charger is detected to debounce pin contact order */
#define USB_CHG_DEBOUNCE_DELAY_MS 1000
/*
 * Wait after reset, before re-enabling attach interrupt, so that the
 * spurious attach interrupt from certain ports is ignored.
 */
#define USB_CHG_RESET_DELAY_MS 100

void usb_charger_task(void)
{
	int port = (task_get_current() == TASK_ID_USB_CHG_P0 ? 0 : 1);
	int device_type, charger_status;
	struct charge_port_info charge;
	int type;
	charge.voltage = USB_BC12_CHARGE_VOLTAGE;

	while (1) {
		/* Read interrupt register to clear on chip */
		pi3usb9281_get_interrupts(port);

		/* Set device type */
		device_type = pi3usb9281_get_device_type(port);
		charger_status = pi3usb9281_get_charger_status(port);

		/* Debounce pin plug order if we detect a charger */
		if (device_type || PI3USB9281_CHG_STATUS_ANY(charger_status)) {
			msleep(USB_CHG_DEBOUNCE_DELAY_MS);

			/* Trigger chip reset to refresh detection registers */
			pi3usb9281_reset(port);
			/*
			 * Restore data switch settings - switches return to
			 * closed on reset until restored.
			 */
			mutex_lock(&usb_switch_lock[port]);
			if (usb_switch_state[port])
				pi3usb9281_set_switches(port, 1);
			mutex_unlock(&usb_switch_lock[port]);
			/* Clear possible disconnect interrupt */
			pi3usb9281_get_interrupts(port);
			/* Mask attach interrupt */
			pi3usb9281_set_interrupt_mask(port,
						      0xff &
						      ~PI3USB9281_INT_ATTACH);
			/* Re-enable interrupts */
			pi3usb9281_enable_interrupts(port);
			msleep(USB_CHG_RESET_DELAY_MS);

			/* Clear possible attach interrupt */
			pi3usb9281_get_interrupts(port);
			/* Re-enable attach interrupt */
			pi3usb9281_set_interrupt_mask(port, 0xff);

			/* Re-read ID registers */
			device_type = pi3usb9281_get_device_type(port);
			charger_status = pi3usb9281_get_charger_status(port);
		}

		if (PI3USB9281_CHG_STATUS_ANY(charger_status))
			type = CHARGE_SUPPLIER_PROPRIETARY;
		else if (device_type & PI3USB9281_TYPE_CDP)
			type = CHARGE_SUPPLIER_BC12_CDP;
		else if (device_type & PI3USB9281_TYPE_DCP)
			type = CHARGE_SUPPLIER_BC12_DCP;
		else if (device_type & PI3USB9281_TYPE_SDP)
			type = CHARGE_SUPPLIER_BC12_SDP;
		else
			type = CHARGE_SUPPLIER_OTHER;

		/* Attachment: decode + update available charge */
		if (device_type || PI3USB9281_CHG_STATUS_ANY(charger_status)) {
			charge.current = pi3usb9281_get_ilim(device_type,
							     charger_status);
			charge_manager_update(type, port, &charge);
		} else { /* Detachment: update available charge to 0 */
			charge.current = 0;
			charge_manager_update(CHARGE_SUPPLIER_PROPRIETARY,
					      port,
					      &charge);
			charge_manager_update(CHARGE_SUPPLIER_BC12_CDP,
					      port,
					      &charge);
			charge_manager_update(CHARGE_SUPPLIER_BC12_DCP,
					      port,
					      &charge);
			charge_manager_update(CHARGE_SUPPLIER_BC12_SDP,
					      port,
					      &charge);
			charge_manager_update(CHARGE_SUPPLIER_OTHER,
					      port,
					      &charge);
		}

		/* notify host of power info change */
		pd_send_host_event(PD_EVENT_POWER_CHANGE);

		/* Wait for interrupt */
		task_wait_event(-1);
	}
}

/* Charge manager callback function, called on delayed override timeout */
void board_charge_manager_override_timeout(void)
{
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}
DECLARE_DEFERRED(board_charge_manager_override_timeout);

static void wake_usb_charger_task(int port)
{
	task_wake(port ? TASK_ID_USB_CHG_P1 : TASK_ID_USB_CHG_P0);
}

void usb0_evt(enum gpio_signal signal)
{
	wake_usb_charger_task(0);
}

void usb1_evt(enum gpio_signal signal)
{
	wake_usb_charger_task(1);
}

/* When battery is full, cutoff charging by disabling AC input current */
static void check_charging_cutoff(void)
{
	int port;

	/* Only check if charging needs to be turned off when not in S0 */
	if (ps == POWER_S0)
		return;

	port = charge_manager_get_active_charge_port();

	/*
	 * If battery is full disable charging, if battery is not full, restore
	 * charge port.
	 */
	if (!chg_is_cutoff && port != CHARGE_PORT_NONE && batt_soc == 100) {
		charge_manager_set_override(OVERRIDE_DONT_CHARGE);
		chg_is_cutoff = 1;
	} else if (chg_is_cutoff && batt_soc < 100) {
		charge_manager_set_override(chg_override_port);
		chg_is_cutoff = 0;
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, check_charging_cutoff, HOOK_PRIO_DEFAULT);

static void chipset_s5_to_s3(void)
{
	ps = POWER_S3;
	hook_notify(HOOK_CHIPSET_STARTUP);
}

static void chipset_s3_to_s0(void)
{
	/* Disable deep sleep and restore charge override port */
	disable_sleep(SLEEP_MASK_AP_RUN);
	charge_manager_set_override(chg_override_port);
	chg_is_cutoff = 0;

	ps = POWER_S0;
	hook_notify(HOOK_CHIPSET_RESUME);
}

static void chipset_s3_to_s5(void)
{
	ps = POWER_S5;
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
}

static void chipset_s0_to_s3(void)
{
	/* Enable deep sleep and store charge override port */
	enable_sleep(SLEEP_MASK_AP_RUN);
	chg_override_port = charge_manager_get_override();

	ps = POWER_S3;
	hook_notify(HOOK_CHIPSET_SUSPEND);
}

static void pch_evt_deferred(void)
{
	/* Determine new chipset state, trigger corresponding transition */
	switch (ps) {
	case POWER_S5:
		if (gpio_get_level(GPIO_PCH_SLP_S5_L))
			chipset_s5_to_s3();
		if (gpio_get_level(GPIO_PCH_SLP_S3_L))
			chipset_s3_to_s0();
		break;
	case POWER_S3:
		if (gpio_get_level(GPIO_PCH_SLP_S3_L))
			chipset_s3_to_s0();
		else if (!gpio_get_level(GPIO_PCH_SLP_S5_L))
			chipset_s3_to_s5();
		break;
	case POWER_S0:
		if (!gpio_get_level(GPIO_PCH_SLP_S3_L))
			chipset_s0_to_s3();
		if (!gpio_get_level(GPIO_PCH_SLP_S5_L))
			chipset_s3_to_s5();
		break;
	default:
		break;
	}
}
DECLARE_DEFERRED(pch_evt_deferred);

void pch_evt(enum gpio_signal signal)
{
	hook_call_deferred(pch_evt_deferred, 0);
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
	int pd_enable, i;
	int slp_s5 = gpio_get_level(GPIO_PCH_SLP_S5_L);
	int slp_s3 = gpio_get_level(GPIO_PCH_SLP_S3_L);
	struct charge_port_info charge;

	/*
	 * Enable CC lines after all GPIO have been initialized. Note, it is
	 * important that this is enabled after the CC_ODL lines are set low
	 * to specify device mode.
	 */
	gpio_set_level(GPIO_USB_C_CC_EN, 1);

	/* Enable interrupts on VBUS transitions. */
	gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE);
	gpio_enable_interrupt(GPIO_USB_C1_VBUS_WAKE);

	/* Initialize all pericom charge suppliers to 0 */
	charge.voltage = USB_BC12_CHARGE_VOLTAGE;
	charge.current = 0;
	for (i = 0; i < PD_PORT_COUNT; i++) {
		charge_manager_update(CHARGE_SUPPLIER_PROPRIETARY, i,
				      &charge);
		charge_manager_update(CHARGE_SUPPLIER_BC12_CDP, i, &charge);
		charge_manager_update(CHARGE_SUPPLIER_BC12_DCP, i, &charge);
		charge_manager_update(CHARGE_SUPPLIER_BC12_SDP, i, &charge);
		charge_manager_update(CHARGE_SUPPLIER_OTHER, i, &charge);
	}

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

#ifdef CONFIG_PWM
	/* Enable ILIM PWM: initial duty cycle 0% = 500mA limit. */
	pwm_enable(PWM_CH_ILIM, 1);
	pwm_set_duty(PWM_CH_ILIM, 0);
#endif
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
	[ADC_VBUS] = {"VBUS",  25774, 4096, 0, STM32_AIN(11)},
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
	check_charging_cutoff();
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

static void pd_send_ec_int(void)
{
	gpio_set_level(GPIO_EC_INT, 1);

	/*
	 * Delay long enough to guarantee EC see's the change. Slowest
	 * EC clock speed is 250kHz in deep sleep -> 4us, and add 1us
	 * for buffer.
	 */
	usleep(5);

	gpio_set_level(GPIO_EC_INT, 0);
}

/**
 * Set active charge port -- only one port can be active at a time.
 *
 * @param charge_port   Charge port to enable.
 *
 * Returns EC_SUCCESS if charge port is accepted and made active,
 * EC_ERROR_* otherwise.
 */
int board_set_active_charge_port(int charge_port)
{
	/* charge port is a realy physical port */
	int is_real_port = (charge_port >= 0 && charge_port < PD_PORT_COUNT);

	if (is_real_port && pd_get_role(charge_port) != PD_ROLE_SINK) {
		CPRINTS("Skip enable p%d", charge_port);
		return EC_ERROR_INVAL;
	}

	pd_status.active_charge_port = charge_port;
	gpio_set_level(GPIO_USB_C0_CHARGE_EN_L, !(charge_port == 0));
	gpio_set_level(GPIO_USB_C1_CHARGE_EN_L, !(charge_port == 1));

	/*
	 * If new charge port when charge is cutoff, then user must have
	 * plugged in a new dedicated charger. This resets the charge
	 * override port and clears the charge cutoff flag.
	 */
	if (chg_is_cutoff && is_real_port) {
		chg_override_port = OVERRIDE_OFF;
		chg_is_cutoff = 0;
	}
	check_charging_cutoff();

	CPRINTS("New chg p%d", charge_port);
	return EC_SUCCESS;
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param charge_ma     Desired charge limit (mA).
 */
void board_set_charge_limit(int charge_ma)
{
#ifdef CONFIG_PWM
	int pwm_duty = MA_TO_PWM(charge_ma);
	if (pwm_duty < 0)
		pwm_duty = 0;
	else if (pwm_duty > 100)
		pwm_duty = 100;

	pwm_set_duty(PWM_CH_ILIM, pwm_duty);
#endif

	pd_status.curr_lim_ma = MAX(0, charge_ma -
					INPUT_CURRENT_LIMIT_OFFSET_MA);
	pd_send_ec_int();

	CPRINTS("New ilim %d", charge_ma);
}

/* Send host event up to AP */
void pd_send_host_event(int mask)
{
	/* mask must be set */
	if (!mask)
		return;

	atomic_or(&(host_event_status.status), mask);
	atomic_or(&(pd_status.status), PD_STATUS_HOST_EVENT);
	pd_send_ec_int();
}

/****************************************************************************/
/* Console commands */
static int command_ec_int(int argc, char **argv)
{
	pd_send_ec_int();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ecint, command_ec_int,
			"",
			"Toggle EC interrupt line",
			NULL);

static int command_pd_host_event(int argc, char **argv)
{
	int event_mask;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	event_mask = strtoi(argv[1], &e, 10);
	if (*e)
		return EC_ERROR_PARAM1;

	pd_send_host_event(event_mask);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pdevent, command_pd_host_event,
			"event_mask",
			"Send PD host event",
			NULL);

static int command_battfake(int argc, char **argv)
{
	char *e;
	int v;

	if (argc == 2) {
		v = strtoi(argv[1], &e, 0);
		if (*e || v < -1 || v > 100)
			return EC_ERROR_PARAM1;

		fake_state_of_charge = v;
	}

	if (fake_state_of_charge < 0) {
		ccprintf("Using real batt level\n");
	} else {
		ccprintf("Using fake batt level %d%%\n",
			 fake_state_of_charge);
	}

	/* Send EC int to get batt info from EC */
	pd_send_ec_int();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battfake, command_battfake,
			"percent (-1 = use real level)",
			"Set fake battery level",
			NULL);

/****************************************************************************/
/* Host commands */
static int ec_status_host_cmd(struct host_cmd_handler_args *args)
{
	const struct ec_params_pd_status *p = args->params;
	struct ec_response_pd_status *r = args->response;

	/* if not using fake soc, then update battery soc */
	board_update_battery_soc(fake_state_of_charge < 0 ?
					p->batt_soc : fake_state_of_charge);

	*r = pd_status;

	/* Clear host event */
	atomic_clear(&(pd_status.status), PD_STATUS_HOST_EVENT);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_EXCHANGE_STATUS, ec_status_host_cmd,
			EC_VER_MASK(0));

static int host_event_status_host_cmd(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_status *r = args->response;

	/* Clear host event bit to avoid sending more unnecessary events */
	atomic_clear(&(pd_status.status), PD_STATUS_HOST_EVENT);

	/* Read and clear the host event status to return to AP */
	r->status = atomic_read_clear(&(host_event_status.status));

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_HOST_EVENT_STATUS, host_event_status_host_cmd,
			EC_VER_MASK(0));
