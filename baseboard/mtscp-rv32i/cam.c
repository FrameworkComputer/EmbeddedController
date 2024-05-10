/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cam.h"
#include "console.h"
#include "dma.h"
#include "ipi_chip.h"
#include "queue_policies.h"
#include "registers.h"
#include "task.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/* Forwad declaration. */
static struct consumer const event_cam_consumer;
#if defined(BOARD_GERALT_SCP_CORE1)
static struct consumer const event_img_consumer;
#endif
static void event_cam_written(struct consumer const *consumer, size_t count);
#if defined(BOARD_GERALT_SCP_CORE1)
static void event_img_written(struct consumer const *consumer, size_t count);
#endif

static struct queue const event_cam_queue =
	QUEUE_DIRECT(8, struct cam_msg, null_producer, event_cam_consumer);
#if defined(BOARD_GERALT_SCP_CORE1)
static struct queue const event_img_queue =
	QUEUE_DIRECT(32, struct cam_msg, null_producer, event_img_consumer);
#endif

static struct consumer const event_cam_consumer = {
	.queue = &event_cam_queue,
	.ops = &((struct consumer_ops const){
		.written = event_cam_written,
	}),
};
#if defined(BOARD_GERALT_SCP_CORE1)
static struct consumer const event_img_consumer = {
	.queue = &event_img_queue,
	.ops = &((struct consumer_ops const){
		.written = event_img_written,
	}),
};
#endif
/* Stub functions only provided by private overlays. */
#ifndef HAVE_PRIVATE_MT_SCP_CORE1
void ipi_cam_handler(void *data)
{
}
void ipi_img_handler(void *data)
{
}
int32_t startRED(void)
{
	return 0;
}
void img_task_handler(void)
{
}
#endif

static void event_cam_written(struct consumer const *consumer, size_t count)
{
	task_wake(TASK_ID_CAM_SERVICE);
}
#if defined(BOARD_GERALT_SCP_CORE1)
static void event_img_written(struct consumer const *consumer, size_t count)
{
	task_wake(TASK_ID_IMG_SERVICE);
}
#endif

static void cam_ipi_handler(int id, void *data, uint32_t len)
{
	struct cam_msg rsv_msg;
	int ret = 0;

	if (!len)
		return;

	rsv_msg.id = id;
	memcpy(rsv_msg.msg, data, MIN(len, sizeof(rsv_msg.msg)));

	/*
	 * If there is no other IPI handler touch this queue, we don't need to
	 * interrupt_disable() or task_disable_irq().
	 */
	if (id == SCP_IPI_ISP_CMD || id == SCP_IPI_ISP_FRAME)
		ret = queue_add_unit(&event_cam_queue, &rsv_msg);
#if defined(BOARD_GERALT_SCP_CORE1)
	else
		ret = queue_add_unit(&event_img_queue, &rsv_msg);
#endif

	if (!ret)
		CPRINTS("Could not send ipi cmd %d to the queue", id);
}

DECLARE_IPI(SCP_IPI_ISP_CMD, cam_ipi_handler, 0);
DECLARE_IPI(SCP_IPI_ISP_FRAME, cam_ipi_handler, 0);
DECLARE_IPI(SCP_IPI_ISP_IMG_CMD, cam_ipi_handler, 0);

/* This function renames from cam_service_entry. */
void cam_service_task(void *u)
{
	struct cam_msg rsv_msg;
	size_t size;

	while (1) {
		/*
		 * Queue unit is added in IPI handler, which is in ISR context.
		 * Disable IRQ to prevent a clobbered queue.
		 */
		ipi_disable_irq();
		size = queue_remove_unit(&event_cam_queue, &rsv_msg);
		ipi_enable_irq();

		if (!size)
			task_wait_event(-1);
		else
			ipi_cam_handler(&rsv_msg);
	}
}
#if defined(BOARD_GERALT_SCP_CORE1)
void img_service_task(void *u)
{
	struct cam_msg rsv_msg;
	size_t size;

	while (1) {
		/*
		 * Queue unit is added in IPI handler, which is in ISR context.
		 * Disable IRQ to prevent a clobbered queue.
		 */
		ipi_disable_irq();
		size = queue_remove_unit(&event_img_queue, &rsv_msg);
		ipi_enable_irq();

		if (!size) {
			if (img_task_working)
				task_wake(TASK_ID_IMG_HANDLER);
			else
				task_wait_event(-1);

		} else
			ipi_img_handler(&rsv_msg);
	}
}

void img_handler_task(void *u)
{
	CPRINTS("img_handler_task");
	startRED();
	img_task_handler();
}
#endif
