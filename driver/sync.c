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
#include "motion_sense_fifo.h"
#include "queue.h"
#include "task.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_SENSE, format, ## args)

#ifndef CONFIG_ACCEL_FIFO
#error This driver needs CONFIG_ACCEL_FIFO
#endif

#ifndef CONFIG_ACCEL_INTERRUPTS
#error This driver needs CONFIG_ACCEL_INTERRUPTS
#endif

struct sync_event_t {
	uint32_t timestamp;
	int counter;
};

static struct queue const sync_event_queue =
	QUEUE_NULL(CONFIG_SYNC_QUEUE_SIZE, struct sync_event_t);

struct sync_event_t next_event;
struct ec_response_motion_sensor_data vector =
	{.flags = MOTIONSENSE_SENSOR_FLAG_WAKEUP, .data = {0, 0, 0} };
int sync_enabled;

static int sync_read(const struct motion_sensor_t *s, intv3_t v)
{
	v[0] = next_event.counter;
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
	next_event.timestamp = __hw_clock_source_read();

	if (!sync_enabled)
		return;

	next_event.counter++;
	queue_add_unit(&sync_event_queue, &next_event);

	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_SYNC_INT_EVENT, 0);
}

/* Bottom half of the irq handler */
static int motion_irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	struct sync_event_t sync_event;

	if (!(*event & CONFIG_SYNC_INT_EVENT))
		return EC_ERROR_NOT_HANDLED;

	while (queue_remove_unit(&sync_event_queue, &sync_event)) {
		vector.data[X] = sync_event.counter;
		motion_sense_fifo_stage_data(
			&vector, s, 1, sync_event.timestamp);
	}
	motion_sense_fifo_commit_data();

	return EC_SUCCESS;
}

static int sync_init(const struct motion_sensor_t *s)
{
	vector.sensor_num = s - motion_sensors;
	sync_enabled = 0;
	next_event.counter = 0;
	queue_init(&sync_event_queue);
	return 0;
}

#ifdef CONFIG_SYNC_COMMAND
static int command_sync(int argc, char **argv)
{
	int count = 1, i;

	if (argc > 1)
		count = strtoi(argv[1], 0, 0);

	for (i = 0; i < count; i++)
		sync_interrupt(GPIO_SYNC_INT);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sync, command_sync,
	"[count]",
	"Simulates sync events");
#endif

const struct accelgyro_drv sync_drv = {
	.init = sync_init,
	.read = sync_read,
	.set_data_rate = sync_set_data_rate,
	.get_data_rate = sync_get_data_rate,
	.irq_handler = motion_irq_handler,
};

