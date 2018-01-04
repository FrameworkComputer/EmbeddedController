/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Sync event driver.
 * Useful for recording the exact time a gpio interrupt happened in the
 * context of sensors. Originally created for a camera vsync signal.
 */

#include "accelgyro.h"
#include "config.h"
#include "console.h"
#include "driver/sync.h"
#include "hwtimer.h"
#include "task.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_SENSE, format, ## args)

#ifndef CONFIG_ACCEL_FIFO
#error This driver needs CONFIG_ACCEL_FIFO
#endif

#ifndef CONFIG_ACCEL_INTERRUPTS
#error This driver needs CONFIG_ACCEL_INTERRUPTS
#endif

static uint32_t previous_interrupt_timestamp, last_interrupt_timestamp;
static int event_counter;
struct ec_response_motion_sensor_data vector = {.flags = 0, .data = {0, 0, 0} };
int sync_enabled;

static int sync_read(const struct motion_sensor_t *s, vector_3_t v)
{
	v[0] = event_counter;
	return EC_SUCCESS;
}

/*
 * Since there's no such thing as data rate for this sensor, but the framework
 * still depends on being able to set this to 0 to disable it, we'll just use
 * non 0 rate values as an enable boolean.
 */
static int sync_set_data_rate(const struct motion_sensor_t *s,
				int rate, int roundup)
{
	sync_enabled = !!rate;
	CPRINTF("sync event driver enabling=%d\n", sync_enabled);
	return EC_SUCCESS;
}

static int sync_get_data_rate(const struct motion_sensor_t *s)
{
	return sync_enabled;
}

/* Upper half of the irq handler */
void sync_interrupt(enum gpio_signal signal)
{
	uint32_t timestamp = __hw_clock_source_read();

	if (!sync_enabled)
		return;

	last_interrupt_timestamp = timestamp;
	event_counter++;

	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_SYNC_INT_EVENT, 0);
}

/* Bottom half of the irq handler */
static int motion_irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	uint32_t timestamp;

	if (!(*event & CONFIG_SYNC_INT_EVENT))
		return EC_ERROR_NOT_HANDLED;

	/* this should be the atomic read */
	timestamp = last_interrupt_timestamp;

	if (previous_interrupt_timestamp == timestamp)
		return EC_ERROR_NOT_HANDLED; /* nothing new yet */
	previous_interrupt_timestamp = timestamp;

	vector.flags = MOTIONSENSE_SENSOR_FLAG_WAKEUP;
	vector.data[X] = event_counter;
	motion_sense_fifo_add_data(&vector, s, 1, timestamp);
	return EC_SUCCESS;
}

static int sync_init(const struct motion_sensor_t *s)
{
	last_interrupt_timestamp = __hw_clock_source_read();
	previous_interrupt_timestamp = last_interrupt_timestamp;
	event_counter = 0;
	vector.sensor_num = s - motion_sensors;
	sync_enabled = 0;
	return 0;
}

#ifdef CONFIG_SYNC_COMMAND
static int command_sync(int argc, char **argv)
{
	sync_interrupt(GPIO_SYNC_INT);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sync, command_sync,
	NULL,
	"Simulates a sync event");
#endif

const struct accelgyro_drv sync_drv = {
	.init = sync_init,
	.read = sync_read,
	.set_data_rate = sync_set_data_rate,
	.get_data_rate = sync_get_data_rate,
	.irq_handler = motion_irq_handler,
};

