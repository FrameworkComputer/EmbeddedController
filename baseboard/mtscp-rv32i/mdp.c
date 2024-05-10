/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ipi_chip.h"
#include "mdp.h"
#include "queue_policies.h"
#include "registers.h"
#include "task.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_IPI, format, ##args)
#define CPRINTS(format, args...) cprints(CC_IPI, format, ##args)

static void event_mdp_written(struct consumer const *consumer, size_t count)
{
	task_wake(TASK_ID_MDP_SERVICE);
}
static struct consumer const event_mdp_consumer;
static struct queue const event_mdp_queue = QUEUE_DIRECT(
	4, struct mdp_msg_service, null_producer, event_mdp_consumer);
static struct consumer const event_mdp_consumer = {
	.queue = &event_mdp_queue,
	.ops = &((struct consumer_ops const){
		.written = event_mdp_written,
	}),
};

/* Stub functions only provided by private overlays. */
#if !defined(HAVE_PRIVATE_MT_SCP) || defined(HAVE_PRIVATE_MT_NO_MDP)
void mdp_common_init(void)
{
}
void mdp_ipi_task_handler(void *pvParameters)
{
}
#endif

static void mdp_ipi_handler(int id, void *data, unsigned int len)
{
	struct mdp_msg_service rsv_msg;

	if (!len) {
		CPRINTS("len is zero.");
		return;
	}

	rsv_msg.id = id;
	memcpy(rsv_msg.msg, data, MIN(len, sizeof(rsv_msg.msg)));

	/*
	 * If there is no other IPI handler touch this queue, we don't need to
	 * interrupt_disable() or task_disable_irq().
	 */
	if (!queue_add_unit(&event_mdp_queue, &rsv_msg))
		CPRINTS("Could not send mdp id: %d to the queue.", id);
}
DECLARE_IPI(SCP_IPI_MDP_INIT, mdp_ipi_handler, 0);
DECLARE_IPI(SCP_IPI_MDP_FRAME, mdp_ipi_handler, 0);
DECLARE_IPI(SCP_IPI_MDP_DEINIT, mdp_ipi_handler, 0);

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
		ipi_disable_irq();
		size = queue_remove_unit(&event_mdp_queue, &rsv_msg);
		ipi_enable_irq();

		if (!size)
			task_wait_event(-1);
		else
			mdp_ipi_task_handler(&rsv_msg);
	}
}
