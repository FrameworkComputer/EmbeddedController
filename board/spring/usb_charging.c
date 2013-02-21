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
#include "pmu_tpschrome.h"
#include "registers.h"
#include "smart_battery.h"
#include "stm32_adc.h"
#include "task.h"
#include "timer.h"
#include "tsu6721.h"
#include "util.h"

#define PWM_FREQUENCY 10000 /* Hz */

/* Console output macros */
#define CPUTS(outstr) cputs(CC_USBCHARGE, outstr)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* Devices that need VBUS power */
#define POWERED_DEVICE_TYPE (TSU6721_TYPE_OTG | \
			     TSU6721_TYPE_JIG_UART_ON)

/* PWM controlled current limit */
#define I_LIMIT_500MA   85
#define I_LIMIT_1000MA  75
#define I_LIMIT_1500MA  50
#define I_LIMIT_2000MA  35
#define I_LIMIT_2400MA  25
#define I_LIMIT_3000MA  0

/* PWM control loop parameters */
#define PWM_CTRL_BEGIN_OFFSET	30
#define PWM_CTRL_STEP_DOWN	1
#define PWM_CTRL_STEP_UP	5
#define PWM_CTRL_VBUS_LOW	4500
#define PWM_CTRL_VBUS_HIGH	4700 /* Must be higher than 4.5V */

static int current_dev_type = TSU6721_TYPE_NONE;
static int nominal_pwm_duty;
static int current_pwm_duty;

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
	 * CPU_CLOCK / PSC determines how fast the counter operates.
	 * ARR determines the wave period, CCRn determines duty cycle.
	 * Thus, frequency = CPU_CLOCK / PSC / ARR.
	 *
	 * Assuming 16MHz clock and ARR=100, PSC needed to achieve PWM_FREQUENCY
	 * is: PSC = CPU_CLOCK / PWM_FREQUENCY / ARR
	 */
	STM32_TIM_PSC(3) = CPU_CLOCK / PWM_FREQUENCY / 100; /* pre-scaler */
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
	msleep(20);
	adc_read_all_channels(data);
	vp = data[ADC_CH_USB_DP_SNS];
	vn = data[ADC_CH_USB_DN_SNS];
	tsu6721_mux(TSU6721_MUX_AUTO);
	tsu6721_enable_interrupts();
	if (vp > 1200)
		type |= 0x2;
	if (vn > 1200)
		type |= 0x1;

	return apple_charger_type[type];
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
	 *     more current from the battery, so do nothing in this case.
	 *   - Otherwise, throttle input current to raise VBUS voltage.
	 * If VBUS voltage is high enough, allow more current until we hit
	 * current limit target.
	 */
	if (vbus < PWM_CTRL_VBUS_LOW &&
	    current_pwm_duty < 100 &&
	    current >= 0) {
		board_pwm_duty_cycle(current_pwm_duty + PWM_CTRL_STEP_UP);
		CPRINTF("[%T PWM duty up %d%%]\n", current_pwm_duty);
	} else if (vbus > PWM_CTRL_VBUS_HIGH &&
		   current_pwm_duty > nominal_pwm_duty) {
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
	else
		board_pwm_duty_cycle(percent + PWM_CTRL_BEGIN_OFFSET);
	nominal_pwm_duty = percent;
}

void usb_charge_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_PMU_TPS65090_CHARGER);
}

static void usb_device_change(int dev_type)
{
	if (current_dev_type == dev_type)
		return;
	current_dev_type = dev_type;

	/* Supply VBUS if needed */
	if (dev_type & POWERED_DEVICE_TYPE)
		gpio_set_level(GPIO_BOOST_EN, 0);
	else
		gpio_set_level(GPIO_BOOST_EN, 1);

	if ((dev_type & TSU6721_TYPE_VBUS_DEBOUNCED) &&
	    !(dev_type & POWERED_DEVICE_TYPE)) {
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
}

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
	ccprintf("VAC = %d mV\n", pmu_adc_read(ADC_VAC) * 17000 / 1024);
	ccprintf("IAC = %d mA\n", pmu_adc_read(ADC_IAC) * 20 * 33 / 1024);
	ccprintf("VBAT = %d mV\n", pmu_adc_read(ADC_VBAT) * 17000 / 1024);
	ccprintf("IBAT = %d mA\n", pmu_adc_read(ADC_IBAT) * 50 * 40 / 1024);
	ccprintf("PWM = %d%%\n", STM32_TIM_CCR1(3));
	battery_current(&val);
	ccprintf("Battery Current = %d mA\n", val);
	battery_voltage(&val);
	ccprintf("Battery Voltage= %d mV\n", val);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(batdebug, command_batdebug,
			NULL, NULL, NULL);
