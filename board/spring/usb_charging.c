/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control for spring board */

#include "adc.h"
#include "board.h"
#include "console.h"
#include "hooks.h"
#include "gpio.h"
#include "lp5562.h"
#include "keyboard_scan.h"
#include "pmu_tpschrome.h"
#include "registers.h"
#include "smart_battery.h"
#include "stm32_adc.h"
#include "task.h"
#include "timer.h"
#include "tsu6721.h"
#include "util.h"

#define PWM_FREQUENCY 32000 /* Hz */

/* Console output macros */
#define CPUTS(outstr) cputs(CC_USBCHARGE, outstr)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* Devices that need VBUS power */
#define POWERED_5000_DEVICE_TYPE (TSU6721_TYPE_OTG)
#define POWERED_3300_DEVICE_TYPE (TSU6721_TYPE_JIG_UART_ON)

/* Voltage threshold of D+ for video */
#define VIDEO_ID_THRESHOLD	1335

/* PWM controlled current limit */
#define I_LIMIT_500MA   90
#define I_LIMIT_1000MA  75
#define I_LIMIT_1500MA  60
#define I_LIMIT_2000MA  50
#define I_LIMIT_2400MA  35
#define I_LIMIT_3000MA  0

/* PWM control loop parameters */
#define PWM_CTRL_MAX_DUTY	96 /* Minimum current for dead battery */
#define PWM_CTRL_BEGIN_OFFSET	30
#define PWM_CTRL_OC_MARGIN	15
#define PWM_CTRL_OC_DETECT_TIME	(800 * MSEC)
#define PWM_CTRL_OC_BACK_OFF	3
#define PWM_CTRL_STEP_DOWN	2
#define PWM_CTRL_STEP_UP	5
#define PWM_CTRL_VBUS_HARD_LOW	4400
#define PWM_CTRL_VBUS_LOW	4500
#define PWM_CTRL_VBUS_HIGH	4700 /* Must be higher than 4.5V */

/* Delay for signals to settle */
#define DELAY_POWER_MS		20
#define DELAY_USB_DP_DN_MS	20
#define DELAY_ID_MUX_MS		30

static int current_dev_type = TSU6721_TYPE_NONE;
static int nominal_pwm_duty;
static int current_pwm_duty;

static enum {
	LIMIT_NORMAL,
	LIMIT_AGGRESSIVE,
} current_limit_mode = LIMIT_AGGRESSIVE;

/*
 * Last time we see a power source removed. Also records the power source
 * type and PWM duty cycle at that moment.
 * Index: 0 = Unknown power source.
 *        1 = Recognized power source.
 */
static timestamp_t power_removed_time[2];
static uint32_t power_removed_type[2];
static int power_removed_pwm_duty[2];

/* PWM duty cycle limit based on over current event */
static int over_current_pwm_duty;

static enum ilim_config current_ilim_config = ILIM_CONFIG_MANUAL_OFF;

static const int apple_charger_type[4] = {I_LIMIT_500MA,
					  I_LIMIT_1000MA,
					  I_LIMIT_2000MA,
					  I_LIMIT_2400MA};

static void board_ilim_use_gpio(void)
{
	/* Disable counter */
	STM32_TIM_CR1(3) &= ~0x1;

	/* Disable TIM3 clock */
	STM32_RCC_APB1ENR &= ~0x2;

	/* Switch to GPIO */
	gpio_set_flags(GPIO_ILIM, GPIO_OUTPUT);
}

static void board_ilim_use_pwm(void)
{
	uint32_t val;

	/* Config alt. function (TIM3/PWM) */
	val = STM32_GPIO_CRL_OFF(GPIO_B) & ~0x000f0000;
	val |= 0x00090000;
	STM32_GPIO_CRL_OFF(GPIO_B) = val;

	/* Enable TIM3 clock */
	STM32_RCC_APB1ENR |= 0x2;

	/* Disable counter during setup */
	STM32_TIM_CR1(3) = 0x0000;

	/*
	 * CPU_CLOCK / (PSC + 1) determines how fast the counter operates.
	 * ARR determines the wave period, CCRn determines duty cycle.
	 * Thus, frequency = CPU_CLOCK / (PSC + 1) / ARR.
	 *
	 * Assuming 16MHz clock and ARR=100, PSC needed to achieve PWM_FREQUENCY
	 * is: PSC = CPU_CLOCK / PWM_FREQUENCY / ARR - 1
	 */
	STM32_TIM_PSC(3) = CPU_CLOCK / PWM_FREQUENCY / 100 - 1; /* pre-scaler */
	STM32_TIM_ARR(3) = 100;			/* auto-reload value */
	STM32_TIM_CCR1(3) = 100;		/* duty cycle */

	/* CC1 configured as output, PWM mode 1, preload enable */
	STM32_TIM_CCMR1(3) = (6 << 4) | (1 << 3);

	/* CC1 output enable, active high */
	STM32_TIM_CCER(3) = (1 << 0);

	/* Generate update event to force loading of shadow registers */
	STM32_TIM_EGR(3) |= 1;

	/* Enable auto-reload preload, start counting */
	STM32_TIM_CR1(3) |= (1 << 7) | (1 << 0);
}

void board_ilim_config(enum ilim_config config)
{
	if (config == current_ilim_config)
		return;
	current_ilim_config = config;

	switch (config) {
	case ILIM_CONFIG_MANUAL_OFF:
	case ILIM_CONFIG_MANUAL_ON:
		board_ilim_use_gpio();
		gpio_set_level(GPIO_ILIM,
			       config == ILIM_CONFIG_MANUAL_ON ? 1 : 0);
		break;
	case ILIM_CONFIG_PWM:
		board_ilim_use_pwm();
		break;
	default:
		break;
	}
}

/* Returns Apple charger current limit */
static int board_apple_charger_current(void)
{
	int vp, vn;
	int type = 0;
	int data[ADC_CH_COUNT];

	/* TODO(victoryang): Handle potential race condition. */
	tsu6721_disable_interrupts();
	tsu6721_mux(TSU6721_MUX_USB);
	/* Wait 20ms for signal to stablize */
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

static int board_probe_video(int device_type)
{
	tsu6721_disable_interrupts();
	gpio_set_level(GPIO_ID_MUX, 1);
	msleep(DELAY_ID_MUX_MS);

	if (adc_read_channel(ADC_CH_USB_DP_SNS) > VIDEO_ID_THRESHOLD) {
		/* Actually an USB host */
		gpio_set_level(GPIO_ID_MUX, 0);
		msleep(DELAY_ID_MUX_MS);
		tsu6721_enable_interrupts();
		return device_type;
	} else {
		/* Not USB host but video */
		device_type = (device_type & ~TSU6721_TYPE_USB_HOST) |
			      TSU6721_TYPE_JIG_UART_ON;
		return device_type;
	}
}

void board_pwm_duty_cycle(int percent)
{
	if (current_ilim_config != ILIM_CONFIG_PWM)
		board_ilim_config(ILIM_CONFIG_PWM);
	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	STM32_TIM_CCR1(3) = (percent * STM32_TIM_ARR(3)) / 100;
	current_pwm_duty = percent;
}

void board_pwm_init_limit(void)
{
	int dummy;

	/*
	 * Shut off power input if battery is good. Otherwise, leave
	 * 500mA to sustain the system.
	 */
	if (battery_current(&dummy))
		board_pwm_duty_cycle(I_LIMIT_500MA);
	else
		board_ilim_config(ILIM_CONFIG_MANUAL_ON);
}

static int board_pwm_check_lower_bound(void)
{
	if (current_limit_mode == LIMIT_AGGRESSIVE)
		return (current_pwm_duty > nominal_pwm_duty -
					   PWM_CTRL_OC_MARGIN &&
			current_pwm_duty > over_current_pwm_duty &&
			current_pwm_duty > 0);
	else
		return (current_pwm_duty > nominal_pwm_duty &&
			current_pwm_duty > 0);
}

static int board_pwm_check_vbus_low(int vbus, int battery_current)
{
	if (battery_current >= 0)
		return vbus < PWM_CTRL_VBUS_LOW && current_pwm_duty < 100;
	else
		return vbus < PWM_CTRL_VBUS_HARD_LOW && current_pwm_duty < 100;
}

static void board_pwm_tweak(void)
{
	int vbus, current;

	if (current_ilim_config != ILIM_CONFIG_PWM)
		return;

	vbus = adc_read_channel(ADC_CH_USB_VBUS_SNS);
	if (battery_current(&current))
		return;
	/*
	 * If VBUS voltage is too low:
	 *   - If battery is discharging, throttling more is going to draw
	 *     more current from the battery, so do nothing unless VBUS is
	 *     about to be lower than AC good threshold.
	 *   - Otherwise, throttle input current to raise VBUS voltage.
	 * If VBUS voltage is high enough, allow more current until we hit
	 * current limit target.
	 */
	if (board_pwm_check_vbus_low(vbus, current)) {
		board_pwm_duty_cycle(current_pwm_duty + PWM_CTRL_STEP_UP);
		CPRINTF("[%T PWM duty up %d%%]\n", current_pwm_duty);
	} else if (vbus > PWM_CTRL_VBUS_HIGH && board_pwm_check_lower_bound()) {
		board_pwm_duty_cycle(current_pwm_duty - PWM_CTRL_STEP_DOWN);
		CPRINTF("[%T PWM duty down %d%%]\n", current_pwm_duty);
	}
}
DECLARE_HOOK(HOOK_SECOND, board_pwm_tweak, HOOK_PRIO_DEFAULT);

void board_pwm_nominal_duty_cycle(int percent)
{
	int dummy;

	if (battery_current(&dummy))
		board_pwm_duty_cycle(percent);
	else if (percent + PWM_CTRL_BEGIN_OFFSET > PWM_CTRL_MAX_DUTY)
		board_pwm_duty_cycle(PWM_CTRL_MAX_DUTY);
	else
		board_pwm_duty_cycle(percent + PWM_CTRL_BEGIN_OFFSET);
	nominal_pwm_duty = percent;
}

void usb_charge_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_PMU_TPS65090_CHARGER);
}

static int usb_has_power_input(int dev_type)
{
	if (dev_type & TSU6721_TYPE_JIG_UART_ON)
		return 1;
	return (dev_type & TSU6721_TYPE_VBUS_DEBOUNCED) &&
	       !(dev_type & POWERED_5000_DEVICE_TYPE);
}

static void usb_device_change(int dev_type)
{
	int need_boost;
	int retry_limit = 3;

	if (current_dev_type == dev_type)
		return;

	over_current_pwm_duty = 0;

	/*
	 * Video output is recognized incorrectly as USB host. When we see
	 * USB host, probe for video output.
	 */
	if (dev_type & TSU6721_TYPE_USB_HOST)
		dev_type = board_probe_video(dev_type);

	/*
	 * When a power source is removed, record time, power source type,
	 * and PWM duty cycle. Then when we see a power source, compare type
	 * and calculate time difference to determine if we have just
	 * encountered an over current event.
	 */
	if ((current_dev_type & TSU6721_TYPE_VBUS_DEBOUNCED) &&
	    (dev_type == TSU6721_TYPE_NONE)) {
		int idx = !(current_dev_type == TSU6721_TYPE_VBUS_DEBOUNCED);
		power_removed_time[idx] = get_time();
		power_removed_type[idx] = current_dev_type;
		/*
		 * Considering user may plug/unplug the charger too fast, we
		 * don't limit current to lower than nominal current limit.
		 */
		power_removed_pwm_duty[idx] = MIN(current_pwm_duty,
						  nominal_pwm_duty);
	} else if (dev_type & TSU6721_TYPE_VBUS_DEBOUNCED) {
		int idx = !(dev_type == TSU6721_TYPE_VBUS_DEBOUNCED);
		timestamp_t now = get_time();
		now.val -= power_removed_time[idx].val;
		if (power_removed_type[idx] == dev_type &&
		    now.val < PWM_CTRL_OC_DETECT_TIME) {
			over_current_pwm_duty = power_removed_pwm_duty[idx] +
						PWM_CTRL_OC_BACK_OFF;
		}
	}

	/*
	 * Supply 5V VBUS if needed. If we toggle power output, wait for a
	 * moment, and then update device type. To avoid race condition, check
	 * if power requirement changes during this time.
	 */
	do {
		if (retry_limit-- <= 0)
			break;

		need_boost = !(dev_type & POWERED_5000_DEVICE_TYPE);
		if (need_boost != gpio_get_level(GPIO_BOOST_EN)) {
			gpio_set_level(GPIO_BOOST_EN, need_boost);
			msleep(DELAY_POWER_MS);
			dev_type = tsu6721_get_device_type();
		}
	} while (need_boost == !!(dev_type & POWERED_5000_DEVICE_TYPE));

	/* Supply 3.3V VBUS if needed. */
	if (dev_type & POWERED_3300_DEVICE_TYPE) {
		pmu_enable_fet(FET_VIDEO, 1, NULL);
	}

	if (usb_has_power_input(dev_type)) {
		/* Limit USB port current. 500mA for not listed types. */
		int current_limit = I_LIMIT_500MA;
		if (dev_type & TSU6721_TYPE_CHG12)
			current_limit = I_LIMIT_3000MA;
		else if (dev_type & TSU6721_TYPE_APPLE_CHG) {
			current_limit = board_apple_charger_current();
		} else if ((dev_type & TSU6721_TYPE_CDP) ||
			   (dev_type & TSU6721_TYPE_DCP))
			current_limit = I_LIMIT_1500MA;

		board_pwm_nominal_duty_cycle(current_limit);

		/* Turns on battery LED */
		lp5562_poweron();
	} else {
		board_ilim_config(ILIM_CONFIG_MANUAL_ON);
		lp5562_poweroff();
	}

	/* Log to console */
	CPRINTF("[%T USB Attached: ");
	if (dev_type == TSU6721_TYPE_NONE)
		CPRINTF("Nothing]\n");
	else if (dev_type & TSU6721_TYPE_OTG)
		CPRINTF("OTG]\n");
	else if (dev_type & TSU6721_TYPE_USB_HOST)
		CPRINTF("USB Host]\n");
	else if (dev_type & TSU6721_TYPE_CHG12)
		CPRINTF("Type 1/2 Charger]\n");
	else if (dev_type & TSU6721_TYPE_NON_STD_CHG)
		CPRINTF("Non standard charger]\n");
	else if (dev_type & TSU6721_TYPE_DCP)
		CPRINTF("DCP]\n");
	else if (dev_type & TSU6721_TYPE_CDP)
		CPRINTF("CDP]\n");
	else if (dev_type & TSU6721_TYPE_U200_CHG)
		CPRINTF("U200]\n");
	else if (dev_type & TSU6721_TYPE_APPLE_CHG)
		CPRINTF("Apple charger]\n");
	else if (dev_type & TSU6721_TYPE_JIG_UART_ON)
		CPRINTF("JIG UART ON]\n");
	else if (dev_type & TSU6721_TYPE_VBUS_DEBOUNCED)
		CPRINTF("Unknown with power]\n");
	else
		CPRINTF("Unknown]\n");

	keyboard_send_battery_key();

	current_dev_type = dev_type;
}

/*
 * TODO(victoryang): Get rid of polling loop when ADC watchdog is ready.
 *                   See crosbug.com/p/18171
 */
static void board_usb_monitor_detach(void)
{
	if (!(current_dev_type & TSU6721_TYPE_JIG_UART_ON))
		return;

	if (adc_read_channel(ADC_CH_USB_DP_SNS) > VIDEO_ID_THRESHOLD) {
		pmu_enable_fet(FET_VIDEO, 0, NULL);
		gpio_set_level(GPIO_ID_MUX, 0);
		msleep(DELAY_ID_MUX_MS);
		tsu6721_enable_interrupts();
		usb_device_change(TSU6721_TYPE_NONE);
	}
}
DECLARE_HOOK(HOOK_SECOND, board_usb_monitor_detach, HOOK_PRIO_DEFAULT);

void board_usb_charge_update(int force_update)
{
	int int_val = tsu6721_get_interrupts();

	if (int_val & TSU6721_INT_DETACH)
		usb_device_change(TSU6721_TYPE_NONE);
	else if (int_val || force_update)
		usb_device_change(tsu6721_get_device_type());
}

int board_get_usb_dev_type(void)
{
	return current_dev_type;
}

int board_get_usb_current_limit(void)
{
	/* Approximate value by PWM duty cycle */
	return 3012 - 29 * current_pwm_duty;
}

/*
 * Console commands for debugging.
 * TODO(victoryang): Remove after charging control is done.
 */
static int command_ilim(int argc, char **argv)
{
	char *e;
	int percent;

	if (argc >= 2) {
		if (strcasecmp(argv[1], "on") == 0)
			board_ilim_config(ILIM_CONFIG_MANUAL_ON);
		else if (strcasecmp(argv[1], "off") == 0)
			board_ilim_config(ILIM_CONFIG_MANUAL_OFF);
		else {
			percent = strtoi(argv[1], &e, 0);
			if (*e)
				return EC_ERROR_PARAM1;
			board_pwm_duty_cycle(percent);
		}
	}

	if (current_ilim_config == ILIM_CONFIG_MANUAL_ON)
		ccprintf("ILIM is GPIO high\n");
	else if (current_ilim_config == ILIM_CONFIG_MANUAL_OFF)
		ccprintf("ILIM is GPIO low\n");
	else
		ccprintf("ILIM is PWM duty cycle %d%%\n", STM32_TIM_CCR1(3));

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ilim, command_ilim,
		"[percent | on | off]",
		"Set or show ILIM duty cycle/GPIO value",
		NULL);

static int command_batdebug(int argc, char **argv)
{
	int val;
	ccprintf("VBUS = %d mV\n", adc_read_channel(ADC_CH_USB_VBUS_SNS));
	ccprintf("VAC = %d mV\n", pmu_adc_read(ADC_VAC, ADC_FLAG_KEEP_ON)
				  * 17000 / 1024);
	ccprintf("IAC = %d mA\n", pmu_adc_read(ADC_IAC, ADC_FLAG_KEEP_ON)
				  * 20 * 33 / 1024);
	ccprintf("VBAT = %d mV\n", pmu_adc_read(ADC_VBAT, ADC_FLAG_KEEP_ON)
				  * 17000 / 1024);
	ccprintf("IBAT = %d mA\n", pmu_adc_read(ADC_IBAT, 0)
				  * 50 * 40 / 1024);
	ccprintf("PWM = %d%%\n", STM32_TIM_CCR1(3));
	battery_current(&val);
	ccprintf("Battery Current = %d mA\n", val);
	battery_voltage(&val);
	ccprintf("Battery Voltage= %d mV\n", val);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(batdebug, command_batdebug,
			NULL, NULL, NULL);

static int command_current_limit_mode(int argc, char **argv)
{
	if (1 == argc) {
		if (current_limit_mode == LIMIT_NORMAL)
			ccprintf("Normal mode\n");
		else
			ccprintf("Aggressive mode\n");
		return EC_SUCCESS;
	} else if (2 == argc) {
		if (!strcasecmp(argv[1], "normal"))
			current_limit_mode = LIMIT_NORMAL;
		else if (!strcasecmp(argv[1], "aggressive"))
			current_limit_mode = LIMIT_AGGRESSIVE;
		else
			return EC_ERROR_INVAL;
		return EC_SUCCESS;
	}
	return EC_ERROR_INVAL;
}
DECLARE_CONSOLE_COMMAND(limitmode, command_current_limit_mode,
			"[normal | aggressive]",
			"Set current limit mode",
			NULL);
