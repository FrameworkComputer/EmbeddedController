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
#include "venc.h"

#define CPRINTF(format, args...) cprintf(CC_IPI, format, ##args)
#define CPRINTS(format, args...) cprints(CC_IPI, format, ##args)

static void event_venc_written(struct consumer const *consumer, size_t count)
{
	task_wake(TASK_ID_VENC_SERVICE);
}
static struct consumer const event_venc_consumer;
static struct queue const event_venc_queue = QUEUE_DIRECT(8,
	struct venc_msg, null_producer, event_venc_consumer);
static struct consumer const event_venc_consumer = {
	.queue = &event_venc_queue,
	.ops = &((struct consumer_ops const) {
		.written = event_venc_written,
	}),
};

/* Stub functions only provided by private overlays. */
#ifndef HAVE_PRIVATE_MT_SCP
void venc_h264_msg_handler(void *data) {}
#endif

static void venc_h264_ipi_handler(int id, void *data, uint32_t len)
{
	struct venc_msg rsv_msg;

	if (!len) {
		CPRINTS("len is zero.");
		return;
	}

	rsv_msg.type = VENC_H264;
	memcpy(rsv_msg.msg, data, MIN(len, sizeof(rsv_msg.msg)));

	/*
	 * If there is no other IPI handler touch this queue, we don't need to
	 * interrupt_disable() or task_disable_irq().
	 */
	if (!queue_add_unit(&event_venc_queue, &rsv_msg))
		CPRINTS("Could not send venc %d to the queue.", rsv_msg.type);
}
DECLARE_IPI(SCP_IPI_VENC_H264, venc_h264_ipi_handler, 0);

void venc_service_task(void *u)
{
	struct venc_msg rsv_msg;
	size_t size;

	typedef void (*venc_msg_handler)(void *msg);
	static venc_msg_handler mtk_venc_msg_handle[VENC_MAX] = {
		[VENC_H264] = venc_h264_msg_handler,
	};

	while (1) {
		/*
		 * Queue unit is added in IPI handler, which is in ISR context.
		 * Disable IRQ to prevent a clobbered queue.
		 */
		ipi_disable_irq();
		size = queue_remove_unit(&event_venc_queue, &rsv_msg);
		ipi_enable_irq();

		if (!size)
			task_wait_event(-1);
		else if (mtk_venc_msg_handle[rsv_msg.type])
			mtk_venc_msg_handle[rsv_msg.type](rsv_msg.msg);
		else
			CPRINTS("venc handler %d not exists.", rsv_msg.type);
	}
}
