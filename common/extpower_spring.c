/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control for spring board */

#include "adc.h"
#include "adc_chip.h"
#include "battery.h"
#include "battery_smart.h"
#include "chipset.h"
#include "clock.h"
#include "console.h"
#include "driver/tsu6721.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_mkbp.h"
#include "pmu_tpschrome.h"
#include "pwm.h"
/*
 * TODO(crosbug.com/p/23745): Refactor low-level STM32 ADC code out of this
 * module.  Files in common should not use chip registers directly.
 */
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define PWM_FREQUENCY 32000 /* Hz */

/* Console output macros */
#define CPUTS(outstr) cputs(CC_USBCHARGE, outstr)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

/* ILIM pin control */
enum ilim_config {
	ILIM_CONFIG_MANUAL_OFF,
	ILIM_CONFIG_MANUAL_ON,
	ILIM_CONFIG_PWM,
};

/* Devices that need VBUS power */
#define POWERED_5000_DEVICE_TYPE (TSU6721_TYPE_OTG)
#define POWERED_3300_DEVICE_TYPE (TSU6721_TYPE_JIG_UART_ON)

/* Toad cable */
#define TOAD_DEVICE_TYPE (TSU6721_TYPE_UART | TSU6721_TYPE_AUDIO3)

/* Voltage threshold of D+ for video */
#define VIDEO_ID_THRESHOLD	1300

/*
 * Mapping from PWM duty to current:
 *   Current = A + B * PWM_Duty
 */
#define PWM_MAPPING_A 2958
#define PWM_MAPPING_B (-29)

/* Map current in milli-amps to PWM duty cycle percentage */
#define MA_TO_PWM(curr) (((curr) - PWM_MAPPING_A) / PWM_MAPPING_B)

/* PWM controlled current limit */
#define I_LIMIT_100MA   MA_TO_PWM(100)
#define I_LIMIT_500MA   MA_TO_PWM(500)
#define I_LIMIT_1000MA  MA_TO_PWM(1000)
#define I_LIMIT_1500MA  MA_TO_PWM(1500)
#define I_LIMIT_2000MA  MA_TO_PWM(2000)
#define I_LIMIT_2400MA  MA_TO_PWM(2400)
#define I_LIMIT_3000MA  0

/* PWM control loop parameters */
#define PWM_CTRL_MAX_DUTY	I_LIMIT_100MA /* Minimum current */
#define PWM_CTRL_BEGIN_OFFSET	90
#define PWM_CTRL_OC_MARGIN	15
#define PWM_CTRL_OC_DETECT_TIME	(1200 * MSEC)
#define PWM_CTRL_OC_BACK_OFF	3
#define PWM_CTRL_OC_RETRY	2
#define PWM_CTRL_STEP_DOWN	3
#define PWM_CTRL_STEP_UP	5
#define PWM_CTRL_VBUS_HARD_LOW	4400
#define PWM_CTRL_VBUS_LOW	4500
#define PWM_CTRL_VBUS_HIGH	4700 /* Must be higher than 4.5V */
#define PWM_CTRL_VBUS_HIGH_500MA 4550

/* Delay before notifying kernel of device type change */
#define BATTERY_KEY_DELAY (PWM_CTRL_OC_DETECT_TIME + 400 * MSEC)

/* Delay for signals to settle */
#define DELAY_POWER_MS		20
#define DELAY_USB_DP_DN_MS	20
#define DELAY_ID_MUX_MS		30
#define CABLE_DET_POLL_MS	100
#define CABLE_DET_POLL_COUNT	6

/* Current sense resistor values */
#define R_INPUT_MOHM 20 /* mOhm */
#define R_BATTERY_MOHM 33 /* mOhm */

static int current_dev_type = TSU6721_TYPE_NONE;
static int nominal_pwm_duty;
static int current_pwm_duty;
static int user_pwm_duty = -1;

static int pending_tsu6721_reset;
static int pending_adc_watchdog_disable;
static int pending_dev_type_update;
static int pending_video_power_off;
static int restore_id_mux;

static enum {
	ADC_WATCH_NONE,
	ADC_WATCH_TOAD,
	ADC_WATCH_USB,
} current_watchdog = ADC_WATCH_NONE;

struct {
	int type;
	const char *name;
} const known_dev_types[] = {
	{TSU6721_TYPE_OTG, "OTG"},
	{TSU6721_TYPE_USB_HOST, "USB"},
	{TSU6721_TYPE_CHG12, "Type-1/2-Chg"},
	{TSU6721_TYPE_NON_STD_CHG, "Non-Std-Chg"},
	{TSU6721_TYPE_DCP, "DCP"},
	{TSU6721_TYPE_CDP, "CDP"},
	{TSU6721_TYPE_U200_CHG, "U200-Chg"},
	{TSU6721_TYPE_APPLE_CHG, "Apple-Chg"},
	{TSU6721_TYPE_JIG_UART_ON, "Video"},
	{TSU6721_TYPE_AUDIO3, "Audio-3"},
	{TSU6721_TYPE_UART, "UART"},
	{TSU6721_TYPE_VBUS_DEBOUNCED, "Power"} };

/*
 * Last time we see a power source removed. Also records the power source
 * type and PWM duty cycle at that moment.
 * Index: 0 = Unknown power source.
 *        1 = Recognized power source.
 */
static timestamp_t power_removed_time[2];
static uint32_t power_removed_type[2];
static int power_removed_pwm_duty[2];
static int oc_detect_retry[2] = {PWM_CTRL_OC_RETRY, PWM_CTRL_OC_RETRY};

/* PWM duty cycle limit based on over current event */
static int over_current_pwm_duty;

static enum ilim_config current_ilim_config = ILIM_CONFIG_MANUAL_OFF;

static const int apple_charger_type[4] = {I_LIMIT_500MA,
					  I_LIMIT_1000MA,
					  I_LIMIT_2000MA,
					  I_LIMIT_2400MA};

static int video_power_enabled;

#define NON_STD_CHARGER_REDETECT_DELAY (600 * MSEC)
static enum {
	NO_REDETECT,
	REDETECT_SCHEDULED,
	REDETECTED,
} charger_need_redetect = NO_REDETECT;
static timestamp_t charger_redetection_time;

/**
 * Directly read discharge current in mA; negative = charging.
 */
static int battery_current(int *current)
{
	int rv, d;

	rv = sb_read(SB_CURRENT, &d);
	if (rv) {
		*current = 0;
		return rv;
	}

	*current = (int16_t)d;
	return EC_SUCCESS;
}

static int get_video_power(void)
{
	return video_power_enabled;
}

static void set_video_power(int enabled)
{
	int power_good;

	pmu_enable_fet(FET_VIDEO, enabled, enabled ? &power_good : NULL);
	if (enabled && !power_good)
		pmu_enable_fet(FET_VIDEO, 0, NULL);
	video_power_enabled = enabled;
}

static void ilim_use_gpio(void)
{
	pwm_enable(PWM_CH_ILIM, 0);
	gpio_set_flags(GPIO_ILIM, GPIO_OUTPUT);
}

/**
 * Set ILIM pin control type.
 */
static void ilim_config(enum ilim_config config)
{
	if (config == current_ilim_config)
		return;
	current_ilim_config = config;

	switch (config) {
	case ILIM_CONFIG_MANUAL_OFF:
	case ILIM_CONFIG_MANUAL_ON:
		ilim_use_gpio();
		gpio_set_level(GPIO_ILIM,
			       config == ILIM_CONFIG_MANUAL_ON ? 1 : 0);
		break;
	case ILIM_CONFIG_PWM:
		pwm_enable(PWM_CH_ILIM, 1);
		break;
	default:
		break;
	}
}

/**
 * Return Apple charger current limit.
 */
static int apple_charger_current(void)
{
	int vp, vn;
	int type = 0;
	int data[ADC_CH_COUNT];

	tsu6721_disable_interrupts();
	tsu6721_mux(TSU6721_MUX_USB);
	/* Wait for signal to stablize */
	msleep(DELAY_USB_DP_DN_MS);
	adc_read_all_channels(data);
	vp = data[ADC_CH_USB_DP_SNS];
	vn = data[ADC_CH_USB_DN_SNS];
	tsu6721_mux(TSU6721_MUX_AUTO);
	tsu6721_enable_interrupts();
	if (vp > 1215)
		type |= 0x2;
	if (vn > 1215)
		type |= 0x1;

	return apple_charger_type[type];
}

static int hard_current_limit(int limit)
{
	/*
	 * The PWM duty cycle goes lower than the nominal
	 * cycle for PWM_CTRL_OC_MARGIN. Therefore, increase duty cycle by
	 * PWM_CTRL_OC_MARGIN avoids going over the hard limit.
	 * (Note that lower PWM cycle translates to higher current)
	 */
	return MIN(limit + PWM_CTRL_OC_MARGIN, 100);
}

static int video_dev_type(int device_type)
{
	return (device_type & ~TSU6721_TYPE_USB_HOST) |
	       TSU6721_TYPE_JIG_UART_ON;
}

static int usb_video_id_present(void)
{
	return adc_read_channel(ADC_CH_USB_DP_SNS) > VIDEO_ID_THRESHOLD;
}

static int usb_poll_video_id(void)
{
	int i;
	for (i = 0; i < CABLE_DET_POLL_COUNT; ++i) {
		msleep(CABLE_DET_POLL_MS);
		if (usb_video_id_present())
			return 1;
	}
	return 0;
}

static int probe_video(int device_type)
{
	tsu6721_disable_interrupts();
	gpio_set_level(GPIO_ID_MUX, 1);
	msleep(DELAY_ID_MUX_MS);

	if (usb_poll_video_id()) {
		/* Not USB host but video */
		device_type = video_dev_type(device_type);
		return device_type;
	} else {
		if (adc_read_channel(ADC_CH_USB_VBUS_SNS) > 3500) {
			/*
			 * Either USB host or video dongle.
			 * Leave ID_MUX high so we see the change on
			 * DP_SNS if any.
			 *
			 * ADC watchdog is responsible for sensing a
			 * detach event and switch back ID_MUX.
			 */
			return device_type;
		} else {
			/* Unhandled unpowered video dongle. Ignore it. */
			gpio_set_level(GPIO_ID_MUX, 0);
			msleep(DELAY_ID_MUX_MS);
			tsu6721_enable_interrupts();
			return TSU6721_TYPE_NONE;
		}
	}
}

/**
 * Set PWM duty cycle.
 */
static void set_pwm_duty_cycle(int percent)
{
	if (current_ilim_config != ILIM_CONFIG_PWM)
		ilim_config(ILIM_CONFIG_PWM);
	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	pwm_set_duty(PWM_CH_ILIM, percent);
	current_pwm_duty = percent;
}

/**
 * Returns next lower PWM duty cycle, or -1 for unchanged duty cycle.
 */
static int pwm_get_next_lower(void)
{
	if (current_pwm_duty > nominal_pwm_duty -
			       PWM_CTRL_OC_MARGIN &&
	    current_pwm_duty > over_current_pwm_duty &&
	    current_pwm_duty > 0)
		return MAX(current_pwm_duty - PWM_CTRL_STEP_DOWN, 0);
	return -1;
}

static int pwm_check_vbus_low(int vbus, int battery_current)
{
	if (battery_current >= 0)
		return vbus < PWM_CTRL_VBUS_LOW && current_pwm_duty < 100;
	else
		return vbus < PWM_CTRL_VBUS_HARD_LOW && current_pwm_duty < 100;
}

static int pwm_check_vbus_high(int vbus)
{
	if (vbus > PWM_CTRL_VBUS_HIGH)
		return 1;
	if (vbus > PWM_CTRL_VBUS_HIGH_500MA && current_pwm_duty > I_LIMIT_500MA)
		return 1;
	return 0;
}

static void pwm_nominal_duty_cycle(int percent)
{
	int new_percent = percent;

	new_percent += PWM_CTRL_BEGIN_OFFSET;
	new_percent = MIN(new_percent, PWM_CTRL_MAX_DUTY);

	set_pwm_duty_cycle(new_percent);
	nominal_pwm_duty = percent;
}

static void adc_watch_vbus(int high, int low)
{
	adc_enable_watchdog(STM32_AIN(5), high, low);
	task_clear_pending_irq(STM32_IRQ_ADC_1);
	task_enable_irq(STM32_IRQ_ADC_1);
}

static void adc_watch_toad(void)
{
	/* Watch VBUS and interrupt if voltage goes under 3V. */
	adc_watch_vbus(4095, 1800);
	current_watchdog = ADC_WATCH_TOAD;
}

static void adc_watch_usb(void)
{
	/* Watch VBUS and interrupt if voltage goes under 3V. */
	adc_watch_vbus(4095, 1800);
	current_watchdog = ADC_WATCH_USB;
}

static int usb_has_power_input(int dev_type)
{
	if (dev_type & TSU6721_TYPE_JIG_UART_ON)
		return 1;
	return (dev_type & TSU6721_TYPE_VBUS_DEBOUNCED) &&
	       !(dev_type & POWERED_5000_DEVICE_TYPE);
}

static int usb_need_boost(int dev_type)
{
	if (dev_type & POWERED_5000_DEVICE_TYPE)
		return 0;
	if (chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_SUSPEND))
		return 1;
	return (dev_type != TSU6721_TYPE_NONE);
}

static void usb_boost_power_hook(int power_on)
{
	if (current_dev_type == TSU6721_TYPE_NONE)
		gpio_set_level(GPIO_BOOST_EN, power_on);
	else if (current_dev_type & TSU6721_TYPE_JIG_UART_ON)
		set_video_power(power_on);
}

static int usb_charger_removed(int dev_type)
{
	if (!(current_dev_type & TSU6721_TYPE_VBUS_DEBOUNCED))
		return 0;

	/* Charger is removed */
	if (dev_type == TSU6721_TYPE_NONE)
		return 1;

	/*
	 * Device type changed from known type to unknown type. Assuming
	 * it went away and came back.
	 */
	if ((current_dev_type != TSU6721_TYPE_VBUS_DEBOUNCED) &&
	    (dev_type == TSU6721_TYPE_VBUS_DEBOUNCED))
		return 1;

	return 0;
}

/**
 * Detect over-current events.
 *
 * When a power source is removed, record time, power source type, and PWM duty
 * cycle. Then when we see a power source, compare type and calculate time
 * difference to determine if we have just encountered an over current event.
 */
static void usb_detect_overcurrent(int dev_type)
{
	if (usb_charger_removed(dev_type)) {
		int idx = !(current_dev_type == TSU6721_TYPE_VBUS_DEBOUNCED);
		power_removed_time[idx] = get_time();
		power_removed_type[idx] = current_dev_type;
		power_removed_pwm_duty[idx] = current_pwm_duty;
	} else if (dev_type & TSU6721_TYPE_VBUS_DEBOUNCED) {
		int idx = !(dev_type == TSU6721_TYPE_VBUS_DEBOUNCED);
		timestamp_t now = get_time();
		now.val -= power_removed_time[idx].val;
		if (now.val >= PWM_CTRL_OC_DETECT_TIME) {
			oc_detect_retry[idx] = PWM_CTRL_OC_RETRY;
			return;
		}
		if (power_removed_type[idx] == dev_type) {
			if (oc_detect_retry[idx] > 0) {
				CPRINTS("USB overcurrent: Retry (%d)",
					oc_detect_retry[idx]);
				oc_detect_retry[idx]--;
				return;
			}
			over_current_pwm_duty = power_removed_pwm_duty[idx] +
						PWM_CTRL_OC_BACK_OFF;
			CPRINTS("USB overcurrent: Limited to %d%%",
				over_current_pwm_duty);
		}
	}
}

/**
 * Supply 5V VBUS if needed.
 *
 * If we toggle power output, wait for a moment, and then update device
 * type. To avoid race condition, check if power requirement changes during
 * this time.
 */
static int usb_manage_boost(int dev_type)
{
	int need_boost;
	int retry_limit = 3;

	do {
		if (retry_limit-- <= 0)
			break;

		need_boost = usb_need_boost(dev_type);
		if (need_boost != gpio_get_level(GPIO_BOOST_EN)) {
			gpio_set_level(GPIO_BOOST_EN, need_boost);
			msleep(DELAY_POWER_MS);
			dev_type = tsu6721_get_device_type();
			if (gpio_get_level(GPIO_ID_MUX))
				dev_type = video_dev_type(dev_type);
		}
	} while (need_boost == !usb_need_boost(dev_type));

	return dev_type;
}

/**
 * Update ILIM current limit according to device type.
 */
static void usb_update_ilim(int dev_type)
{
	if (usb_has_power_input(dev_type)) {
		/* Limit USB port current. 500mA for not listed types. */
		int current_limit = I_LIMIT_500MA;
		if (dev_type & TSU6721_TYPE_CHG12)
			current_limit = I_LIMIT_3000MA;
		else if (dev_type & TSU6721_TYPE_APPLE_CHG)
			current_limit = apple_charger_current();
		else if (dev_type & TSU6721_TYPE_CDP)
			current_limit = I_LIMIT_1500MA;
		else if (dev_type & TSU6721_TYPE_DCP)
			current_limit = hard_current_limit(I_LIMIT_1500MA);
		else if (dev_type & TSU6721_TYPE_JIG_UART_ON)
			current_limit = hard_current_limit(I_LIMIT_2000MA);
		else if (dev_type & TOAD_DEVICE_TYPE)
			current_limit = hard_current_limit(I_LIMIT_500MA);
		else if (dev_type == TSU6721_TYPE_VBUS_DEBOUNCED)
			current_limit = hard_current_limit(I_LIMIT_100MA);

		pwm_nominal_duty_cycle(current_limit);
	} else {
		ilim_config(ILIM_CONFIG_MANUAL_ON);
	}
}

static void usb_log_dev_type(int dev_type)
{
	int i = sizeof(known_dev_types) / sizeof(known_dev_types[0]);

	CPRINTF("[%T USB: 0x%06x", dev_type);
	for (--i; i >= 0; --i)
		if (dev_type & known_dev_types[i].type)
			CPRINTF(" %s", known_dev_types[i].name);
	CPRINTF("]\n");
}

static void send_battery_key_deferred(void)
{
	keyboard_send_battery_key();
}
DECLARE_DEFERRED(send_battery_key_deferred);

static void notify_dev_type_change(int dev_type)
{
	usb_log_dev_type(dev_type);
	current_dev_type = dev_type;
	hook_call_deferred(send_battery_key_deferred, BATTERY_KEY_DELAY);
}

static void usb_device_change(int dev_type)
{

	if (current_dev_type == dev_type)
		return;

	over_current_pwm_duty = 0;

	/*
	 * Video output is recognized incorrectly as USB host. When we see
	 * USB host, probe for video output.
	 */
	if (dev_type & TSU6721_TYPE_USB_HOST)
		dev_type = probe_video(dev_type);

	usb_detect_overcurrent(dev_type);

	dev_type = usb_manage_boost(dev_type);

	/* Supply 3.3V VBUS if needed. */
	if (dev_type & POWERED_3300_DEVICE_TYPE)
		set_video_power(1);

	usb_update_ilim(dev_type);

	if ((dev_type & TOAD_DEVICE_TYPE) &&
	    (dev_type & TSU6721_TYPE_VBUS_DEBOUNCED))
		adc_watch_toad();
	else if (dev_type & TSU6721_TYPE_USB_HOST)
		adc_watch_usb();

	if (dev_type != current_dev_type) {
		if ((dev_type & TSU6721_TYPE_NON_STD_CHG ||
		     dev_type == TSU6721_TYPE_VBUS_DEBOUNCED) &&
		    charger_need_redetect == NO_REDETECT) {
			/* Schedule redetection */
			charger_need_redetect = REDETECT_SCHEDULED;
			charger_redetection_time = get_time();
			charger_redetection_time.val +=
				NON_STD_CHARGER_REDETECT_DELAY;
		} else if (dev_type != TSU6721_TYPE_VBUS_DEBOUNCED &&
			   !(dev_type & TSU6721_TYPE_NON_STD_CHG)) {
			/* Not non-std charger. Disarm redetection timer. */
			charger_need_redetect = NO_REDETECT;
		}
		notify_dev_type_change(dev_type);
	}

	if (dev_type)
		disable_sleep(SLEEP_MASK_USB_PWR);
	else
		enable_sleep(SLEEP_MASK_USB_PWR);
}

/*****************************************************************************/
/* External API */

/*
 * TODO(crosbug.com/p/23741): Init here until we can do with HOOK_INIT.  Just
 * need to set prio so we init before the charger task does.
 */
void extpower_charge_init(void)
{
	set_pwm_duty_cycle(I_LIMIT_500MA);

	/*
	 * TODO(crosbug.com/p/23742): For some reason the TSU6721 comes up very
	 * slowly.  Wait for a while before accessing it.  This delay seems to
	 * be long enough.  Once we understand the slow start, it may be
	 * possible to remove this delay.
	 */
	msleep(500);

	tsu6721_reset();
	gpio_enable_interrupt(GPIO_USB_CHG_INT);

	/* TODO(crosbug.com/p/23742): Need delay after reset as well */
	msleep(100);

	extpower_charge_update(1);
}

void extpower_charge_update(int force_update)
{
	int int_val = 0;

	if (restore_id_mux) {
		gpio_set_level(GPIO_ID_MUX, 0);
		msleep(DELAY_ID_MUX_MS);
		restore_id_mux = 0;
	}

	if (pending_adc_watchdog_disable) {
		current_watchdog = ADC_WATCH_NONE;
		adc_disable_watchdog();
		pending_adc_watchdog_disable = 0;
	}

	if (pending_video_power_off) {
		set_video_power(0);
		pending_video_power_off = 0;
	}

	if (pending_tsu6721_reset) {
		tsu6721_reset();
		force_update = 1;
		pending_tsu6721_reset = 0;
	}

	if (pending_dev_type_update) {
		force_update = 1;
		pending_dev_type_update = 0;
	}

	/*
	 * Check device type except when:
	 *   1. Current device type is non-standard charger or undetermined
	 *      charger type. This is handled by charger re-detection.
	 *   2. ID_MUX=1. This is handled by ADC watchdog.
	 */
	if (current_dev_type != TSU6721_TYPE_VBUS_DEBOUNCED &&
	    !(current_dev_type & TSU6721_TYPE_NON_STD_CHG) &&
	    gpio_get_level(GPIO_ID_MUX) == 0)
		force_update |= (tsu6721_get_device_type() != current_dev_type);

	if (!force_update)
		int_val = tsu6721_get_interrupts();

	if (int_val & TSU6721_INT_DETACH)
		usb_device_change(TSU6721_TYPE_NONE);
	else if (int_val || force_update)
		usb_device_change(tsu6721_get_device_type());
}

int extpower_charge_needs_update(void)
{
	return tsu6721_peek_interrupts();
}

int extpower_is_present(void)
{
	static int last_vbus;
	int vbus, vbus_good;

	if (!gpio_get_level(GPIO_BOOST_EN))
		return 0;

	/*
	 * UVLO is 4.1V. We consider AC bad when its voltage drops below 4.2V
	 * for two consecutive samples. This is to give PWM a chance to bring
	 * voltage up.
	 */
	vbus = adc_read_channel(ADC_CH_USB_VBUS_SNS);
	vbus_good = (vbus >= 4200 || last_vbus >= 4200);
	last_vbus = vbus;

	return vbus_good;
}

void extpower_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_CHARGER);
}

/*****************************************************************************/
/* Hooks */

void adc_watchdog_interrupt(void)
{
	switch (current_watchdog) {
	case ADC_WATCH_USB:
		restore_id_mux = 1;
		/* Fall through */
	case ADC_WATCH_TOAD:
		pending_tsu6721_reset = 1;
		pending_adc_watchdog_disable = 1;
		task_disable_irq(STM32_IRQ_ADC_1);
		task_wake(TASK_ID_CHARGER);
		break;
	default:
		break;
	}
}
DECLARE_IRQ(STM32_IRQ_ADC_1, adc_watchdog_interrupt, 2);

static void usb_boost_pwr_on_hook(void)
{
	usb_boost_power_hook(1);
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, usb_boost_pwr_on_hook, HOOK_PRIO_DEFAULT);

static void usb_boost_pwr_off_hook(void)
{
	usb_boost_power_hook(0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, usb_boost_pwr_off_hook, HOOK_PRIO_DEFAULT);

static void pwm_tweak(void)
{
	int vbus, current;
	int next;

	if (current_ilim_config != ILIM_CONFIG_PWM)
		return;

	vbus = adc_read_channel(ADC_CH_USB_VBUS_SNS);
	if (battery_current(&current))
		current = 0;

	if (user_pwm_duty >= 0) {
		if (current_pwm_duty != user_pwm_duty)
			set_pwm_duty_cycle(user_pwm_duty);
		return;
	}

	/*
	 * If VBUS voltage is too low:
	 *   - If battery is discharging, throttling more is going to draw
	 *     more current from the battery, so do nothing unless VBUS is
	 *     about to be lower than AC good threshold.
	 *   - Otherwise, throttle input current to raise VBUS voltage.
	 * If VBUS voltage is high enough, allow more current until we hit
	 * current limit target.
	 */
	if (pwm_check_vbus_low(vbus, current)) {
		set_pwm_duty_cycle(current_pwm_duty + PWM_CTRL_STEP_UP);
		CPRINTS("PWM duty up %d%%", current_pwm_duty);
	} else if (pwm_check_vbus_high(vbus)) {
		next = pwm_get_next_lower();
		if (next >= 0) {
			set_pwm_duty_cycle(next);
			CPRINTS("PWM duty down %d%%", current_pwm_duty);
		}
	}
}
DECLARE_HOOK(HOOK_SECOND, pwm_tweak, HOOK_PRIO_DEFAULT);

static void usb_detach_video(void)
{
	if (!(current_dev_type & TSU6721_TYPE_JIG_UART_ON))
		return;
	pending_video_power_off = 1;
	restore_id_mux = 1;
	pending_tsu6721_reset = 1;
	task_wake(TASK_ID_CHARGER);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, usb_detach_video, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, usb_detach_video, HOOK_PRIO_DEFAULT);

static void usb_monitor_detach(void)
{
	int vbus;

	if (!(current_dev_type & TSU6721_TYPE_JIG_UART_ON))
		return;

	if (!usb_video_id_present()) {
		usb_detach_video();
		return;
	}

	/* Check if there is external power */
	vbus = adc_read_channel(ADC_CH_USB_VBUS_SNS);
	if (get_video_power() && vbus > 4000) {
		set_video_power(0);
		notify_dev_type_change(current_dev_type |
				       TSU6721_TYPE_VBUS_DEBOUNCED);
	} else if (!get_video_power() && vbus <= 4000) {
		set_pwm_duty_cycle(100);
		set_video_power(1);
		notify_dev_type_change(current_dev_type &
				       ~TSU6721_TYPE_VBUS_DEBOUNCED);
	}
}
DECLARE_HOOK(HOOK_SECOND, usb_monitor_detach, HOOK_PRIO_DEFAULT);

static void usb_monitor_cable_det(void)
{
	if (!(current_dev_type & TSU6721_TYPE_USB_HOST))
		return;

	if (usb_video_id_present())
		adc_watchdog_interrupt();
}
DECLARE_HOOK(HOOK_SECOND, usb_monitor_cable_det, HOOK_PRIO_DEFAULT);

static void usb_charger_redetect(void)
{
	if (charger_need_redetect != REDETECT_SCHEDULED)
		return;

	if (timestamp_expired(charger_redetection_time, NULL)) {
		CPRINTS("USB Redetecting");
		/*
		 * TSU6721 doesn't update device type if power or ID pin
		 * is present. Therefore, if the device type is the same,
		 * we need to reset TSU6721 to force a redetection.
		 */
		if (tsu6721_get_device_type() == current_dev_type)
			pending_tsu6721_reset = 1;
		else
			pending_dev_type_update = 1;
		if (gpio_get_level(GPIO_ID_MUX))
			restore_id_mux = 1;
		charger_need_redetect = REDETECTED;
		task_wake(TASK_ID_CHARGER);
	}
}
DECLARE_HOOK(HOOK_SECOND, usb_charger_redetect, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Console commands for debugging */

#ifdef CONFIG_CMD_ILIM
static int command_ilim(int argc, char **argv)
{
	char *e;
	int v;

	if (argc >= 2) {
		if (parse_bool(argv[1], &v)) {
			ilim_config(v ? ILIM_CONFIG_MANUAL_ON :
					  ILIM_CONFIG_MANUAL_OFF);
		} else {
			v = strtoi(argv[1], &e, 0);
			if (*e)
				return EC_ERROR_PARAM1;
			set_pwm_duty_cycle(v);
		}
	}

	if (current_ilim_config == ILIM_CONFIG_MANUAL_ON)
		ccprintf("ILIM is GPIO high\n");
	else if (current_ilim_config == ILIM_CONFIG_MANUAL_OFF)
		ccprintf("ILIM is GPIO low\n");
	else
		ccprintf("ILIM is PWM duty cycle %d%%\n",
			 pwm_get_duty(PWM_CH_ILIM));

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ilim, command_ilim,
		"[percent | on | off]",
		"Set or show ILIM duty cycle/GPIO value",
		NULL);
#endif /* CONFIG_CMD_ILIM */

#ifdef CONFIG_CMD_BATDEBUG
static int command_batdebug(int argc, char **argv)
{
	struct batt_params batt;

	battery_get_params(&batt);

	ccprintf("VBUS = %d mV\n", adc_read_channel(ADC_CH_USB_VBUS_SNS));
	ccprintf("VAC = %d mV\n", pmu_adc_read(ADC_VAC, ADC_FLAG_KEEP_ON)
				  * 17000 / 1024);
	ccprintf("IAC = %d mA\n", pmu_adc_read(ADC_IAC, ADC_FLAG_KEEP_ON)
				  * (1000 / R_INPUT_MOHM) * 33 / 1024);
	ccprintf("VBAT = %d mV\n", pmu_adc_read(ADC_VBAT, ADC_FLAG_KEEP_ON)
				  * 17000 / 1024);
	ccprintf("IBAT = %d mA\n", pmu_adc_read(ADC_IBAT, 0)
				  * (1000 / R_BATTERY_MOHM) * 40 / 1024);
	ccprintf("PWM = %d%%\n", pwm_get_duty(PWM_CH_ILIM));
	ccprintf("Battery Current = %d mA\n", batt.current);
	ccprintf("Battery Voltage= %d mV\n", batt.voltage);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(batdebug, command_batdebug,
			NULL, NULL, NULL);
#endif /* CONFIG_CMD_BATDEBUG */

/*****************************************************************************/
/* Host commands */

static int ext_power_command_current_limit(struct host_cmd_handler_args *args)
{
	const struct ec_params_ext_power_current_limit *p = args->params;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	user_pwm_duty = ((int)(p->limit) - PWM_MAPPING_A) / PWM_MAPPING_B;

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_EXT_POWER_CURRENT_LIMIT,
		     ext_power_command_current_limit,
		     EC_VER_MASK(0));

static int power_command_info(struct host_cmd_handler_args *args)
{
	struct ec_response_power_info *r = args->response;

	r->voltage_ac = adc_read_channel(ADC_CH_USB_VBUS_SNS);
	r->voltage_system = pmu_adc_read(ADC_VAC, ADC_FLAG_KEEP_ON)
			  * 17000 / 1024;
	r->current_system = pmu_adc_read(ADC_IAC, 0)
			  * (1000 / R_INPUT_MOHM) * 33 / 1024;
	r->usb_dev_type = current_dev_type;

	/* Approximate value by PWM duty cycle */
	r->usb_current_limit = PWM_MAPPING_A + PWM_MAPPING_B * current_pwm_duty;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_POWER_INFO, power_command_info, EC_VER_MASK(0));
