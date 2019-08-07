/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "queue_policies.h"
#include "task.h"
#include "util.h"

#include "chip/mt_scp/ipi_chip.h"
#include "chip/mt_scp/registers.h"
#include "mdp_ipi_message.h"

#define CPRINTF(format, args...) cprintf(CC_IPI, format, ##args)
#define CPRINTS(format, args...) cprints(CC_IPI, format, ##args)

/* Forwad declaration. */
static struct consumer const event_mdp_consumer;
static void event_mdp_written(struct consumer const *consumer, size_t count);

static struct queue const event_mdp_queue = QUEUE_DIRECT(4,
	struct mdp_msg_service, null_producer, event_mdp_consumer);
static struct consumer const event_mdp_consumer = {
	.queue = &event_mdp_queue,
	.ops = &((struct consumer_ops const) {
		.written = event_mdp_written,
	}),
};

/* Stub functions only provided by private overlays. */
#ifndef HAVE_PRIVATE_MT8183
void mdp_common_init(void) {}
void mdp_ipi_task_handler(void *pvParameters) {}
#endif

static void event_mdp_written(struct consumer const *consumer, size_t count)
{
	task_wake(TASK_ID_MDP_SERVICE);
}

static void mdp_ipi_handler(int id, void *data, unsigned int len)
{
	struct mdp_msg_service cmd;

	cmd.id = id;
	memcpy(cmd.msg, data, MIN(len, sizeof(cmd.msg)));

	/*
	 * If there is no other IPI handler touch this queue, we don't need to
	 * interrupt_disable() or task_disable_irq().
	 */
	if (!queue_add_unit(&event_mdp_queue, &cmd))
		CPRINTS("Could not send mdp id: %d to the queue.", id);
}
DECLARE_IPI(IPI_MDP_INIT, mdp_ipi_handler, 0);
DECLARE_IPI(IPI_MDP_FRAME, mdp_ipi_handler, 0);
DECLARE_IPI(IPI_MDP_DEINIT, mdp_ipi_handler, 0);

/* This function renames from mdp_service_entry. */
void mdp_service_task(void *u)
{
	struct mdp_msg_service rsv_msg;
	size_t size;

	mdp_common_init();

	while (1) {
		/*
		 * Queue unit is added in IPI handler, which is in ISR context.
		 * Disable IRQ to prevent a clobbered queue.
		 */
		ipi_disable_irq(SCP_IRQ_IPC0);
		size = queue_remove_unit(&event_mdp_queue, &rsv_msg);
		ipi_enable_irq(SCP_IRQ_IPC0);

		if (!size)
			task_wait_event(-1);
		else
			mdp_ipi_task_handler(&rsv_msg);
	}
}
