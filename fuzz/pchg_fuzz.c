/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test peripheral device charger module.
 */

#define HIDE_EC_STDLIB
#include "common.h"
#include "compile_time_macros.h"
#include "driver/nfc/ctn730.h"
#include "peripheral_charger.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define TASK_EVENT_FUZZ TASK_EVENT_CUSTOM_BIT(0)

extern struct pchg_drv ctn730_drv;
struct pchg pchgs[] = {
	[0] = {
		.cfg = &(const struct pchg_config) {
			.drv = &ctn730_drv,
			.i2c_port = I2C_PORT_WLC,
			.irq_pin = GPIO_WLC_IRQ_CONN,
			.full_percent = 96,
			.block_size = 128,
		},
		.events = QUEUE_NULL(PCHG_EVENT_QUEUE_SIZE, enum pchg_event),
	},
};
const int pchg_count = ARRAY_SIZE(pchgs);

static pthread_cond_t done_cond;
static pthread_mutex_t lock;

#define MAX_MESSAGES 8
#define MAX_MESSAGE_SIZE (sizeof(struct ctn730_msg) \
			  + member_size(struct ctn730_msg, length) * 256)
static uint8_t input[MAX_MESSAGE_SIZE * MAX_MESSAGES];
static uint8_t *head, *tail;
static bool data_available;

int pchg_i2c_xfer(int port, uint16_t addr_flags,
		  const uint8_t *out, int out_size,
		  uint8_t *in, int in_size, int flags)
{
	if (port != I2C_PORT_WLC || addr_flags != CTN730_I2C_ADDR)
		return EC_ERROR_INVAL;

	if (in == NULL || in_size == 0)
		return EC_SUCCESS;

	if (head + in_size >= tail) {
		data_available = false;
		return EC_ERROR_OVERFLOW;
	}

	memcpy(in, head, in_size);
	head += in_size;

	return EC_SUCCESS;
}
DECLARE_TEST_I2C_XFER(pchg_i2c_xfer);

/*
 * Task for generating IRQs. The task priority is lower than the PCHG task so
 * that it can yield the CPU to the PCHG task.
 */
void irq_task(int argc, char **argv)
{
	ccprints("%s task started", __func__);
	wait_for_task_started();

	while (1) {
		int i = 0;

		task_wait_event_mask(TASK_EVENT_FUZZ, -1);
		test_chipset_on();

		while (data_available && i++ < MAX_MESSAGES)
			pchg_irq(pchgs[0].cfg->irq_pin);

		test_chipset_off();

		pthread_mutex_lock(&lock);
		pthread_cond_signal(&done_cond);
		pthread_mutex_unlock(&lock);
	}

}

void run_test(int argc, char **argv)
{
	ccprints("Fuzzing task started");
	task_wait_event(-1);
}

int test_fuzz_one_input(const uint8_t *data, unsigned int size)
{
	/* We're not interested in too small or too large input. */
	if (size < sizeof(struct ctn730_msg) || sizeof(input) < size)
		return 0;

	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&done_cond, NULL);

	head = input;
	tail = input + size;
	memcpy(input, data, size);
	data_available = true;

	task_set_event(TASK_ID_IRQ, TASK_EVENT_FUZZ);

	pthread_mutex_lock(&lock);
	pthread_cond_wait(&done_cond, &lock);
	pthread_mutex_unlock(&lock);

	return 0;
}
