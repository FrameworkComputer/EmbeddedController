/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "hooks.h"
#include "ipi_chip.h"
#include "queue.h"
#include "queue_policies.h"
#include "registers.h"
#include "task.h"
#include "util.h"
#include "vdec.h"

#define CPRINTF(format, args...) cprintf(CC_IPI, format, ##args)
#define CPRINTS(format, args...) cprints(CC_IPI, format, ##args)

static void event_vdec_written(struct consumer const *consumer, size_t count)
{
	task_wake(TASK_ID_VDEC_SERVICE);
}
static struct consumer const event_vdec_consumer;
static struct queue const event_vdec_queue = QUEUE_DIRECT(8,
	struct vdec_msg, null_producer, event_vdec_consumer);
static struct consumer const event_vdec_consumer = {
	.queue = &event_vdec_queue,
	.ops = &((struct consumer_ops const) {
		.written = event_vdec_written,
	}),
};

static void event_vdec_core_written(struct consumer const *consumer,
				    size_t count)
{
	task_wake(TASK_ID_VDEC_CORE_SERVICE);
}
static struct consumer const event_vdec_core_consumer;
static struct queue const event_vdec_core_queue = QUEUE_DIRECT(8,
	struct vdec_msg, null_producer, event_vdec_core_consumer);
static struct consumer const event_vdec_core_consumer = {
	.queue = &event_vdec_core_queue,
	.ops = &((struct consumer_ops const) {
		.written = event_vdec_core_written,
	}),
};

/* Stub functions only provided by private overlays. */
#ifndef HAVE_PRIVATE_MT_SCP
void vdec_msg_handler(void *data) {}
void vdec_core_msg_handler(void *data) {}
#endif

static void vdec_h264_ipi_handler(int id, void *data, uint32_t len)
{
	struct vdec_msg rsv_msg;

	if (!len) {
		CPRINTS("len is zero.");
		return;
	}

	rsv_msg.type = VDEC_LAT;
	memcpy(rsv_msg.msg, data, MIN(len, sizeof(rsv_msg.msg)));

	/*
	 * If there is no other IPI handler touch this queue, we don't need to
	 * interrupt_disable() or task_disable_irq().
	 */
	if (!queue_add_unit(&event_vdec_queue, &rsv_msg))
		CPRINTS("Could not send vdec %d to the queue.", rsv_msg.type);
}
DECLARE_IPI(SCP_IPI_VDEC_LAT, vdec_h264_ipi_handler, 0);

static void vdec_h264_ipi_core_handler(int id, void *data, uint32_t len)
{
	struct vdec_msg rsv_msg;

	if (!len) {
		CPRINTS("len is zero.");
		return;
	}

	rsv_msg.type = VDEC_CORE;
	memcpy(rsv_msg.msg, data, MIN(len, sizeof(rsv_msg.msg)));

	/*
	 * If there is no other IPI handler touch this queue, we don't need to
	 * interrupt_disable() or task_disable_irq().
	 */
	if (!queue_add_unit(&event_vdec_core_queue, &rsv_msg))
		CPRINTS("Could not send vdec %d to core queue.", rsv_msg.type);
}
DECLARE_IPI(SCP_IPI_VDEC_CORE, vdec_h264_ipi_core_handler, 0);

void vdec_service_task(void *u)
{
	struct vdec_msg rsv_msg;
	size_t size;

	while (1) {
		/*
		 * Queue unit is added in IPI handler, which is in ISR context.
		 * Disable IRQ to prevent a clobbered queue.
		 */
		ipi_disable_irq();
		size = queue_remove_unit(&event_vdec_queue, &rsv_msg);
		ipi_enable_irq();

		if (!size)
			task_wait_event(-1);
		else
			vdec_msg_handler(rsv_msg.msg);
	}
}

void vdec_core_service_task(void *u)
{
	struct vdec_msg rsv_msg;
	size_t size;

	while (1) {
		/*
		 * Queue unit is added in IPI handler, which is in ISR context.
		 * Disable IRQ to prevent a clobbered queue.
		 */
		ipi_disable_irq();
		size = queue_remove_unit(&event_vdec_core_queue, &rsv_msg);
		ipi_enable_irq();

		if (!size)
			task_wait_event(-1);
		else
			vdec_core_msg_handler(rsv_msg.msg);
	}
}
