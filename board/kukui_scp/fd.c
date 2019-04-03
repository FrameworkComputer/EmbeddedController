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
#include "fd.h"
#include "queue.h"
#include "queue_policies.h"

#define CPRINTF(format, args...) cprintf(CC_IPI, format, ##args)
#define CPRINTS(format, args...) cprints(CC_IPI, format, ##args)

/* Forwad declaration. */
static struct consumer const event_fd_consumer;
static void event_fd_written(struct consumer const *consumer, size_t count);

static struct queue const fd_queue = QUEUE_DIRECT(4, struct fd_msg,
						  null_producer,
						  event_fd_consumer);
static struct consumer const event_fd_consumer = {
	.queue = &fd_queue,
	.ops = &((struct consumer_ops const) {
		.written = event_fd_written,
	}),
};

/* Stub functions only provided by private overlays. */
// Jerry TODO implement private part and remove this
#ifndef HAVE_PRIVATE_MT8183
void fd_ipi_msg_handler(void *data) {}
#endif

static void event_fd_written(struct consumer const *consumer, size_t count)
{
	task_wake(TASK_ID_FD_SERVICE);
}

static void fd_ipi_handler(int id, void *data, uint32_t len)
{
	struct fd_msg rsv_msg;

	if (!len)
		return;

	rsv_msg.type = IPI_FD_CMD;
	memcpy(rsv_msg.msg, data, MIN(len, sizeof(rsv_msg.msg)));

	/*
	 * If there is no other IPI handler touch this queue, we don't need to
	 * interrupt_disable() or task_disable_irq().
	 */
	if (!queue_add_unit(&fd_queue, &rsv_msg))
		CPRINTS("Could not send fd %d to the queue.", rsv_msg.type);
}
DECLARE_IPI(IPI_FD_CMD, fd_ipi_handler, 0);

/* This function renames from fd_service_entry. */
void fd_service_task(void *u)
{
	struct fd_msg rsv_msg;
	size_t size;

	while (1) {
		/*
		 * Queue unit is added in IPI handler, which is in ISR context.
		 * Disable IRQ to prevent a clobbered queue.
		 */
		ipi_disable_irq(SCP_IRQ_IPC0);
		size = queue_remove_unit(&fd_queue, &rsv_msg);
		ipi_enable_irq(SCP_IRQ_IPC0);

		if (!size)
			task_wait_event(-1);
		else
			fd_ipi_msg_handler(rsv_msg.msg);
	}
}
