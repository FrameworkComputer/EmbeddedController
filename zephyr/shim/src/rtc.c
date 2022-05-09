/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/zephyr.h>

#include "console.h"
#include "drivers/cros_rtc.h"
#include "hooks.h"
#include "host_command.h"
#include "util.h"

LOG_MODULE_REGISTER(shim_cros_rtc, LOG_LEVEL_ERR);

#define CROS_RTC_NODE DT_CHOSEN(cros_rtc)
static const struct device *cros_rtc_dev;

#ifdef CONFIG_HOSTCMD_EVENTS
static void set_rtc_host_event(void)
{
	host_set_single_event(EC_HOST_EVENT_RTC);
}
DECLARE_DEFERRED(set_rtc_host_event);
#endif

void rtc_callback(const struct device *dev)
{
	ARG_UNUSED(dev);

	if (IS_ENABLED(CONFIG_HOSTCMD_EVENTS)) {
		hook_call_deferred(&set_rtc_host_event_data, 0);
	}
}

/** Initialize the rtc. */
static int system_init_rtc(const struct device *unused)
{
	ARG_UNUSED(unused);

	cros_rtc_dev = DEVICE_DT_GET(CROS_RTC_NODE);
	if (!cros_rtc_dev) {
		LOG_ERR("Error: device %s is not ready", cros_rtc_dev->name);
		return -ENODEV;
	}

	/* set the RTC callback */
	cros_rtc_configure(cros_rtc_dev, rtc_callback);

	return 0;
}
SYS_INIT(system_init_rtc, APPLICATION, 1);

uint32_t system_get_rtc_sec(void)
{
	uint32_t seconds;

	cros_rtc_get_value(cros_rtc_dev, &seconds);

	return seconds;
}

void system_set_rtc(uint32_t seconds)
{
	cros_rtc_set_value(cros_rtc_dev, seconds);
}

void system_reset_rtc_alarm(void)
{
	if (!cros_rtc_dev) {
		/* TODO(b/183115086): check the error handler for NULL device */
		LOG_ERR("rtc_dev hasn't initialized.");
		return;
	}

	cros_rtc_reset_alarm(cros_rtc_dev);
}

/*
 * For NPCX series, The alarm counter only stores wakeup time in seconds.
 * Microseconds will be ignored.
 */
void system_set_rtc_alarm(uint32_t seconds, uint32_t microseconds)
{
	if (!cros_rtc_dev) {
		LOG_ERR("rtc_dev hasn't initialized.");
		return;
	}

	/* If time = 0, clear the current alarm */
	if (seconds == EC_RTC_ALARM_CLEAR && microseconds == 0) {
		system_reset_rtc_alarm();
		return;
	}

	seconds += system_get_rtc_sec();

	cros_rtc_set_alarm(cros_rtc_dev, seconds, microseconds);
}

/*
 * Return the seconds remaining before the RTC alarm goes off.
 * Returns 0 if alarm is not set.
 */
uint32_t system_get_rtc_alarm(void)
{
	uint32_t seconds, microseconds;

	cros_rtc_get_alarm(cros_rtc_dev, &seconds, &microseconds);

	/*
	 * Return 0:
	 * 1. If alarm is not set to go off, OR
	 * 2. If alarm is set and has already gone off
	 */
	if (seconds == 0) {
		return 0;
	}

	return seconds - system_get_rtc_sec();
}

/* Console commands */
void print_system_rtc(enum console_channel ch)
{
	uint32_t sec = system_get_rtc_sec();

	cprintf(ch, "RTC: 0x%08x (%d.00 s)\n", sec, sec);
}

/*
 * TODO(b/179055201): This is similar to the same function in some of the
 * chip-specific code. We should factor out the common parts.
 */
#ifdef CONFIG_PLATFORM_EC_CONSOLE_CMD_RTC
static int command_system_rtc(int argc, char **argv)
{
	if (argc == 3 && !strcasecmp(argv[1], "set")) {
		char *e;
		unsigned int t = strtoi(argv[2], &e, 0);

		if (*e)
			return EC_ERROR_PARAM2;

		system_set_rtc(t);
	} else if (argc > 1) {
		return EC_ERROR_INVAL;
	}

	print_system_rtc(CC_COMMAND);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rtc, command_system_rtc, "[set <seconds>]",
			"Get/set real-time clock");

#ifdef CONFIG_PLATFORM_EC_CONSOLE_CMD_RTC_ALARM
/**
 * Test the RTC alarm by setting an interrupt on RTC match.
 */
static int command_rtc_alarm_test(int argc, char **argv)
{
	int s = 1, us = 0;
	char *e;

	if (argc > 1) {
		s = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;
	}
	if (argc > 2) {
		us = strtoi(argv[2], &e, 10);
		if (*e)
			return EC_ERROR_PARAM2;
	}

	ccprintf("Setting RTC alarm\n");

	system_set_rtc_alarm(s, us);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rtc_alarm, command_rtc_alarm_test,
			"[seconds [microseconds]]", "Test alarm");
#endif /* CONFIG_PLATFORM_EC_CONSOLE_CMD_RTC_ALARM */
#endif /* CONFIG_PLATFORM_EC_CONSOLE_CMD_RTC */

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_RTC
static enum ec_status system_rtc_get_value(struct host_cmd_handler_args *args)
{
	struct ec_response_rtc *r = args->response;

	r->time = system_get_rtc_sec();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_GET_VALUE, system_rtc_get_value,
		     EC_VER_MASK(0));

static enum ec_status system_rtc_set_value(struct host_cmd_handler_args *args)
{
	const struct ec_params_rtc *p = args->params;

	system_set_rtc(p->time);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_SET_VALUE, system_rtc_set_value,
		     EC_VER_MASK(0));

static enum ec_status system_rtc_set_alarm(struct host_cmd_handler_args *args)
{
	const struct ec_params_rtc *p = args->params;

	system_set_rtc_alarm(p->time, 0);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_SET_ALARM, system_rtc_set_alarm,
		     EC_VER_MASK(0));

static enum ec_status system_rtc_get_alarm(struct host_cmd_handler_args *args)
{
	struct ec_response_rtc *r = args->response;

	r->time = system_get_rtc_alarm();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_GET_ALARM, system_rtc_get_alarm,
		     EC_VER_MASK(0));

#endif /* CONFIG_PLATFORM_EC_HOSTCMD_RTC */
