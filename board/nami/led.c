/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Nami and its variants
 *
 * This is an event-driven LED control library. It does not use tasks or
 * periodical hooks (HOOK_TICK, HOOK_SECOND), thus, it's more resource
 * efficient.
 *
 * The library defines LED states and assigns an LED behavior to each state.
 * The state space consists of tuple of (charge state, power state).
 * In each LED state, a color and a pulse interval can be defined.
 *
 * Charging states are queried each time there is a state transition, thus, not
 * stored. We hook power state transitions (e.g. s0->s3) and save the
 * destination states (e.g. s3) in power_state.
 *
 * When system is suspending and AC is unplugged, there will be race condition
 * between a power state hook and a charge state hook but whichever is called
 * first or last the result will be the same.
 *
 * Currently, it supports two LEDs, called 'battery LED' and 'power LED'.
 * It assumes the battery LED is connected to a PWM pin and the power LED is
 * connected to a regular GPIO pin.
 */

#include "charge_state.h"
#include "chipset.h"
#include "console.h"
#include "cros_board_info.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "power.h"
#include "pwm.h"
#include "timer.h"
#include "util.h"

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED,
					     EC_LED_ID_POWER_LED };
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_GREEN,
	LED_AMBER,
	LED_WHITE,
	LED_WARM_WHITE,
	LED_FACTORY,
	/* Number of colors, not a color itself */
	LED_COLOR_COUNT
};

/* Charging states of LED's interests */
enum led_charge_state {
	LED_STATE_DISCHARGE = 0,
	LED_STATE_CHARGE,
	LED_STATE_FULL,
	LED_CHARGE_STATE_COUNT,
};

/* Power states of LED's interests */
enum led_power_state {
	LED_STATE_S0 = 0,
	LED_STATE_S3,
	LED_STATE_S5,
	LED_POWER_STATE_COUNT,
};

/* Defines a LED pattern for a single state */
struct led_pattern {
	uint8_t color;
	/* Bit 0-5: Interval in 100 msec. 0=solid. Max is 3.2 sec.
	 * Bit 6: 1=alternate (on-off-off-off), 0=regular (on-off-on-off)
	 * Bit 7: 1=pulse, 0=blink */
	uint8_t pulse;
};

#define PULSE_NO 0
#define PULSE(interval) (BIT(7) | (interval))
#define BLINK(interval) (interval)
#define ALTERNATE(interval) (BIT(6) | (interval))
#define IS_PULSING(pulse) ((pulse) & 0x80)
#define IS_ALTERNATE(pulse) ((pulse) & 0x40)
#define PULSE_INTERVAL(pulse) (((pulse) & 0x3f) * 100 * MSEC)

/* 40 msec for nice and smooth transition. */
#define LED_PULSE_TICK_US (40 * MSEC)

typedef struct led_pattern led_patterns[LED_CHARGE_STATE_COUNT]
				       [LED_POWER_STATE_COUNT];

/*
 * Nami/Vayne - One dual color LED:
 * Charging               Amber on (S0/S3/S5)
 * Charging (full)        White on (S0/S3/S5)
 * Discharge in S0        White on
 * Discharge in S3/S0ix   Pulsing (rising for 2 sec , falling for 2 sec)
 * Discharge in S5        Off
 * Battery Error          Amber on 1sec off 1sec
 * Factory mode	          White on 2sec, Amber on 2sec
 */
const static led_patterns battery_pattern_0 = {
	/* discharging: s0, s3, s5 */
	{ { LED_WHITE, PULSE_NO },
	  { LED_WHITE, PULSE(10) },
	  { LED_OFF, PULSE_NO } },
	/* charging: s0, s3, s5 */
	{ { LED_AMBER, PULSE_NO },
	  { LED_AMBER, PULSE_NO },
	  { LED_AMBER, PULSE_NO } },
	/* full: s0, s3, s5 */
	{ { LED_WHITE, PULSE_NO },
	  { LED_WHITE, PULSE_NO },
	  { LED_WHITE, PULSE_NO } },
};

/*
 * Sona - Battery LED (dual color)
 */
const static led_patterns battery_pattern_1 = {
	/* discharging: s0, s3, s5 */
	{ { LED_OFF, PULSE_NO }, { LED_OFF, PULSE_NO }, { LED_OFF, PULSE_NO } },
	/* charging: s0, s3, s5 */
	{ { LED_AMBER, PULSE_NO },
	  { LED_AMBER, PULSE_NO },
	  { LED_AMBER, PULSE_NO } },
	/* full: s0, s3, s5 */
	{ { LED_WHITE, PULSE_NO },
	  { LED_WHITE, PULSE_NO },
	  { LED_WHITE, PULSE_NO } },
};

/*
 * Pantheon - AC In/Battery LED(dual color):
 * Connected to AC power / Charged (100%)        White (solid on)
 * Connected to AC power / Charging(1% -99%)     Amber (solid on)
 * Not connected to AC power                     Off
 */
const static led_patterns battery_pattern_2 = {
	/* discharging: s0, s3, s5 */
	{ { LED_OFF, PULSE_NO }, { LED_OFF, PULSE_NO }, { LED_OFF, PULSE_NO } },
	/* charging: s0, s3, s5 */
	{ { LED_AMBER, PULSE_NO },
	  { LED_AMBER, PULSE_NO },
	  { LED_AMBER, PULSE_NO } },
	/* full: s0, s3, s5 */
	{ { LED_WHITE, PULSE_NO },
	  { LED_WHITE, PULSE_NO },
	  { LED_WHITE, PULSE_NO } },
};

/*
 * Sona - Power LED (single color)
 */
const static led_patterns power_pattern_1 = {
	/* discharging: s0, s3, s5 */
	{ { LED_WHITE, PULSE_NO },
	  { LED_WHITE, BLINK(10) },
	  { LED_OFF, PULSE_NO } },
	/* charging: s0, s3, s5 */
	{ { LED_WHITE, PULSE_NO },
	  { LED_WHITE, BLINK(10) },
	  { LED_OFF, PULSE_NO } },
	/* full: s0, s3, s5 */
	{ { LED_WHITE, PULSE_NO },
	  { LED_WHITE, BLINK(10) },
	  { LED_OFF, PULSE_NO } },
};

/*
 * Pantheon - Power LED
 * S0:        White on
 * S3/S0ix:   White 1 second on, 3 second off
 * S5:        Off
 */
const static led_patterns power_pattern_2 = {
	/* discharging: s0, s3, s5 */
	{ { LED_WHITE, 0 },
	  { LED_WHITE, ALTERNATE(BLINK(10)) },
	  { LED_OFF, 0 } },
	/* charging: s0, s3, s5 */
	{ { LED_WHITE, 0 },
	  { LED_WHITE, ALTERNATE(BLINK(10)) },
	  { LED_OFF, 0 } },
	/* full: s0, s3, s5 */
	{ { LED_WHITE, 0 },
	  { LED_WHITE, ALTERNATE(BLINK(10)) },
	  { LED_OFF, 0 } },
};

/*
 * Akali - battery LED
 * Charge:           Amber on (s0/s3/s5)
 * Full:             Blue on (s0/s3/s5)
 * Discharge in S0:  Blue on
 * Discharge in S3:  Amber on 1 sec off 3 sec
 * Discharge in S5:  Off
 * Battery Error:    Amber on 1sec off 1sec
 * Factory mode :    Blue on 2sec, Amber on 2sec
 */
const static led_patterns battery_pattern_3 = {
	/* discharging: s0, s3, s5 */
	{ { LED_WHITE, 0 },
	  { LED_AMBER, ALTERNATE(BLINK(10)) },
	  { LED_OFF, 0 } },
	/* charging: s0, s3, s5 */
	{ { LED_AMBER, PULSE_NO },
	  { LED_AMBER, PULSE_NO },
	  { LED_AMBER, PULSE_NO } },
	/* full: s0, s3, s5 */
	{ { LED_WHITE, PULSE_NO },
	  { LED_WHITE, PULSE_NO },
	  { LED_WHITE, PULSE_NO } },
};

const static led_patterns battery_pattern_4 = {
	/* discharging: s0, s3, s5 */
	{ { LED_WHITE, PULSE_NO },
	  { LED_WHITE, BLINK(10) },
	  { LED_OFF, PULSE_NO } },
	/* charging: s0, s3, s5 */
	{ { LED_AMBER, PULSE_NO },
	  { LED_AMBER, PULSE_NO },
	  { LED_AMBER, PULSE_NO } },
	/* full: s0, s3, s5 */
	{ { LED_WHITE, PULSE_NO },
	  { LED_WHITE, PULSE_NO },
	  { LED_WHITE, PULSE_NO } },
};

/* Patterns for battery LED and power LED. Initialized at run-time. */
static led_patterns const *patterns[2];
/* Pattern for battery error. Only blinking battery LED is supported. */
static struct led_pattern battery_error = { LED_AMBER, BLINK(10) };
/* Pattern for low state of charge. Only battery LED is supported. */
static struct led_pattern low_battery = { LED_WHITE, BLINK(10) };
/* Pattern for factory mode. Blinking 2-color battery LED. */
static struct led_pattern battery_factory = { LED_FACTORY, BLINK(20) };
static int low_battery_soc;
static void led_charge_hook(void);
static enum led_power_state power_state;

static void led_init(void)
{
	switch (oem) {
	case PROJECT_NAMI:
	case PROJECT_VAYNE:
		patterns[0] = &battery_pattern_0;
		break;
	case PROJECT_SONA:
		if (model == MODEL_SYNDRA) {
			/* Syndra doesn't have power LED */
			patterns[0] = &battery_pattern_4;
		} else {
			patterns[0] = &battery_pattern_1;
			patterns[1] = &power_pattern_1;
		}
		battery_error.pulse = BLINK(5);
		low_battery_soc = 100; /* 10.0% */
		break;
	case PROJECT_PANTHEON:
		patterns[0] = &battery_pattern_2;
		patterns[1] = &power_pattern_2;
		battery_error.color = LED_OFF;
		battery_error.pulse = 0;
		break;
	case PROJECT_AKALI:
		patterns[0] = &battery_pattern_3;
		break;
	default:
		break;
	}

	pwm_enable(PWM_CH_LED1, 1);
	pwm_enable(PWM_CH_LED2, 1);

	/* After sysjump, power_state is cleared. Thus, we need to actively
	 * retrieve it. */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		power_state = LED_STATE_S5;
	else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		power_state = LED_STATE_S3;
	else
		power_state = LED_STATE_S0;
}
DECLARE_HOOK(HOOK_INIT, led_init, HOOK_PRIO_DEFAULT);

static int set_color_battery(enum led_color color, int duty)
{
	int led1 = 0;
	int led2 = 0;

	if (duty < 0 || 100 < duty)
		return EC_ERROR_UNKNOWN;

	switch (color) {
	case LED_OFF:
		break;
	case LED_AMBER:
		led2 = 1;
		break;
	case LED_WHITE:
		led1 = 1;
		break;
	case LED_WARM_WHITE:
		led1 = 1;
		led2 = 1;
		break;
	case LED_FACTORY:
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	if (color != LED_FACTORY) {
		pwm_set_duty(PWM_CH_LED1, led1 ? duty : 0);
		pwm_set_duty(PWM_CH_LED2, led2 ? duty : 0);
	} else {
		pwm_set_duty(PWM_CH_LED1, duty ? 100 : 0);
		pwm_set_duty(PWM_CH_LED2, duty ? 0 : 100);
	}

	return EC_SUCCESS;
}

static int set_color_power(enum led_color color, int duty)
{
	if (color == LED_OFF)
		duty = 0;
	gpio_set_level(GPIO_LED1, !duty /* Reversed logic */);
	return EC_SUCCESS;
}

static int set_color(enum ec_led_id id, enum led_color color, int duty)
{
	switch (id) {
	case EC_LED_ID_BATTERY_LED:
		return set_color_battery(color, duty);
	case EC_LED_ID_POWER_LED:
		return set_color_power(color, duty);
	default:
		return EC_ERROR_UNKNOWN;
	}
}

static struct {
	uint32_t interval;
	int duty_inc;
	enum led_color color;
	int duty;
	int alternate;
	uint8_t pulse;
} tick[2];

static void tick_battery(void);
DECLARE_DEFERRED(tick_battery);
static void tick_power(void);
DECLARE_DEFERRED(tick_power);
static void cancel_tick(enum ec_led_id id)
{
	if (id == EC_LED_ID_BATTERY_LED)
		hook_call_deferred(&tick_battery_data, -1);
	else
		hook_call_deferred(&tick_power_data, -1);
}

static int config_tick(enum ec_led_id id, const struct led_pattern *pattern)
{
	static const struct led_pattern *patterns[2];
	uint32_t stride;

	if (pattern == patterns[id])
		/* This pattern was already set */
		return -1;

	patterns[id] = pattern;

	if (!pattern->pulse) {
		/* This is a steady pattern. cancel the tick */
		cancel_tick(id);
		set_color(id, pattern->color, 100);
		return 1;
	}

	stride = PULSE_INTERVAL(pattern->pulse);
	if (IS_PULSING(pattern->pulse)) {
		tick[id].interval = LED_PULSE_TICK_US;
		tick[id].duty_inc = 100 / (stride / LED_PULSE_TICK_US);
	} else {
		tick[id].interval = stride;
		tick[id].duty_inc = 100;
	}
	tick[id].color = pattern->color;
	tick[id].duty = 0;
	tick[id].alternate = 0;
	tick[id].pulse = pattern->pulse;

	return 0;
}

/*
 * When pulsing, brightness is incremented by <duty_inc> every <interval> usec
 * from 0 to 100%. Then it's decremented from 100% to 0.
 */
static void pulse_led(enum ec_led_id id)
{
	if (tick[id].duty + tick[id].duty_inc > 100) {
		tick[id].duty_inc = tick[id].duty_inc * -1;
	} else if (tick[id].duty + tick[id].duty_inc < 0) {
		if (IS_ALTERNATE(tick[id].pulse)) {
			/* Falling phase landing. Flip the alternate flag. */
			tick[id].alternate = !tick[id].alternate;
			if (tick[id].alternate)
				return;
		}
		tick[id].duty_inc = tick[id].duty_inc * -1;
	}
	tick[id].duty += tick[id].duty_inc;
	set_color(id, tick[id].color, tick[id].duty);
}

static uint32_t tick_led(enum ec_led_id id)
{
	uint32_t elapsed;
	uint32_t start = get_time().le.lo;
	uint32_t next;

	if (led_auto_control_is_enabled(id))
		pulse_led(id);
	if (tick[id].alternate)
		/* Skip 2 phases (rising & falling) */
		next = PULSE_INTERVAL(tick[id].pulse) * 2;
	else
		next = tick[id].interval;
	elapsed = get_time().le.lo - start;
	return next > elapsed ? next - elapsed : 0;
}

static void tick_battery(void)
{
	hook_call_deferred(&tick_battery_data, tick_led(EC_LED_ID_BATTERY_LED));
}

static void tick_power(void)
{
	hook_call_deferred(&tick_power_data, tick_led(EC_LED_ID_POWER_LED));
}

static void start_tick(enum ec_led_id id, const struct led_pattern *pattern)
{
	if (config_tick(id, pattern))
		/*
		 * If this pattern is already active, ticking must have started
		 * already. So, we don't re-start ticking to prevent LED from
		 * blinking at every SOC change.
		 *
		 * If this pattern is static, we skip ticking as well.
		 */
		return;

	if (id == EC_LED_ID_BATTERY_LED)
		tick_battery();
	else
		tick_power();
}

static void led_alert(int enable)
{
	if (enable)
		start_tick(EC_LED_ID_BATTERY_LED, &battery_error);
	else
		led_charge_hook();
}

static void led_factory(int enable)
{
	if (enable)
		start_tick(EC_LED_ID_BATTERY_LED, &battery_factory);
	else
		led_charge_hook();
}

void config_led(enum ec_led_id id, enum led_charge_state charge)
{
	const led_patterns *pattern;

	pattern = patterns[id];
	if (!pattern)
		return; /* This LED isn't present */

	start_tick(id, &(*pattern)[charge][power_state]);
}

void config_leds(enum led_charge_state charge)
{
	config_led(EC_LED_ID_BATTERY_LED, charge);
	config_led(EC_LED_ID_POWER_LED, charge);
}

static void call_handler(void)
{
	int soc;
	enum led_pwr_state cs;

	if (!led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		return;

	cs = led_pwr_get_state();
	soc = charge_get_display_charge();
	if (soc < 0)
		cs = LED_PWRS_ERROR;

	switch (cs) {
	case LED_PWRS_DISCHARGE:
	case LED_PWRS_DISCHARGE_FULL:
		if (soc < low_battery_soc)
			start_tick(EC_LED_ID_BATTERY_LED, &low_battery);
		else
			config_led(EC_LED_ID_BATTERY_LED, LED_STATE_DISCHARGE);
		config_led(EC_LED_ID_POWER_LED, LED_STATE_DISCHARGE);
		break;
	case LED_PWRS_CHARGE_NEAR_FULL:
	case LED_PWRS_CHARGE:
		if (soc >= 1000)
			config_leds(LED_STATE_FULL);
		else
			config_leds(LED_STATE_CHARGE);
		break;
	case LED_PWRS_ERROR:
		/* It doesn't matter what 'charge' state we pass because power
		 * LED (if it exists) is orthogonal to battery state. */
		config_led(EC_LED_ID_POWER_LED, 0);
		led_alert(1);
		break;
	case LED_PWRS_IDLE:
		/* External power connected in IDLE. */
		break;
	case LED_PWRS_FORCED_IDLE:
		/* This is used to show factory mode when
		 * 'ectool chargecontrol idle' is run during factory process.
		 */
		led_factory(1);
		break;
	default:;
	}
}

/* LED state transition handlers */
static void s0(void)
{
	power_state = LED_STATE_S0;
	call_handler();
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, s0, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, s0, HOOK_PRIO_DEFAULT);

static void s3(void)
{
	power_state = LED_STATE_S3;
	call_handler();
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, s3, HOOK_PRIO_DEFAULT);

static void s5(void)
{
	power_state = LED_STATE_S5;
	call_handler();
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, s5, HOOK_PRIO_DEFAULT);

static void led_charge_hook(void)
{
	call_handler();
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, led_charge_hook, HOOK_PRIO_DEFAULT);

static void print_config(enum ec_led_id id)
{
	ccprintf("ID:%d\n", id);
	ccprintf("  Color:%d\n", tick[id].color);
	ccprintf("  Duty:%d\n", tick[id].duty);
	ccprintf("  Duty Increment:%d\n", tick[id].duty_inc);
	ccprintf("  Interval:%d\n", tick[id].interval);
}

static int command_led(int argc, const char **argv)
{
	enum ec_led_id id = EC_LED_ID_BATTERY_LED;
	static int alert = 0;
	static int factory;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "debug")) {
		led_auto_control(id, !led_auto_control_is_enabled(id));
		ccprintf("o%s\n", led_auto_control_is_enabled(id) ? "ff" : "n");
	} else if (!strcasecmp(argv[1], "off")) {
		set_color(id, LED_OFF, 0);
	} else if (!strcasecmp(argv[1], "red")) {
		set_color(id, LED_RED, 100);
	} else if (!strcasecmp(argv[1], "white")) {
		set_color(id, LED_WHITE, 100);
	} else if (!strcasecmp(argv[1], "amber")) {
		set_color(id, LED_AMBER, 100);
	} else if (!strcasecmp(argv[1], "alert")) {
		alert = !alert;
		led_alert(alert);
	} else if (!strcasecmp(argv[1], "s0")) {
		s0();
	} else if (!strcasecmp(argv[1], "s3")) {
		s3();
	} else if (!strcasecmp(argv[1], "s5")) {
		s5();
	} else if (!strcasecmp(argv[1], "conf")) {
		print_config(id);
	} else if (!strcasecmp(argv[1], "factory")) {
		factory = !factory;
		led_factory(factory);
	} else {
		return EC_ERROR_PARAM1;
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(
	led, command_led,
	"[debug|red|green|amber|off|alert|s0|s3|s5|conf|factory]",
	"Turn on/off LED.");

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	/*
	 * We return amber=100, white=100 regardless of OEM ID or led_id. This
	 * function is for ectool led command, which is used to test LED
	 * functionality.
	 */
	brightness_range[EC_LED_COLOR_AMBER] = 100;
	brightness_range[EC_LED_COLOR_WHITE] = 100;
}

int led_set_brightness(enum ec_led_id id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_AMBER])
		return set_color(id, LED_AMBER, brightness[EC_LED_COLOR_AMBER]);
	else if (brightness[EC_LED_COLOR_WHITE])
		return set_color(id, LED_WHITE, brightness[EC_LED_COLOR_WHITE]);
	else
		return set_color(id, LED_OFF, 0);
}
