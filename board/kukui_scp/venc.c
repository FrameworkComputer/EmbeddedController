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
#include "venc.h"
#include "queue.h"
#include "queue_policies.h"

#define CPRINTF(format, args...) cprintf(CC_IPI, format, ##args)
#define CPRINTS(format, args...) cprints(CC_IPI, format, ##args)

/* Forwad declaration. */
static struct consumer const event_venc_consumer;
static void event_venc_written(struct consumer const *consumer, size_t count);

static struct queue const event_venc_queue = QUEUE_DIRECT(8,
	struct venc_msg, null_producer, event_venc_consumer);
static struct consumer const event_venc_consumer = {
	.queue = &event_venc_queue,
	.ops = &((struct consumer_ops const) {
		.written = event_venc_written,
	}),
};

static venc_msg_handler mtk_venc_msg_handle[VENC_MAX];

/* Stub functions only provided by private overlays. */
#ifndef HAVE_PRIVATE_MT8183
void venc_h264_msg_handler(void *data) {}
#endif

static void event_venc_written(struct consumer const *consumer, size_t count)
{
	task_wake(TASK_ID_VENC_SERVICE);
}

static void venc_h264_ipi_handler(int id, void *data, uint32_t len)
{
	struct venc_msg rsv_msg;

	if (!len)
		return;
	rsv_msg.type = VENC_H264;
	memcpy(rsv_msg.msg, data, MIN(len, sizeof(rsv_msg.msg)));

	/*
	 * If there is no other IPI handler touch this queue, we don't need to
	 * interrupt_disable() or task_disable_irq().
	 */
	if (!queue_add_unit(&event_venc_queue, &rsv_msg))
		CPRINTS("Could not send venc %d to the queue.", rsv_msg.type);
}
DECLARE_IPI(IPI_VENC_H264, venc_h264_ipi_handler, 0);

/* This function renames from venc_service_entry. */
void venc_service_task(void *u)
{
	struct venc_msg rsv_msg;
	size_t size;

	mtk_venc_msg_handle[VENC_H264] = venc_h264_msg_handler;
	while (1) {
		/*
		 * Queue unit is added in IPI handler, which is in ISR context.
		 * Disable IRQ to prevent a clobbered queue.
		 */
		ipi_disable_irq(SCP_IRQ_IPC0);
		size = queue_remove_unit(&event_venc_queue, &rsv_msg);
		ipi_enable_irq(SCP_IRQ_IPC0);

		if (!size)
			task_wait_event(-1);
		else if (mtk_venc_msg_handle[rsv_msg.type])
			venc_h264_msg_handler(rsv_msg.msg);
		else
			CPRINTS("venc handler %d not exists.", rsv_msg.type);
	}
}
