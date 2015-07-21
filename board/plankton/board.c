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
#include "util.h"

void button_event(enum gpio_signal signal);
void hpd_event(enum gpio_signal signal);
void vbus_event(enum gpio_signal signal);
#include "gpio_list.h"

static volatile uint64_t hpd_prev_ts;
static volatile int hpd_prev_level;
static volatile int hpd_possible_irq;

/* Detect the type of cable used (either single CC or double) */
enum typec_cable {
	TYPEC_CABLE_NONE,
	TYPEC_CABLE_CHECK,
	TYPEC_CABLE_SINGLE_CC,
	TYPEC_CABLE_DOUBLE_CC
};
static enum typec_cable cable;

static int active_cc;
static int host_mode;

static int sn75dp130_dpcd_init(void);

/**
 * Hotplug detect deferred task
 *
 * Called after level change on hpd GPIO to evaluate (and debounce) what event
 * has occurred.  There are 3 events that occur on HPD:
 *    1. low  : downstream display sink is deattached
 *    2. high : downstream display sink is attached
 *    3. irq  : downstream display sink signalling an interrupt.
 *
 * The debounce times for these various events are:
 *   HPD_USTREAM_DEBOUNCE_LVL : min pulse width of level value.
 *   HPD_USTREAM_DEBOUNCE_IRQ : min pulse width of IRQ low pulse.
 *
 * lvl(n-2) lvl(n-1)  lvl   prev_delta  now_delta event
 * ----------------------------------------------------
 * 1        0         1     <IRQ        n/a       low glitch (ignore)
 * 1        0         1     >IRQ        <LVL      irq
 * x        0         1     n/a         >LVL      high
 * 0        1         0     <LVL        n/a       high glitch (ignore)
 * x        1         0     n/a         >LVL      low
 */

void hpd_lvl_deferred(void)
{
	int level = gpio_get_level(GPIO_DPSRC_HPD);
	int dp_mode = !gpio_get_level(GPIO_USBC_SS_USB_MODE);

	if (level != hpd_prev_level) {
		/* Stable level changed. Send HPD event */
		hpd_prev_level = level;
		if (dp_mode)
			pd_send_hpd(0, level ? hpd_high : hpd_low);
		/* Configure redriver's back side */
		if (level)
			sn75dp130_dpcd_init();

	}

	/* Send queued IRQ if the cable is attached */
	if (hpd_possible_irq && level && dp_mode)
		pd_send_hpd(0, hpd_irq);
	hpd_possible_irq = 0;

}
DECLARE_DEFERRED(hpd_lvl_deferred);

void hpd_event(enum gpio_signal signal)
{
	timestamp_t now = get_time();
	int level = gpio_get_level(signal);
	uint64_t cur_delta = now.val - hpd_prev_ts;

	/* Record low pulse */
	if (cur_delta >= HPD_USTREAM_DEBOUNCE_IRQ && level)
		hpd_possible_irq = 1;

	/* store current time */
	hpd_prev_ts = now.val;

	/* All previous hpd level events need to be re-triggered */
	hook_call_deferred(hpd_lvl_deferred, HPD_USTREAM_DEBOUNCE_LVL);
}

/* Debounce time for voltage buttons */
#define BUTTON_DEBOUNCE_US (100 * MSEC)

static enum gpio_signal button_pressed;

static int fake_pd_disconnected;
static int fake_pd_host_mode;
static int fake_pd_disconnect_duration_us;

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
	USBC_ACT_CCD_EN,

	/* Number of USBC actions */
	USBC_ACT_COUNT
};

enum board_src_cap src_cap_mapping[USBC_ACT_COUNT] =
{
	[USBC_ACT_5V_TO_DUT] = SRC_CAP_5V,
	[USBC_ACT_12V_TO_DUT] = SRC_CAP_12V,
	[USBC_ACT_20V_TO_DUT] = SRC_CAP_20V,
};

/**
 * Set the active CC line. The non-active CC line will be left in
 * High-Z, and we will fake the ADC reading for it.
 */
static void set_active_cc(int cc)
{
	active_cc = cc;

	/* High Z for no pull-up or pull-down resistor on CC1 */
	gpio_set_flags_by_mask(GPIO_A, (1 << 2) | (1 << 9), GPIO_INPUT);
	/* High Z for no pull-up or pull-down resistor on CC2 */
	gpio_set_flags_by_mask(GPIO_B, (1 << 6) | (1 << 7), GPIO_INPUT);

	if (cc) {
		if (host_mode)
			/* Pull-up on CC2 */
			gpio_set_flags_by_mask(GPIO_B, (1 << 6), GPIO_OUT_HIGH);
		else
			/* Pull-down on CC2 */
			gpio_set_flags_by_mask(GPIO_B, (1 << 7), GPIO_OUT_LOW);
	} else {
		if (host_mode)
			/* Pull-up on CC1 */
			gpio_set_flags_by_mask(GPIO_A, (1 << 2), GPIO_OUT_HIGH);
		else
			/* Pull-down on CC1 */
			gpio_set_flags_by_mask(GPIO_A, (1 << 9), GPIO_OUT_LOW);
	}
}

/**
 * Detect type-C cable type. Toggle the active CC line until a type-C connection
 * is detected. If a type-C connection can be made in both polarities, then we
 * have a double CC cable, otherwise we have a single CC cable.
 */
static void detect_cc_cable(void)
{
	/*
	 * Delay long enough to guarantee a type-C disconnect will be seen and
	 * a new connection will be made made.
	 */
	hook_call_deferred(detect_cc_cable, PD_T_CC_DEBOUNCE + PD_T_SAFE_0V);

	switch (cable) {
	case TYPEC_CABLE_NONE:
		/* When no cable attached, toggle active CC line */
		if (pd_is_connected(0))
			cable = TYPEC_CABLE_CHECK;
		set_active_cc(!active_cc);
		break;
	case TYPEC_CABLE_CHECK:
		/* If we still have a connection, we have a double CC cable */
		cable = pd_is_connected(0) ? TYPEC_CABLE_DOUBLE_CC :
					     TYPEC_CABLE_SINGLE_CC;
		/* Flip back to original polarity and enable PD comms */
		set_active_cc(!active_cc);
		pd_comm_enable(1);
		break;
	case TYPEC_CABLE_SINGLE_CC:
	case TYPEC_CABLE_DOUBLE_CC:
		/* Check for disconnection and disable PD comms */
		if (!pd_is_connected(0)) {
			cable = TYPEC_CABLE_NONE;
			pd_comm_enable(0);
		}
		break;
	}
}
DECLARE_DEFERRED(detect_cc_cable);

static void fake_disconnect_end(void)
{
	fake_pd_disconnected = 0;
	board_pd_set_host_mode(fake_pd_host_mode);

	/* Restart CC cable detection */
	hook_call_deferred(detect_cc_cable, 500*MSEC);
}
DECLARE_DEFERRED(fake_disconnect_end);

static void fake_disconnect_start(void)
{
	/* Cancel detection of CC cable */
	hook_call_deferred(detect_cc_cable, -1);

	/* Record the current host mode */
	fake_pd_host_mode = !gpio_get_level(GPIO_USBC_CHARGE_EN);
	/* Disable VBUS */
	gpio_set_level(GPIO_VBUS_CHARGER_EN, 0);
	gpio_set_level(GPIO_USBC_VSEL_0, 0);
	gpio_set_level(GPIO_USBC_VSEL_1, 0);
	/* High Z for no pull-up or pull-down resistor on CC1 */
	gpio_set_flags_by_mask(GPIO_A, (1 << 2) | (1 << 9), GPIO_INPUT);
	/* High Z for no pull-up or pull-down resistor on CC2 */
	gpio_set_flags_by_mask(GPIO_B, (1 << 6) | (1 << 7), GPIO_INPUT);

	fake_pd_disconnected = 1;

	hook_call_deferred(fake_disconnect_end,
			   fake_pd_disconnect_duration_us);
}
DECLARE_DEFERRED(fake_disconnect_start);

static void set_usbc_action(enum usbc_action act)
{
	int need_soft_reset;
	int was_usb_mode;

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
		was_usb_mode = gpio_get_level(GPIO_USBC_SS_USB_MODE);
		gpio_set_level(GPIO_USBC_SS_USB_MODE, !was_usb_mode);
		gpio_set_level(GPIO_CASE_CLOSE_EN, !was_usb_mode);
		if (!gpio_get_level(GPIO_DPSRC_HPD))
			break;
		/*
		 * DP cable is connected. Send HPD event according to USB/DP
		 * mux state.
		 */
		if (!was_usb_mode) {
			pd_send_hpd(0, hpd_low);
		} else {
			pd_send_hpd(0, hpd_high);
			pd_send_hpd(0, hpd_irq);
		}
		break;
	case USBC_ACT_USB_EN:
		gpio_set_level(GPIO_USBC_SS_USB_MODE, 1);
		break;
	case USBC_ACT_DP_EN:
		gpio_set_level(GPIO_USBC_SS_USB_MODE, 0);
		break;
	case USBC_ACT_MUX_FLIP:
		/*
		 * For a single CC cable, send custom VDM to flip
		 * USB polarity only. For double CC cable, actually
		 * disconnect and reconnect with opposite polarity.
		 */
		if (cable == TYPEC_CABLE_SINGLE_CC) {
			pd_send_vdm(0, USB_VID_GOOGLE, VDO_CMD_FLIP, NULL, 0);
			gpio_set_level(GPIO_USBC_POLARITY,
				       !gpio_get_level(GPIO_USBC_POLARITY));
		} else if (cable == TYPEC_CABLE_DOUBLE_CC) {
			/*
			 * Fake a disconnection for long enough to guarantee
			 * that we disconnect.
			 */
			hook_call_deferred(fake_disconnect_start, -1);
			hook_call_deferred(fake_disconnect_end, -1);
			fake_pd_disconnect_duration_us = PD_T_SAFE_0V;
			hook_call_deferred(fake_disconnect_start, 0);
			set_active_cc(!active_cc);
		}
		break;
	case USBC_ACT_CABLE_POLARITY0:
		gpio_set_level(GPIO_USBC_POLARITY, 0);
		break;
	case USBC_ACT_CABLE_POLARITY1:
		gpio_set_level(GPIO_USBC_POLARITY, 1);
		break;
	case USBC_ACT_CCD_EN:
		pd_send_vdm(0, USB_VID_GOOGLE, VDO_CMD_CCD_EN, NULL, 0);
		break;
	default:
		break;
	}
}

/* has Pull-up */
static int prev_dbg20v = 1;
static void button_dbg20v_deferred(void);
static void enable_dbg20v_poll(void)
{
	hook_call_deferred(button_dbg20v_deferred, 10 * MSEC);
}

/* Handle debounced button press */
static void button_deferred(void)
{
	if (button_pressed == GPIO_DBG_20V_TO_DUT_L) {
		enable_dbg20v_poll();
		if (gpio_get_level(GPIO_DBG_20V_TO_DUT_L) == prev_dbg20v)
			return;
		else
			prev_dbg20v = !prev_dbg20v;
	}
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
		if (gpio_get_level(GPIO_USBC_SS_USB_MODE))
			board_maybe_reset_usb_hub();
		break;
	case GPIO_DBG_MUX_FLIP_L:
		set_usbc_action(USBC_ACT_MUX_FLIP);
		break;
	case GPIO_DBG_CASE_CLOSE_EN_L:
		set_usbc_action(USBC_ACT_CCD_EN);
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

static void button_dbg20v_deferred(void)
{
	if (gpio_get_level(GPIO_DBG_20V_TO_DUT_L) == 0)
		button_event(GPIO_DBG_20V_TO_DUT_L);
	else
		enable_dbg20v_poll();
}
DECLARE_DEFERRED(button_dbg20v_deferred);

void vbus_event(enum gpio_signal signal)
{
	ccprintf("VBUS! =%d\n", gpio_get_level(signal));
	task_wake(TASK_ID_PD);
}

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

/* 8-bit address */
#define SN75DP130_I2C_ADDR 0x5c
/*
 * Pin number for active-high reset from PCA9534 to CMOS pull-down to
 * SN75DP130's RSTN (active-low)
 */
#define REDRIVER_RST_PIN  0x1

static int sn75dp130_i2c_write(uint8_t index, uint8_t value)
{
	return i2c_write8(I2C_PORT_MASTER, SN75DP130_I2C_ADDR, index, value);
}

/**
 * Reset redriver.
 *
 * Note, MUST set SW15 to 'PD' in order to control i2c from PD-MCU.  This can
 * NOT be done via software.
 */
static int sn75dp130_reset(void)
{
	int rv;

	rv = pca9534_config_pin(I2C_PORT_MASTER, 0x40, REDRIVER_RST_PIN,
				PCA9534_OUTPUT);
	/* Assert (its active high) */
	rv |= pca9534_set_level(I2C_PORT_MASTER, 0x40, REDRIVER_RST_PIN, 1);
	/* datasheet recommends > 100usec */
	usleep(200);

	/* De-assert */
	rv |= pca9534_set_level(I2C_PORT_MASTER, 0x40, REDRIVER_RST_PIN, 0);
	/* datasheet recommends > 400msec */
	usleep(450 * MSEC);
	return rv;
}

static int sn75dp130_dpcd_init(void)
{
	int i, rv;

	/* set upper & middle DPCD addr ... constant for writes below */
	rv = sn75dp130_i2c_write(0x1c, 0x0);
	rv |= sn75dp130_i2c_write(0x1d, 0x1);

	/* link_bw_set: 5.4gbps */
	rv |= sn75dp130_i2c_write(0x1e, 0x0);
	rv |= sn75dp130_i2c_write(0x1f, 0x14);

	/* lane_count_set: 4 */
	rv |= sn75dp130_i2c_write(0x1e, 0x1);
	rv |= sn75dp130_i2c_write(0x1f, 0x4);

	/*
	 * Force Link voltage level & pre-emphasis by writing each of the lane's
	 * DPCD config registers 103-106h accordingly.
	 */
	for (i = 0x3; i < 0x7; i++) {
		rv |= sn75dp130_i2c_write(0x1e, i);
		rv |= sn75dp130_i2c_write(0x1f, 0x3);
	}
	return rv;
}

static int sn75dp130_redriver_init(void)
{
	int rv;

	rv = sn75dp130_reset();

	/* Disable squelch detect */
	rv |= sn75dp130_i2c_write(0x3, 0x1a);
	/* Disable link training on re-driver source side */
	rv |= sn75dp130_i2c_write(0x4, 0x0);

	/* Can only configure DPCD portion of redriver in presence of an HPD */
	if (gpio_get_level(GPIO_DPSRC_HPD))
		sn75dp130_dpcd_init();

	return rv;
}

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
	else if (!strcasecmp(argv[1], "ccd"))
		act = USBC_ACT_CCD_EN;
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
			"<5v|12v|20v|ccd|dev|usb|dp|flip|pol0|pol1>",
			"Set Plankton type-C port state",
			NULL);

int board_in_hub_mode(void)
{
	int ret;
	int level;

	ret = pca9534_config_pin(I2C_PORT_MASTER, 0x40, 6, PCA9534_INPUT);
	if (ret)
		return -1;
	ret = pca9534_get_level(I2C_PORT_MASTER, 0x40, 6, &level);
	if (ret)
		return -1;
	return level;
}

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
	if (board_in_hub_mode() == 1)
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

static int board_pd_fake_disconnected(void)
{
	return fake_pd_disconnected;
}

int board_fake_pd_adc_read(int cc)
{
	if (fake_pd_disconnected) {
		/* Always disconnected */
		return fake_pd_host_mode ? 3000 : 0;
	} else {
		/* Only read the active CC line, fake disconnected on other */
		if (active_cc == cc)
			return adc_read_channel(cc ? ADC_CH_CC2_PD :
						     ADC_CH_CC1_PD);
		else
			return host_mode ? 3000 : 0;
	}
}

/* Set fake PD pull-up/pull-down */
static void board_update_fake_adc_value(int host_mode)
{
	fake_pd_host_mode = host_mode;
}

void board_pd_set_host_mode(int enable)
{
	cprintf(CC_USBPD, "Host mode: %d\n", enable);

	if (board_pd_fake_disconnected()) {
		board_update_fake_adc_value(enable);
		return;
	}

	/* if host mode changed, reset cable type */
	if (host_mode != enable) {
		host_mode = enable;
		cable = TYPEC_CABLE_NONE;
	}

	if (enable) {
		/* Source mode, disable charging */
		gpio_set_level(GPIO_USBC_CHARGE_EN, 0);

		/* Set CC lines */
		set_active_cc(active_cc);
	} else {
		/* Device mode, disable VBUS */
		gpio_set_level(GPIO_VBUS_CHARGER_EN, 0);
		gpio_set_level(GPIO_USBC_VSEL_0, 0);
		gpio_set_level(GPIO_USBC_VSEL_1, 0);

		/* Set CC lines */
		set_active_cc(active_cc);

		/* Enable charging */
		gpio_set_level(GPIO_USBC_CHARGE_EN, 1);
	}
}

static void board_init(void)
{
	timestamp_t now = get_time();
	hpd_prev_level = gpio_get_level(GPIO_DPSRC_HPD);
	hpd_prev_ts = now.val;
	gpio_enable_interrupt(GPIO_DPSRC_HPD);

	/* Enable interrupts on VBUS transitions. */
	gpio_enable_interrupt(GPIO_VBUS_WAKE);

	/* Enable button interrupts. */
	gpio_enable_interrupt(GPIO_DBG_5V_TO_DUT_L);
	gpio_enable_interrupt(GPIO_DBG_12V_TO_DUT_L);
	gpio_enable_interrupt(GPIO_DBG_CHG_TO_DEV_L);
	gpio_enable_interrupt(GPIO_DBG_USB_TOGGLE_L);
	gpio_enable_interrupt(GPIO_DBG_MUX_FLIP_L);
	gpio_enable_interrupt(GPIO_DBG_CASE_CLOSE_EN_L);

	/* TODO(crosbug.com/33761): poll DBG_20V_TO_DUT_L */
	enable_dbg20v_poll();

	ina2xx_init(0, 0x399f, INA2XX_CALIB_1MA(10 /* mOhm */));
	sn75dp130_redriver_init();

	/* Initialize USB hub */
	if (system_get_reset_flags() & RESET_FLAG_POWER_ON)
		hook_call_deferred(board_usb_hub_reset_no_return, 500 * MSEC);

	/* Start detecting CC cable type */
	hook_call_deferred(detect_cc_cable, SECOND);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

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

	fake_pd_disconnect_duration_us = duration_ms * MSEC;
	hook_call_deferred(fake_disconnect_start, delay_ms * MSEC);

	ccprintf("Fake disconnect for %d ms starting in %d ms.\n",
		 duration_ms, delay_ms);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fake_disconnect, cmd_fake_disconnect,
			"<delay_ms> <duration_ms>", NULL, NULL);

static void trigger_dfu_release(void)
{
	gpio_set_level(GPIO_CASE_CLOSE_DFU_L, 1);
	ccprintf("Deasserting CASE_CLOSE_DFU_L.\n");
}
DECLARE_DEFERRED(trigger_dfu_release);

static int cmd_trigger_dfu(int argc, char *argv[])
{
	gpio_set_level(GPIO_CASE_CLOSE_DFU_L, 0);
	ccprintf("Asserting CASE_CLOSE_DFU_L.\n");
	ccprintf("If you expect to see DFU debug but it doesn't show up,\n");
	ccprintf("try flipping the USB type-C cable.\n");
	hook_call_deferred(trigger_dfu_release, 1500 * MSEC);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dfu, cmd_trigger_dfu, NULL, NULL, NULL);
