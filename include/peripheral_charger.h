/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PERIPHERAL_CHARGER_H
#define __CROS_EC_PERIPHERAL_CHARGER_H

#include "atomic.h"
#include "common.h"
#include "ec_commands.h"
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
 *                  +---------------+
 *                  |  INITIALIZED  |<--------------+
 *                  +------+--------+               |
 *                         | ^                      |
 *                 ENABLED | | DISABLED             |
 *                         v |                      |
 *                  +--------+------+               |
 *   +------------->|    ENABLED    |               |
 *   |              +-----+------+--+               |
 *   |                    |      |                  |
 *   |    DEVICE_DETECTED |      | DEVICE_CONNECTED |
 *   |                    v      |                  |
 *   | DEVICE_LOST  +----------+ |                  |
 *   +--------------+ DETECTED +-|------------------+
 *   |              +-----+----+ |     ERROR        |
 *   |                    |      |                  |
 *   |    DEVICE_CONNECTED|      |                  |
 *   |                    v      v                  |
 *   |              +---------------+               |
 *   +--------------+   CONNECTED   +---------------+
 *   | DEVICE_LOST  +------+--------+  ERROR        |
 *   |                     | ^                      |
 *   |      CHARGE_STARTED | | CHARGE_ENDED         |
 *   |                     | | CHARGE_STOPPED       |
 *   |                     v |                      |
 *   |              +--------+------+               |
 *   +--------------+   CHARGING    +---------------+
 *     DEVICE_LOST  +---------------+  ERROR
 *
 *
 * In download (firmware update) mode, the state machine transitions as follows:
 *
 *                  +---------------+
 *                  |   DOWNLOAD    |
 *                  +------+--------+
 *                         | ^
 *             UPDATE_OPEN | |
 *                         | | UPDATE_CLOSE
 *                         v |
 *                  +--------+------+
 *              +-->|  DOWNLOADING  |
 *              |   +------+--------+
 *              |          |
 *              +----------+
 *              UPDATE_WRITE
 */

/* Size of event queue. Use it to initialize struct pchg.events. */
#define PCHG_EVENT_QUEUE_SIZE 8

enum pchg_event {
	/* No event */
	PCHG_EVENT_NONE = 0,

	/* IRQ is pending. */
	PCHG_EVENT_IRQ,

	/* External Events */
	PCHG_EVENT_RESET,
	PCHG_EVENT_INITIALIZED,
	PCHG_EVENT_ENABLED,
	PCHG_EVENT_DISABLED,
	PCHG_EVENT_DEVICE_DETECTED,
	PCHG_EVENT_DEVICE_CONNECTED,
	PCHG_EVENT_DEVICE_LOST,
	PCHG_EVENT_CHARGE_STARTED,
	PCHG_EVENT_CHARGE_UPDATE,
	PCHG_EVENT_CHARGE_ENDED,
	PCHG_EVENT_CHARGE_STOPPED,
	PCHG_EVENT_UPDATE_OPENED,
	PCHG_EVENT_UPDATE_CLOSED,
	PCHG_EVENT_UPDATE_WRITTEN,
	PCHG_EVENT_IN_NORMAL,

	/* Errors */
	PCHG_EVENT_CHARGE_ERROR,
	PCHG_EVENT_UPDATE_ERROR,
	PCHG_EVENT_OTHER_ERROR,

	/* Internal (a.k.a. Host) Events */
	PCHG_EVENT_ENABLE,
	PCHG_EVENT_DISABLE,
	PCHG_EVENT_UPDATE_OPEN,
	PCHG_EVENT_UPDATE_WRITE,
	PCHG_EVENT_UPDATE_CLOSE,

	/* Counter. Add new entry above. */
	PCHG_EVENT_COUNT,
};

enum pchg_error {
	/* Errors reported by host. */
	PCHG_ERROR_HOST,
	PCHG_ERROR_OVER_TEMPERATURE,
	PCHG_ERROR_OVER_CURRENT,
	PCHG_ERROR_FOREIGN_OBJECT,
	/* Errors reported by chip. */
	PCHG_ERROR_FW_VERSION,
	PCHG_ERROR_INVALID_FW,
	PCHG_ERROR_WRITE_FLASH,
	/* All other errors */
	PCHG_ERROR_OTHER,
};

#define PCHG_ERROR_MASK(e) BIT(e)

enum pchg_mode {
	PCHG_MODE_NORMAL = 0,
	PCHG_MODE_DOWNLOAD,
	/* Add no more entries below here. */
	PCHG_MODE_COUNT,
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
	/* Full battery percentage */
	const uint8_t full_percent;
	/* Update block size */
	const uint32_t block_size;
};

struct pchg_update {
	/* Version of new firmware. Usually used by EC_PCHG_UPDATE_CMD_OPEN. */
	uint32_t version;
	/* CRC32 of new firmware. Usually used by EC_PCHG_UPDATE_CMD_CLOSE. */
	uint32_t crc32;
	/* Address which <data> will be written to. */
	uint32_t addr;
	/* Size of <data> */
	uint32_t size;
	/* 0: No data. 1: Data is ready for write. */
	uint8_t data_ready;
	/* Partial data of new firmware */
	uint8_t data[128];
};

/**
 * Data struct describing the status of a peripheral charging port. It provides
 * the state machine and a charger driver with a context to work on.
 */
struct pchg {
	/* Static configuration */
	const struct pchg_config *const cfg;
	/* Current state of the port */
	enum pchg_state state;
	/* Event queue */
	struct queue const events;
	/* Event queue mutex */
	struct mutex mtx;
	/* 1:Pending IRQ 0:No pending IRQ */
	atomic_t irq;
	/* Event currently being handled */
	enum pchg_event event;
	/* Error (enum pchg_error). Port is disabled until it's cleared. */
	uint32_t error;
	/* Battery percentage (0% ~ 100%) of the connected peripheral device */
	uint8_t battery_percent;
	/* Number of dropped events (due to queue overflow) */
	uint32_t dropped_event_count;
	/* Number of dropped host events (due to queue overflow) */
	uint32_t dropped_host_event_count;
	/* enum pchg_mode */
	uint8_t mode;
	/* FW version */
	uint32_t fw_version;
	/* Context related to FW update */
	struct pchg_update update;
};

/**
 * Peripheral charger driver
 */
struct pchg_drv {
	/*
	 * Reset charger chip. External reset (e.g by GPIO). No
	 * communication or data access is expected (e.g. no I2C access).
	 */
	int (*reset)(struct pchg *ctx);
	/*
	 * Initialize the charger. Run setup needed only once per reset
	 * (e.g. enable I2C, unlock I2C).
	 */
	int (*init)(struct pchg *ctx);
	/* Enable/disable the charger. */
	int (*enable)(struct pchg *ctx, bool enable);
	/*
	 * Get chip info, identify chip and setup function pointers
	 * (e.g. I2C read function). It needs to work without IRQ.
	 */
	int (*get_chip_info)(struct pchg *ctx);
	/* Get event info. */
	int (*get_event)(struct pchg *ctx);
	/* Get battery level. */
	int (*get_soc)(struct pchg *ctx);
	/* open update session */
	int (*update_open)(struct pchg *ctx);
	/* write update image */
	int (*update_write)(struct pchg *ctx);
	/* close update session */
	int (*update_close)(struct pchg *ctx);
};

/**
 * Array storing configs and states of all the peripheral charging ports.
 * Should be defined in board.c.
 */
extern struct pchg pchgs[];
extern const int pchg_count;

/* Utility macro converting port config to port number. */
#define PCHG_CTX_TO_PORT(ctx) ((ctx) - &pchgs[0])

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

/**
 * Turn on/off power for a PCHG charger.
 *
 * @param port  Port number of the PCHG charger.
 * @param on
 */
__override_proto void board_pchg_power_on(int port, bool on);

#endif /* __CROS_EC_PERIPHERAL_CHARGER_H */
