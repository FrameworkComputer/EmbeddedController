/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PERIPHERAL_CHARGER_H
#define __CROS_EC_PERIPHERAL_CHARGER_H

#include "common.h"
#include "gpio.h"
#include "queue.h"
#include "stdbool.h"
#include "task.h"

/*
 * Peripheral charge manager
 *
 * Peripheral charge manager (PCHG) is a state machine (SM), which manages
 * charge ports to charge peripheral devices. Events can be generated
 * externally (by a charger chip) or internally (by a host command or the SM
 * itself). Events are queued and handled first-come-first-serve basis.
 *
 * Peripheral charger drivers should implement struct pchg_drv. Each operation
 * can be synchronous or asynchronous depending on the chip. If a function
 * works synchronously, it should return EC_SUCCESS. That'll make the SM
 * immediately queue the next event (if applicable) and transition to the next
 * state. If a function works asynchronously, it should return
 * EC_SUCCESS_IN_PROGRESS. That'll make the SM stay in the same state. The SM
 * is expected to receive IRQ for further information about the operation,
 * which may or may not make the SM transition to the next state.
 *
 * Roughly speaking the SM looks as follows:
 *
 *                  +---------------+
 *                  |     RESET     |
 *                  +-------+-------+
 *                          |
 *                          | INITIALIZED
 *                          v
 *                  +-------+-------+
 *                  |  INITIALIZED  |<--------------+
 *                  +------+-+------+               |
 *                         | ^                      |
 *                 ENABLED | | DISABLED             |
 *                         v |                      |
 *                  +------+--------+               |
 *   +------------->+    ENABLED    |               |
 *   |              +-------+-------+               |
 *   |                      |                       |
 *   |                      | DEVICE_DETECTED       |
 *   |                      v                       |
 *   |              +-------+-------+               |
 *   +--------------+   DETECTED    +---------------+
 *   | DEVICE_LOST  +------+-+------+  ERROR        |
 *   |                     | ^                      |
 *   |      CHARGE_STARTED | | CHARGE_ENDED         |
 *   |                     | | CHARGE_STOPPED       |
 *   |                     v |                      |
 *   |              +------+-+------+               |
 *   +--------------+   CHARGING    +---------------+
 *     DEVICE_LOST  +---------------+  ERROR
 *
 */

/* Size of event queue. Use it to initialize struct pchg.events. */
#define PCHG_EVENT_QUEUE_SIZE	8

enum pchg_event {
	/* No event */
	PCHG_EVENT_NONE = 0,

	/* IRQ is pending. */
	PCHG_EVENT_IRQ,

	/* External Events */
	PCHG_EVENT_INITIALIZED,
	PCHG_EVENT_ENABLED,
	PCHG_EVENT_DISABLED,
	PCHG_EVENT_DEVICE_DETECTED,
	PCHG_EVENT_DEVICE_LOST,
	PCHG_EVENT_CHARGE_STARTED,
	PCHG_EVENT_CHARGE_UPDATE,
	PCHG_EVENT_CHARGE_ENDED,
	PCHG_EVENT_CHARGE_STOPPED,
	PCHG_EVENT_CHARGE_ERROR,

	/* Internal (a.k.a. Host) Events */
	PCHG_EVENT_INITIALIZE,
	PCHG_EVENT_ENABLE,
	PCHG_EVENT_DISABLE,
};

enum pchg_state {
	/* Charger is reset and not initialized. */
	PCHG_STATE_RESET = 0,
	/* Charger is initialized or disabled. */
	PCHG_STATE_INITIALIZED,
	/* Charger is enabled and ready to detect a device. */
	PCHG_STATE_ENABLED,
	/* Device is detected in proximity. */
	PCHG_STATE_DETECTED,
	/* Device is being charged. */
	PCHG_STATE_CHARGING,
};

enum pchg_error {
	PCHG_ERROR_NONE = 0,
	/* Error initiated by host. */
	PCHG_ERROR_HOST = BIT(0),
	PCHG_ERROR_OVER_TEMPERATURE = BIT(1),
	PCHG_ERROR_OVER_CURRENT = BIT(2),
	PCHG_ERROR_FOREIGN_OBJECT = BIT(3),
};

/**
 * Data struct describing the configuration of a peripheral charging port.
 */
struct pchg_config {
	/* Charger driver */
	const struct pchg_drv *drv;
	/* I2C port number */
	const int i2c_port;
	/* GPIO pin used for IRQ */
	const enum gpio_signal irq_pin;
};

/**
 * Data struct describing the status of a peripheral charging port. It provides
 * the state machine and a charger driver with a context to work on.
 */
struct pchg {
	/* Static configuration */
	const struct pchg_config * const cfg;
	/* Current state of the port */
	enum pchg_state state;
	/* Event queue */
	struct queue const events;
	/* Event queue mutex */
	struct mutex mtx;
	/* 1:Pending IRQ 0:No pending IRQ */
	uint32_t irq;
	/* Event currently being handled */
	enum pchg_event event;
	/* Error (enum pchg_error). Port is disabled until it's cleared. */
	uint32_t error;
	/* Battery percentage (0% ~ 100%) of the connected peripheral device */
	uint8_t battery_percent;
};

/**
 * Peripheral charger driver
 */
struct pchg_drv {
	/* Initialize the charger. */
	int (*init)(struct pchg *ctx);
	/* Enable/disable the charger. */
	int (*enable)(struct pchg *ctx, bool enable);
	/* Get event info. */
	int (*get_event)(struct pchg *ctx);
};

/**
 * Array storing configs and states of all the peripheral charging ports.
 * Should be defined in board.c.
 */
extern struct pchg pchgs[];
extern const int pchg_count;

/* Utility macro converting port config to port number. */
#define PCHG_CTX_TO_PORT(ctx)	((ctx) - &pchgs[0])

/**
 * Interrupt handler for a peripheral charger.
 *
 * @param signal
 */
void pchg_irq(enum gpio_signal signal);

/**
 * Task running a state machine for charging peripheral devices.
 */
void pchg_task(void *u);

#endif /* __CROS_EC_PERIPHERAL_CHARGER_H */
