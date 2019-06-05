/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chip/mt_scp/ipi_chip.h"
#include "chip/mt_scp/registers.h"
#include "console.h"
#include "hooks.h"
#include "task.h"
#include "util.h"
#include "vdec.h"
#include "queue.h"
#include "queue_policies.h"

#define CPRINTF(format, args...) cprintf(CC_IPI, format, ##args)
#define CPRINTS(format, args...) cprints(CC_IPI, format, ##args)

/* Forwad declaration. */
static struct consumer const event_vdec_consumer;
static void event_vdec_written(struct consumer const *consumer, size_t count);

static struct queue const event_vdec_queue = QUEUE_DIRECT(8,
	struct vdec_msg, null_producer, event_vdec_consumer);
static struct consumer const event_vdec_consumer = {
	.queue = &event_vdec_queue,
	.ops = &((struct consumer_ops const) {
		.written = event_vdec_written,
	}),
};

/* Stub functions only provided by private overlays. */
#ifndef HAVE_PRIVATE_MT8183
void vdec_h264_service_init(void) {}
void vdec_h264_msg_handler(void *data) {}
#endif

static vdec_msg_handler mtk_vdec_msg_handle[VDEC_MAX];

static void event_vdec_written(struct consumer const *consumer, size_t count)
{
	task_wake(TASK_ID_VDEC_SERVICE);
}

static void vdec_h264_ipi_handler(int id, void *data, uint32_t len)
{
	struct vdec_msg rsv_msg;

	if (!len)
		return;

	rsv_msg.type = VDEC_H264;
	memcpy(rsv_msg.msg, data, MIN(len, sizeof(rsv_msg.msg)));

	/*
	 * If there is no other IPI handler touch this queue, we don't need to
	 * interrupt_disable() or task_disable_irq().
	 */
	if (!queue_add_unit(&event_vdec_queue, &rsv_msg))
		CPRINTS("Could not send vdec %d to the queue.", rsv_msg.type);
}
DECLARE_IPI(IPI_VDEC_H264, vdec_h264_ipi_handler, 0);

/* This function renames from vdec_service_entry. */
void vdec_service_task(void *u)
{
	struct vdec_msg rsv_msg;
	size_t size;

	vdec_h264_service_init();
	mtk_vdec_msg_handle[VDEC_H264] = vdec_h264_msg_handler;

	while (1) {
		/*
		 * Queue unit is added in IPI handler, which is in ISR context.
		 * Disable IRQ to prevent a clobbered queue.
		 */
		ipi_disable_irq(SCP_IRQ_IPC0);
		size = queue_remove_unit(&event_vdec_queue, &rsv_msg);
		ipi_enable_irq(SCP_IRQ_IPC0);

		if (!size)
			task_wait_event(-1);
		else if (mtk_vdec_msg_handle[rsv_msg.type])
			vdec_h264_msg_handler(rsv_msg.msg);
		else
			CPRINTS("vdec handler %d not exists.", rsv_msg.type);
	}
}
