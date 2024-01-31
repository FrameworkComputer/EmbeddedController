/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Realtek RTS545x Power Delivery Controller Driver
 */

#include <assert.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/smbus.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/smf.h>
LOG_MODULE_REGISTER(pdc_rts54, LOG_LEVEL_INF);
#include "usbc/utils.h"

#include <drivers/pdc.h>

#define DT_DRV_COMPAT realtek_rts54_pdc

#define BYTE0(n) ((n) & 0xff)
#define BYTE1(n) (((n) >> 8) & 0xff)
#define BYTE2(n) (((n) >> 16) & 0xff)
#define BYTE3(n) (((n) >> 24) & 0xff)

/**
 * @brief Time before sending a ping status
 */
#define T_PING_STATUS 10

/**
 * @brief Number of times to try an I2C transaction
 */
#define N_I2C_TRANSACTION_COUNT 10

/**
 * @brief Number of times to send a ping status
 */
#define N_RETRY_COUNT 200

/**
 * @brief VBUS Voltage Scale Factor is 50mV
 */
#define VOLTAGE_SCALE_FACTOR 50

/**
 * @brief FORCE_SET_POWER_SWITCH enable
 *	  Bits [0:1] 00 VBSIN_EN off
 *	             11 VBSIN_EN on
 *	  Bits [2:5] Set to 0
 *	  Bits [6]   VBSIN_EN control: set to 1
 *	  Bits [7]   Set to 0
 */
#define VBSIN_EN_ON 0x43
#define VBSIN_EN_OFF 0x40

/**
 * @brief Offsets of data fields in the GET_IC_STATUS response
 */
#define RTS54XX_GET_IC_STATUS_FWVER_MAJOR_OFFSET (4)
#define RTS54XX_GET_IC_STATUS_FWVER_MINOR_OFFSET (5)
#define RTS54XX_GET_IC_STATUS_FWVER_PATCH_OFFSET (6)

/**
 * @brief Macro to transition to init or idle state and return
 */
#define TRANSITION_TO_INIT_OR_IDLE_STATE(data)  \
	transition_to_init_or_idle_state(data); \
	return

/**
 * @brief Number of RTS54XX ports detected
 */
#define NUM_PDC_RTS54XX_PORTS DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

/* TODO: b/323371550 */
BUILD_ASSERT(NUM_PDC_RTS54XX_PORTS <= 2,
	     "rts54xx driver supports a maximum of 2 ports");

/**
 * @brief SMbus Command struct for Realtek commands
 */
struct smbus_cmd_t {
	/* Command */
	uint8_t cmd;
	/* Number of bytes to write */
	uint8_t len;
	/* Sub-Command */
	uint8_t sub;
};

/** @brief Realtek SMbus commands */

const struct smbus_cmd_t VENDOR_CMD_ENABLE = { 0x01, 0x03, 0xDA };
const struct smbus_cmd_t SET_NOTIFICATION_ENABLE = { 0x08, 0x06, 0x01 };
const struct smbus_cmd_t SET_PDOS = { 0x08, 0x03, 0x03 };
const struct smbus_cmd_t SET_RDO = { 0x08, 0x06, 0x04 };
const struct smbus_cmd_t SET_TPC_RP = { 0x08, 0x03, 0x05 };
const struct smbus_cmd_t SET_TPC_CSD_OPERATION_MODE = { 0x08, 0x03, 0x1D };
const struct smbus_cmd_t SET_TPC_RECONNECT = { 0x08, 0x03, 0x1F };
const struct smbus_cmd_t FORCE_SET_POWER_SWITCH = { 0x08, 0x03, 0x21 };
const struct smbus_cmd_t GET_PDOS = { 0x08, 0x03, 0x83 };
const struct smbus_cmd_t GET_RDO = { 0x08, 0x02, 0x84 };
const struct smbus_cmd_t GET_CURRENT_PARTNER_SRC_PDO = { 0x08, 0x02, 0xA7 };
const struct smbus_cmd_t GET_POWER_SWITCH_STATE = { 0x08, 0x02, 0xA9 };
const struct smbus_cmd_t GET_RTK_STATUS = { 0x09, 0x03, 0x00 };
const struct smbus_cmd_t PPM_RESET = { 0x0E, 0x02, 0x01 };
const struct smbus_cmd_t CONNECTOR_RESET = { 0x0E, 0x03, 0x03 };
const struct smbus_cmd_t GET_CAPABILITY = { 0x0E, 0x02, 0x06 };
const struct smbus_cmd_t GET_CONNECTOR_CAPABILITY = { 0x0E, 0x02, 0x07 };
const struct smbus_cmd_t SET_UOR = { 0x0E, 0x03, 0x09 };
const struct smbus_cmd_t SET_PDR = { 0x0E, 0x03, 0x0B };
const struct smbus_cmd_t UCSI_GET_ERROR_STATUS = { 0x0E, 0x02, 0x13 };
const struct smbus_cmd_t UCSI_READ_POWER_LEVEL = { 0x0E, 0x03, 0x1E };
const struct smbus_cmd_t GET_IC_STATUS = { 0x3A, 0x03, 0x00 };

/**
 * @brief PDC Command states
 */
enum cmd_sts_t {
	/** Command has not been started */
	CMD_BUSY = 0,
	/** Command has completed */
	CMD_DONE = 1,
	/** Command has been started but has not completed */
	CMD_DEFERRED = 2,
	/** Command completed with error. Send GET_ERROR_STATUS for details */
	CMD_ERROR = 3
};

/**
 * @brief Ping Status of the PDC
 */
union ping_status_t {
	struct {
		/** Command status */
		uint8_t cmd_sts : 2;
		/** Length of data read to read */
		uint8_t data_len : 6;
	};
	uint8_t raw_value;
};

/**
 * @brief States of the main state machine
 */
enum state_t {
	/** Init State */
	ST_INIT,
	/** Idle State */
	ST_IDLE,
	/** Write State */
	ST_WRITE,
	/** Ping State */
	ST_PING_STATUS,
	/** Read State */
	ST_READ,
	/** Interrupt State */
	ST_IRQ
};

/**
 * @brief Init sub-states
 */
enum init_state_t {
	/** Enable the PDC */
	INIT_PDC_ENABLE,
	/** Get the PDC IC Status */
	INIT_PDC_GET_IC_STATUS,
	/** Set the PDC Notifications */
	INIT_PDC_SET_NOTIFICATION_ENABLE,
	/** Reset the PDC */
	INIT_PDC_RESET,
	/** Initialization complete */
	INIT_PDC_COMPLETE
};

/**
 * @brief PDC commands
 */
enum cmd_t {
	/** No command */
	CMD_NONE,
	/** CMD_TRIGGER_PDC_RESET */
	CMD_TRIGGER_PDC_RESET,
	/** PDC Enable */
	CMD_VENDOR_ENABLE,
	/** Set Notification Enable */
	CMD_SET_NOTIFICATION_ENABLE,
	/** PDC Reset */
	CMD_PPM_RESET,
	/** Connector Reset */
	CMD_CONNECTOR_RESET,
	/** Get Capability */
	CMD_GET_CAPABILITY,
	/** Get Connector Capability */
	CMD_GET_CONNECTOR_CAPABILITY,
	/** Set UOR */
	CMD_SET_UOR,
	/** Set PDR */
	CMD_SET_PDR,
	/** Get PDOs */
	CMD_GET_PDOS,
	/** Get Connector Status */
	CMD_GET_CONNECTOR_STATUS,
	/** Get Error Status */
	CMD_GET_ERROR_STATUS,
	/** Get VBUS Voltage */
	CMD_GET_VBUS_VOLTAGE,
	/** Get IC Status */
	CMD_GET_IC_STATUS,
	/** Set CCOM */
	CMD_SET_CCOM,
	/** Read Power Level */
	CMD_READ_POWER_LEVEL,
	/** Get RDO */
	CMD_GET_RDO,
	/** Set RDO */
	CMD_SET_RDO,
	/** Set Sink Path */
	CMD_SET_SINK_PATH,
	/** Get current Partner SRC PDO */
	CMD_GET_CURRENT_PARTNER_SRC_PDO,
	/** Set the Rp TypeC current */
	CMD_SET_TPC_RP,
	/** TypeC reconnect */
	CMD_SET_TPC_RECONNECT,
};

/**
 * @brief PDC Config object
 */
struct pdc_config_t {
	/** I2C config */
	struct i2c_dt_spec i2c;
	/** pdc power path interrupt */
	struct gpio_dt_spec irq_gpios;
	/** connector number of this port */
	uint8_t connector_number;
	/** Create thread function */
	void (*create_thread)(const struct device *dev);
};

/**
 * @brief PDC Data object
 */
struct pdc_data_t {
	/** State machine context */
	struct smf_ctx ctx;
	/** Init's local state variable */
	enum init_state_t init_local_state;
	/** PDC's last state */
	enum state_t last_state;
	/** PDC device structure */
	const struct device *dev;
	/** PDC command */
	enum cmd_t cmd;
	/** Driver thread */
	k_tid_t thread;
	/** Driver thread's data */
	struct k_thread thread_data;
	/** Ping status */
	union ping_status_t ping_status;
	/** Ping status retry counter */
	uint8_t ping_retry_counter;
	/** I2C retry counter */
	uint8_t i2c_transaction_retry_counter;
	/** PDC write buffer */
	uint8_t wr_buf[PDC_MAX_DATA_LENGTH];
	/** Length of bytes in the write buffer */
	uint8_t wr_buf_len;
	/** PDC read buffer */
	uint8_t rd_buf[PDC_MAX_DATA_LENGTH];
	/** Length of bytes in the read buffer */
	uint8_t rd_buf_len;
	/** Pointer to user data */
	uint8_t *user_buf;
	/** Command mutex */
	struct k_mutex mtx;
	/** Interrupt workqueue */
	struct k_work work;
	/** GPIO interrupt callback */
	struct gpio_callback gpio_cb;
	/** Error status */
	union error_status_t error_status;
	/** CCI Event */
	union cci_event_t cci_event;
	/** CCI Event callback */
	pdc_cci_handler_cb_t cci_cb;
	/** CCI Event callback data */
	void *cb_data;
	/** Information about the PDC */
	struct pdc_info_t info;
	/** Init done flag */
	bool init_done;
};

/**
 * @brief Name of the command, used for debugging
 */
static const char *const cmd_names[] = {
	[CMD_NONE] = "",
	[CMD_TRIGGER_PDC_RESET] = "TRIGGER_PDC_RESET",
	[CMD_VENDOR_ENABLE] = "VENDOR_ENABLE",
	[CMD_SET_NOTIFICATION_ENABLE] = "SET_NOTIFICATION_ENABLE",
	[CMD_PPM_RESET] = "PPM_RESET",
	[CMD_CONNECTOR_RESET] = "CONNECTOR_RESET",
	[CMD_GET_CAPABILITY] = "GET_CAPABILITY",
	[CMD_GET_CONNECTOR_CAPABILITY] = "GET_CONNECTOR_CAPABILITY",
	[CMD_SET_UOR] = "SET_UOR",
	[CMD_SET_PDR] = "SET_PDR",
	[CMD_GET_PDOS] = "GET_PDOS",
	[CMD_GET_CONNECTOR_STATUS] = "GET_CONNECTOR_STATUS",
	[CMD_GET_ERROR_STATUS] = "GET_ERROR_STATUS",
	[CMD_GET_VBUS_VOLTAGE] = "GET_VBUS_VOLTAGE",
	[CMD_GET_IC_STATUS] = "GET_IC_STATUS",
	[CMD_SET_CCOM] = "SET_CCOM",
	[CMD_SET_SINK_PATH] = "SET_SINK_PATH",
	[CMD_READ_POWER_LEVEL] = "READ_POWER_LEVEL",
	[CMD_GET_RDO] = "GET_RDO",
	[CMD_SET_TPC_RP] = "SET_TPC_RP",
	[CMD_SET_TPC_RECONNECT] = "SET_TPC_RECONNECT",
	[CMD_SET_RDO] = "SET_RDO",
	[CMD_GET_CURRENT_PARTNER_SRC_PDO] = "GET_CURRENT_PARTNER_SRC_PDO",
};

/**
 * @brief List of human readable state names for console debugging
 */
static const char *const state_names[] = {
	[ST_INIT] = "INIT",   [ST_IDLE] = "IDLE",
	[ST_WRITE] = "WRITE", [ST_PING_STATUS] = "PING_STATUS",
	[ST_READ] = "READ",   [ST_IRQ] = "IRQ",
};

static const struct device *irq_shared_port;
static int irq_share_pin;
static bool irq_init_done;
static volatile bool irq_pending;
static const struct smf_state states[];
static int rts54_enable(const struct device *dev);
static int rts54_reset(const struct device *dev);
static int rts54_set_notification_enable(const struct device *dev,
					 union notification_enable_t bits,
					 uint16_t ext_bits);
static int rts54_get_info(const struct device *dev, struct pdc_info_t *info);

/**
 * @brief PDC port data used in interrupt handler
 */
static struct pdc_data_t *pdc_data[NUM_PDC_RTS54XX_PORTS];

static enum state_t get_state(struct pdc_data_t *data)
{
	return data->ctx.current - &states[0];
}

static void set_state(struct pdc_data_t *data, const enum state_t next_state)
{
	data->last_state = get_state(data);
	smf_set_state(SMF_CTX(data), &states[next_state]);
}

static void print_current_state(struct pdc_data_t *data)
{
	int st = get_state(data);

	if (st == ST_WRITE) {
		LOG_INF("ST: %s %s", state_names[st], cmd_names[data->cmd]);
	} else {
		LOG_INF("ST: %s", state_names[get_state(data)]);
	}
}

static void call_cci_event_cb(struct pdc_data_t *data)
{
	if (!data->init_done) {
		return;
	}

	if (data->cci_cb) {
		LOG_INF("cci_event_cb event=0x%x", data->cci_event.raw_value);
		data->cci_cb(data->cci_event, data->cb_data);
	}
}

static int get_ara(const struct device *dev, uint8_t *ara)
{
	const struct pdc_config_t *cfg = dev->config;

	return i2c_read(cfg->i2c.bus, ara, 1, SMBUS_ADDRESS_ARA);
}

static void perform_pdc_init(struct pdc_data_t *data)
{
	/* Set initial local state of Init */
	data->init_local_state = INIT_PDC_ENABLE;
	set_state(data, ST_INIT);
}

/**
 * @brief This function performs a state change, so a return should
 * be placed after its immediate call.
 */
static void transition_to_init_or_idle_state(struct pdc_data_t *data)
{
	if (data->init_done) {
		set_state(data, ST_IDLE);
	} else {
		set_state(data, ST_INIT);
	}
}

static int get_ping_status(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;
	const struct pdc_config_t *cfg = dev->config;
	struct i2c_msg msg;

	msg.buf = &data->ping_status.raw_value;
	msg.len = 1;
	msg.flags = I2C_MSG_READ | I2C_MSG_STOP;

	return i2c_transfer_dt(&cfg->i2c, &msg, 1);
}

static int rts54_i2c_read(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;
	const struct pdc_config_t *cfg = dev->config;
	struct i2c_msg msg[2];
	uint8_t cmd = 0x80;
	int rv;

	msg[0].buf = &cmd;
	msg[0].len = 1;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = data->rd_buf;
	msg[1].len = data->ping_status.data_len + 1;
	msg[1].flags = I2C_MSG_READ | I2C_MSG_STOP;

	rv = i2c_transfer_dt(&cfg->i2c, msg, 2);
	if (rv < 0) {
		return rv;
	}

	data->rd_buf_len = data->ping_status.data_len;

	return rv;
}

static int rts54_i2c_write(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;
	const struct pdc_config_t *cfg = dev->config;
	struct i2c_msg msg;

	msg.buf = data->wr_buf;
	msg.len = data->wr_buf_len;
	msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer_dt(&cfg->i2c, &msg, 1);
}

static void st_init_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);
	data->init_done = false;
	data->cmd = CMD_NONE;
}

static void st_init_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	union notification_enable_t bits;

	switch (data->init_local_state) {
	case INIT_PDC_ENABLE:
		rts54_enable(data->dev);
		data->init_local_state = INIT_PDC_GET_IC_STATUS;
		break;
	case INIT_PDC_GET_IC_STATUS:
		rts54_get_info(data->dev, &data->info);
		data->init_local_state = INIT_PDC_SET_NOTIFICATION_ENABLE;
		break;
	case INIT_PDC_SET_NOTIFICATION_ENABLE:
		bits.raw_value = 0xDBE7; /* TODO: Read from device tree */
		rts54_set_notification_enable(data->dev, bits, 0);
		data->init_local_state = INIT_PDC_RESET;
		break;
	case INIT_PDC_RESET:
		rts54_reset(data->dev);
		if (data->cci_event.reset_completed) {
			data->init_local_state = INIT_PDC_COMPLETE;
		}
		break;
	case INIT_PDC_COMPLETE:
		/* Init is complete, so transition to Idle state */
		set_state(data, ST_IDLE);
		data->init_done = true;
		return;
	}

	set_state(data, ST_WRITE);
}

/**
 * @brief Each port has its own IRQ. When an interrupt is pending, one
 * of the ST_IDLE states will execute and handle all pending interrupts for all
 * ports.
 */
static void handle_irqs(struct pdc_data_t *data)
{
	uint8_t ara;
	int rv;

	/* Clear pending bit, so other thread won't attempt to handle an irq */
	irq_pending = false;

	for (int i = 0; i < NUM_PDC_RTS54XX_PORTS; i++) {
		/*
		 * Read the Alert Response Address to determine
		 * which port generated the interrupt.
		 */
		rv = get_ara(data->dev, &ara);
		if (rv) {
			return;
		}

		/* Search for port with matching I2C address */
		for (int j = 0; j < CONFIG_USB_PD_PORT_MAX_COUNT; j++) {
			struct pdc_data_t *pdc_int_data = pdc_data[j];
			const struct pdc_config_t *cfg =
				pdc_int_data->dev->config;

			if ((ara >> 1) == cfg->i2c.addr) {
				LOG_INF("C%d: IRQ", cfg->connector_number);

				/* Found pending interrupt, handle it */
				/* Inform subsystem of the interrupt */
				/* Clear the CCI Event */
				pdc_int_data->cci_event.raw_value = 0;
				/* Set the port the CCI Event occurred
				 * on */
				pdc_int_data->cci_event.connector_change =
					cfg->connector_number;
				/* Set the interrupt event */
				pdc_int_data->cci_event
					.vendor_defined_indicator = 1;
				/* Notify system of status change */
				call_cci_event_cb(pdc_int_data);
				/* done with this port */
				break;
			}
		}
	}
}

static void st_idle_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);

	data->cmd = CMD_NONE;
}

static void st_idle_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	/*
	 * Priority of events:
	 *  1: CMD_TRIGGER_PDC_RESET
	 *  2: Interrupt
	 *  3: Non-Reset command
	 */
	if (data->cmd == CMD_TRIGGER_PDC_RESET) {
		perform_pdc_init(data);
	} else if (irq_pending) {
		handle_irqs(data);
	} else if (data->cmd != CMD_NONE) {
		set_state(data, ST_WRITE);
	}
}

static void st_write_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	const struct pdc_config_t *cfg = data->dev->config;

	print_current_state(data);

	/* This state can only be entered from the Init and Idle states */
	assert(data->last_state == ST_INIT || data->last_state == ST_IDLE);

	/* Clear I2C transaction retry counter */
	data->i2c_transaction_retry_counter = 0;
	/* Clear the Error Status */
	data->error_status.raw_value = 0;
	/* Clear the CCI Event */
	data->cci_event.raw_value = 0;
	/* Set the port the CCI Event occurred on */
	data->cci_event.connector_change = cfg->connector_number;
}

static void st_write_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	int rv;

	/* Write the command */
	rv = rts54_i2c_write(data->dev);
	if (rv < 0) {
		/* I2C write failed */
		data->i2c_transaction_retry_counter++;
		if (data->i2c_transaction_retry_counter >
		    N_I2C_TRANSACTION_COUNT) {
			/* MAX I2C transactions exceeded */

			/*
			 * The command was not successfully completed,
			 * so set cci.error to 1b.
			 */
			data->cci_event.error = 1;
			data->cci_event.command_completed = 1;
			data->error_status.i2c_write_error = 1;
			/* Notify system of status change */
			call_cci_event_cb(data);

			/*
			 * This state can only be entered from the Init or
			 * Idle state, so return to one of them.
			 */
			TRANSITION_TO_INIT_OR_IDLE_STATE(data);
		}
		return;
	}

	/* I2C transaction succeeded */
	set_state(data, ST_PING_STATUS);
}

static void st_ping_status_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	const struct pdc_config_t *cfg = data->dev->config;

	print_current_state(data);

	/* This state can only be entered from the Write Status state */
	assert(data->last_state == ST_WRITE);

	/* Clear I2c Transaction Retry Counter */
	data->i2c_transaction_retry_counter = 0;
	/* Clear Ping Rety Counter */
	data->ping_retry_counter = 0;
	/* Clear Ping Status */
	data->ping_status.raw_value = 0;
	/* Clear the Error Status */
	data->error_status.raw_value = 0;
	/* Clear the CCI Event */
	data->cci_event.raw_value = 0;
	/* Set the port the CCI Event occurred on */
	data->cci_event.connector_change = cfg->connector_number;
}

static void st_ping_status_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	int rv;

	/* Read the Ping Status */
	rv = get_ping_status(data->dev);
	if (rv < 0) {
		/* I2C transaction failed */
		data->i2c_transaction_retry_counter++;
		if (data->i2c_transaction_retry_counter >
		    N_I2C_TRANSACTION_COUNT) {
			/* MAX I2C transactions exceeded */
			/*
			 * The command was not successfully completed,
			 * so set cci.error to 1b.
			 */
			data->cci_event.error = 1;
			/* Command has completed */
			data->cci_event.command_completed = 1;
			/* Set error, I2C read error */
			data->error_status.i2c_read_error = 1;
			/* Notify system of status change */
			call_cci_event_cb(data);

			/* An error occurred, return to idle state */
			TRANSITION_TO_INIT_OR_IDLE_STATE(data);
		}
		return;
	}

	switch (data->ping_status.cmd_sts) {
	case CMD_BUSY:
		/*
		 * Busy and Deferred and handled the same,
		 * so fall through
		 */
	case CMD_DEFERRED:
		/*
		 * The command has not been processed.
		 * Stay here and resend get ping status.
		 */
		data->ping_retry_counter++;
		if (data->ping_retry_counter > N_RETRY_COUNT) {
			/* MAX Ping Retries exceeded */
			/*
			 * The command was not successfully completed,
			 * so set cci.error to 1b.
			 */
			data->cci_event.error = 1;
			/* Command completed */
			data->cci_event.command_completed = 1;
			/* Ping Retry Count error */
			data->error_status.ping_retry_count = 1;

			/* Notify system of status change */
			call_cci_event_cb(data);

			/* An error occurred, return to idle state */
			TRANSITION_TO_INIT_OR_IDLE_STATE(data);
		} else {
			/*
			 * If Busy, then set this cci.busy to a 1b
			 * and all other fields to zero.
			 */
			if (data->cci_event.busy == 0) {
				/* Only notify subsystem of busy event once */
				data->cci_event.busy = 1;

				/* Notify system of status change */
				call_cci_event_cb(data);
			}
		}
		break;
	case CMD_DONE:
		if (data->cmd == CMD_PPM_RESET) {
			/* The PDC has been reset,
			 * so set cci.reset_completed to 1b.
			 */
			data->cci_event.reset_completed = 1;
			/* Notify system of status change */
			call_cci_event_cb(data);
			LOG_DBG("Realtek PDC reset complete");
			/* All done, return to Init or Idle state */
			TRANSITION_TO_INIT_OR_IDLE_STATE(data);
		} else {
			LOG_DBG("ping_status: %02x",
				data->ping_status.raw_value);

			/*
			 * The command completed successfully,
			 * so set cci.command_completed to 1b.
			 */
			data->cci_event.command_completed = 1;

			if (data->ping_status.data_len > 0) {
				/* Data is available, so read it */
				set_state(data, ST_READ);
			} else {
				/* Inform the system of the event */
				call_cci_event_cb(data);

				/* Return to Idle or Init state */
				TRANSITION_TO_INIT_OR_IDLE_STATE(data);
			}
		}
		break;
	case CMD_ERROR:
		/*
		 * The command was not successfully completed,
		 * so set cci.error to 1b.
		 */
		data->cci_event.error = 1;
		/* Command completed */
		data->cci_event.command_completed = 1;

		/* Notify system of status change */
		call_cci_event_cb(data);

		/* An error occurred, return to idle state */
		TRANSITION_TO_INIT_OR_IDLE_STATE(data);
		break;
	}
}

static void st_read_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	const struct pdc_config_t *cfg = data->dev->config;

	print_current_state(data);

	/* This state can only be entered from the Ping Status state */
	assert(data->last_state == ST_PING_STATUS);

	/* Clear the Error Status */
	data->error_status.raw_value = 0;
	/* Clear the CCI Event */
	data->cci_event.raw_value = 0;
	/* Set the port the CCI Event occurred on */
	data->cci_event.connector_change = cfg->connector_number;
}

static void st_read_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	uint8_t offset;
	uint8_t len;
	int rv;

	rv = rts54_i2c_read(data->dev);
	if (rv < 0) {
		/*
		 * The command was not successfully completed,
		 * so set cci.error to 1b.
		 */
		data->cci_event.error = 1;
		/* Command completed */
		data->cci_event.command_completed = 1;
		/* I2C read error */
		data->error_status.i2c_read_error = 1;
		/* Notify system of status change */
		call_cci_event_cb(data);

		/* An error occurred, return to idle state */
		TRANSITION_TO_INIT_OR_IDLE_STATE(data);
	}

	/* Get length of data returned */
	len = data->rd_buf[0];

	/* Skip over length byte */
	offset = 1;

	/* Copy the received data to the user's buffer */
	switch (data->cmd) {
	case CMD_GET_IC_STATUS: {
		struct pdc_info_t *info = (struct pdc_info_t *)data->user_buf;

		/* Realtek Is running flash code: Byte 1 */
		info->is_running_flash_code = data->rd_buf[1];

		/* Realtek FW main version: Byte4, Byte5, Byte6 */
		info->fw_version =
			data->rd_buf[RTS54XX_GET_IC_STATUS_FWVER_MAJOR_OFFSET]
				<< 16 |
			data->rd_buf[RTS54XX_GET_IC_STATUS_FWVER_MINOR_OFFSET]
				<< 8 |
			data->rd_buf[RTS54XX_GET_IC_STATUS_FWVER_PATCH_OFFSET];

		/* Realtek VID PID: Byte10, Byte11, Byte12, Byte13
		 * (little-endian) */
		info->vid_pid = data->rd_buf[11] << 24 |
				data->rd_buf[10] << 16 | data->rd_buf[13] << 8 |
				data->rd_buf[12];

		/* Realtek Running flash bank offset: Byte15 */
		info->running_in_flash_bank = data->rd_buf[15];

		/* Realtek PD Revision: Byte23, Byte24 (big-endian) */
		info->pd_revision = data->rd_buf[23] << 8 | data->rd_buf[24];

		/* Realtek PD Version: Byte25, Byte26 (big-endian) */
		info->pd_version = data->rd_buf[25] << 8 | data->rd_buf[26];

		/* Only print this log on init */
		if (data->init_local_state != INIT_PDC_COMPLETE) {
			LOG_INF("Realtek: FW Version: %04x", info->fw_version);
			LOG_INF("Realtek: PD Version: %04x, Rev %04x",
				info->pd_version, info->pd_revision);
		}
		break;
	}
	case CMD_GET_VBUS_VOLTAGE:
		/*
		 * Realtek Voltage reading is on Byte16 and Byte17, but
		 * the READ_RTK_STATUS command was issued with reading
		 * 2-bytes from offset 16, so the data is read from
		 * rd_buf at Byte1 and Byte2.
		 */
		*(uint16_t *)data->user_buf =
			((data->rd_buf[2] << 8) | data->rd_buf[1]) *
			VOLTAGE_SCALE_FACTOR;
		break;
	case CMD_GET_CONNECTOR_STATUS: {
		/* Map Realtek GET_RTK_STATUS bits to UCSI GET_CONNECTOR_STATUS
		 */
		struct connector_status_t *cs =
			(struct connector_status_t *)data->user_buf;

		/*
		 * NOTE: Realtek sets an additional 16-bits of status_change
		 *       events in bytes 3 and 4, but they are not part of the
		 *       UCSI spec, so are ignored.
		 */
		cs->conn_status_change_bits.raw_value = data->rd_buf[2] << 8 |
							data->rd_buf[1];
		/* ignore data->rd_buf[3] */
		/* ignore data->rd_buf[4] */

		/* Realtek Port Operation Mode: Byte5, Bit1:3 */
		cs->power_operation_mode = ((data->rd_buf[5] >> 1) & 7);

		/* Realtek Connection Status: Byte5, Bit7 */
		cs->connect_status = ((data->rd_buf[5] >> 7) & 1);

		/* Realtek Power Direction: Byte5, Bit6 */
		cs->power_direction = ((data->rd_buf[5] >> 6) & 1);

		/* Realtek Connector Partner Flags: Byte6, Bit0:7 */
		cs->conn_partner_flags = data->rd_buf[6];

		/* Realtek Connector Partner Type: Byte11, Bit0:2 */
		cs->conn_partner_type = (data->rd_buf[11] & 7);

		/* Realtek RDO: Bytes [7:10] */
		cs->rdo = data->rd_buf[10] << 24 | data->rd_buf[9] << 16 |
			  data->rd_buf[8] << 8 | data->rd_buf[7];

		/* Realtek Battery Charging Capability Status, Byte 11, Bit3:4
		 */
		cs->battery_charging_cap = 0; /* NOTE: Not set in this register
						 by Realtek */
		cs->provider_caps_limited = 0; /* NOTE: Not set in this register
						  by Realtek */

		/* Realtek bcdPDVersion Operation Mode, Byte13, Bit6:7 */
		cs->bcd_pd_version = ((((data->rd_buf[13] >> 6) & 3) + 1) << 8);

		/* Realtek Plug Direction, Byte 12, Bit5 */
		cs->orientation = ((data->rd_buf[12] >> 5) & 1);

		/* Realtek VBSIN_EN switch status, Byte 13, Bit0:1 */
		cs->sink_path_status = (((data->rd_buf[13]) & 3) == 3);

		/* Not Set by Realtek */
		cs->reverse_current_protection_status = 0;
		cs->power_reading_ready = 0;
		cs->current_scale = 0;
		cs->peak_current = 0;
		cs->average_current = 0;

		/* Realtek voltage scale is 1010b - 50mV */
		cs->voltage_scale = 0xa;

		/* Realtek Voltage Reading Byte 17 (low byte) and Byte 18 (high
		 * byte) */
		cs->voltage_reading = data->rd_buf[18] << 8 | data->rd_buf[17];
		break;
	}
	case CMD_GET_ERROR_STATUS: {
		/* Map Realtek GET_ERROR_STATUS bits to UCSI GET_ERROR_STATUS */
		union error_status_t *es =
			(union error_status_t *)data->user_buf;

		/* Realtek Unrecognized command, Byte 1, Bit0 */
		es->unrecognized_command = (data->rd_buf[1] & 1);
		/* Realtek Non Existent Connector Number, Byte 1, Bit1 */
		es->non_existent_connector_number =
			((data->rd_buf[1] >> 1) & 1);
		/* Realtek Invalid Command Specific Parameter, Byte 1, Bit2 */
		es->invalid_command_specific_param =
			((data->rd_buf[1] >> 2) & 1);
		/* Realtek Incompatible Connector Partner, Byte 1, Bit3 */
		es->incompatible_connector_partner =
			((data->rd_buf[1] >> 3) & 1);
		/* Realtek CC Communication Error, Byte 1, Bit4 */
		es->cc_communication_error = ((data->rd_buf[1] >> 4) & 1);
		/* Realtek Command unsuccessful Dead Battery condition, Byte 1,
		 * Bit 5 */
		es->cmd_unsuccessful_dead_batt = ((data->rd_buf[1] >> 5) & 1);
		/* Realtek Contract negotiation fail, Byte 1, Bit6 */
		es->contract_negotiation_failed = ((data->rd_buf[1] >> 6) & 1);

		/* Not Set by Realtek */
		es->overcurrent = 0;
		es->undefined = 0;
		es->port_partner_rejected_swap = 0;
		es->hard_reset = 0;
		es->ppm_policy_conflict = 0;
		es->swap_rejected = 0;
		es->reverse_current_protection = 0;

		/*
		 * NOTE: Vendor Specific Error were already set in previous
		 * states
		 */
		break;
	}
	default:
		/* No preprocessing needed for the user data */
		memcpy(data->user_buf, data->rd_buf + offset, len);
	}

	/* Clear the read buffer */
	memset(data->rd_buf, 0, 256);

	/*
	 * Set cci.data_len. This will be zero if no
	 * data is available.
	 */
	data->cci_event.data_len = len;
	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);
	/* All done, return to Init or Idle state */
	TRANSITION_TO_INIT_OR_IDLE_STATE(data);
}

/* Populate cmd state table */
static const struct smf_state states[] = {
	[ST_INIT] = SMF_CREATE_STATE(st_init_entry, st_init_run, NULL, NULL),
	[ST_IDLE] = SMF_CREATE_STATE(st_idle_entry, st_idle_run, NULL, NULL),
	[ST_WRITE] = SMF_CREATE_STATE(st_write_entry, st_write_run, NULL, NULL),
	[ST_PING_STATUS] = SMF_CREATE_STATE(st_ping_status_entry,
					    st_ping_status_run, NULL, NULL),
	[ST_READ] = SMF_CREATE_STATE(st_read_entry, st_read_run, NULL, NULL),
};

/**
 * @brief Helper method for setting up a command call.
 * @param dev PDC device pointer
 * @param cmd Command to execute
 * @param buf Command payload to copy into write buffer
 * @param len Length of paylaod buffer
 * @param user_buf Pointer to buffer where response data will be written.
 * @return 0 on success, -EBUSY if command is already pending.
 */
static int rts54_post_command(const struct device *dev, enum cmd_t cmd,
			      const uint8_t *buf, uint8_t len,
			      uint8_t *user_buf)
{
	struct pdc_data_t *data = dev->data;

	k_mutex_lock(&data->mtx, K_FOREVER);

	if (data->cmd != CMD_NONE) {
		k_mutex_unlock(&data->mtx);
		return -EBUSY;
	}

	if (buf) {
		assert(len <= ARRAY_SIZE(data->wr_buf));
		memcpy(data->wr_buf, buf, len);
	}

	data->wr_buf_len = len;
	data->user_buf = user_buf;
	data->cmd = cmd;

	k_mutex_unlock(&data->mtx);

	return 0;
}

static int rts54_get_rtk_status(const struct device *dev, uint8_t offset,
				uint8_t len, enum cmd_t cmd, uint8_t *buf)
{
	if (buf == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		GET_RTK_STATUS.cmd,
		GET_RTK_STATUS.len,
		GET_RTK_STATUS.sub + offset,
		0x00,
		len,
	};

	return rts54_post_command(dev, cmd, payload, ARRAY_SIZE(payload), buf);
}

static int rts54_get_ucsi_version(const struct device *dev, uint16_t *version)
{
	if (version == NULL) {
		return -EINVAL;
	}

	*version = UCSI_VERSION;

	return 0;
}

static int rts54_set_handler_cb(const struct device *dev,
				pdc_cci_handler_cb_t cci_cb, void *cb_data)
{
	struct pdc_data_t *data = dev->data;

	data->cci_cb = cci_cb;
	data->cb_data = cb_data;

	return 0;
}

static int rts54_enable(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	/* Can only be called from Init State */
	if (get_state(data) != ST_INIT) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		VENDOR_CMD_ENABLE.cmd,
		VENDOR_CMD_ENABLE.len,
		VENDOR_CMD_ENABLE.sub,
		0x0b,
		0x01,
	};

	return rts54_post_command(dev, CMD_VENDOR_ENABLE, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_read_power_level(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		UCSI_READ_POWER_LEVEL.cmd,
		UCSI_READ_POWER_LEVEL.len,
		UCSI_READ_POWER_LEVEL.sub,
		0x00,
		0x00,
	};

	return rts54_post_command(dev, CMD_READ_POWER_LEVEL, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_reconnect(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		SET_TPC_RECONNECT.cmd,
		SET_TPC_RECONNECT.len,
		SET_TPC_RECONNECT.sub,
		0x00,
		0x01,
	};

	return rts54_post_command(dev, CMD_SET_TPC_RECONNECT, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_pdc_reset(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	return rts54_post_command(dev, CMD_TRIGGER_PDC_RESET, NULL, 0, NULL);
}

static int rts54_reset(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	/* Can only be called from Init State */
	if (get_state(data) != ST_INIT) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		PPM_RESET.cmd,
		PPM_RESET.len,
		PPM_RESET.sub,
		0x00,
	};

	return rts54_post_command(dev, CMD_PPM_RESET, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_connector_reset(const struct device *dev,
				 enum connector_reset_t type)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		CONNECTOR_RESET.cmd,
		CONNECTOR_RESET.len,
		CONNECTOR_RESET.sub,
		0x00,
		type,
	};

	return rts54_post_command(dev, CMD_CONNECTOR_RESET, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_set_power_level(const struct device *dev,
				 enum usb_typec_current_t tcc)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte = 0;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* Map UCSI USB Type-C current to Realtek format */
	switch (tcc) {
	case TC_CURRENT_PPM_DEFINED:
		/* Realtek does not support this */
		return -EINVAL;
	case TC_CURRENT_3_0A:
		byte = 0x03 << 2;
		break;
	case TC_CURRENT_1_5A:
		byte = 0x02 << 2;
		break;
	case TC_CURRENT_USB_DEFAULT:
		byte = 0x01 << 2;
		break;
	}

	uint8_t payload[] = {
		SET_TPC_RP.cmd, SET_TPC_RP.len, SET_TPC_RP.sub, 0x00, byte,
	};

	return rts54_post_command(dev, CMD_SET_TPC_RP, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_set_sink_path(const struct device *dev, bool en)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (en) {
		byte = VBSIN_EN_ON;
	} else {
		byte = VBSIN_EN_OFF;
	}

	uint8_t payload[] = {
		FORCE_SET_POWER_SWITCH.cmd,
		FORCE_SET_POWER_SWITCH.len,
		FORCE_SET_POWER_SWITCH.sub,
		0x00,
		byte,
	};

	return rts54_post_command(dev, CMD_SET_SINK_PATH, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_set_notification_enable(const struct device *dev,
					 union notification_enable_t bits,
					 uint16_t ext_bits)
{
	struct pdc_data_t *data = dev->data;

	/* Can only be called from Init State */
	if (get_state(data) != ST_INIT) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		SET_NOTIFICATION_ENABLE.cmd,
		SET_NOTIFICATION_ENABLE.len,
		SET_NOTIFICATION_ENABLE.sub,
		0x00,
		BYTE0(bits.raw_value),
		BYTE1(bits.raw_value),
		BYTE0(ext_bits),
		BYTE1(ext_bits),
	};

	return rts54_post_command(dev, CMD_SET_NOTIFICATION_ENABLE, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_get_capability(const struct device *dev,
				struct capability_t *caps)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (caps == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		GET_CAPABILITY.cmd,
		GET_CAPABILITY.len,
		GET_CAPABILITY.sub,
		0x00,
	};

	return rts54_post_command(dev, CMD_GET_CAPABILITY, payload,
				  ARRAY_SIZE(payload), (uint8_t *)caps);
}

static int rts54_get_connector_capability(const struct device *dev,
					  union connector_capability_t *caps)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (caps == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		GET_CONNECTOR_CAPABILITY.cmd,
		GET_CONNECTOR_CAPABILITY.len,
		GET_CONNECTOR_CAPABILITY.sub,
		0x00,
	};

	return rts54_post_command(dev, CMD_GET_CONNECTOR_CAPABILITY, payload,
				  ARRAY_SIZE(payload), (uint8_t *)caps);
}

static int rts54_get_connector_status(const struct device *dev,
				      struct connector_status_t *cs)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (cs == NULL) {
		return -EINVAL;
	}

	/*
	 * NOTE: Realtek's get connector status command doesn't provide all the
	 * information in the UCSI get connector status command, but the
	 * get rtk status command comes close.
	 */

	return rts54_get_rtk_status(dev, 0, 18, CMD_GET_CONNECTOR_STATUS,
				    (uint8_t *)cs);
}

static int rts54_get_error_status(const struct device *dev,
				  union error_status_t *es)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (es == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		UCSI_GET_ERROR_STATUS.cmd,
		UCSI_GET_ERROR_STATUS.len,
		UCSI_GET_ERROR_STATUS.sub,
		0x00,
	};

	return rts54_post_command(dev, CMD_GET_ERROR_STATUS, payload,
				  ARRAY_SIZE(payload), (uint8_t *)es);
}

static int rts54_set_rdo(const struct device *dev, uint32_t rdo)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		SET_RDO.cmd, SET_RDO.len, SET_RDO.sub, 0x00,
		BYTE0(rdo),  BYTE1(rdo),  BYTE2(rdo),  BYTE3(rdo),
	};

	return rts54_post_command(dev, CMD_SET_RDO, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_get_rdo(const struct device *dev, uint32_t *rdo)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (rdo == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		GET_RDO.cmd,
		GET_RDO.len,
		GET_RDO.sub,
		0x00,
	};

	return rts54_post_command(dev, CMD_GET_RDO, payload,
				  ARRAY_SIZE(payload), (uint8_t *)rdo);
}

static int rts54_get_pdos(const struct device *dev, enum pdo_type_t pdo_type,
			  enum pdo_offset_t pdo_offset, uint8_t num_pdos,
			  bool port_partner_pdo, uint32_t *pdos)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte4;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (pdos == NULL) {
		return -EINVAL;
	}

	byte4 = (num_pdos << 5) | (pdo_offset << 2) | (port_partner_pdo << 1) |
		pdo_type;

	memset((uint8_t *)pdos, 0, sizeof(uint32_t) * num_pdos);

	uint8_t payload[] = {
		GET_PDOS.cmd, GET_PDOS.len, GET_PDOS.sub, 0x00, byte4,
	};

	return rts54_post_command(dev, CMD_GET_PDOS, payload,
				  ARRAY_SIZE(payload), (uint8_t *)pdos);
}

static int rts54_get_info(const struct device *dev, struct pdc_info_t *info)
{
	struct pdc_data_t *data = dev->data;

	if ((get_state(data) != ST_IDLE) && (get_state(data) != ST_INIT)) {
		return -EBUSY;
	}

	if (info == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		GET_IC_STATUS.cmd,
		GET_IC_STATUS.len,
		GET_IC_STATUS.sub,
		0x00,
		26,
	};

	return rts54_post_command(dev, CMD_GET_IC_STATUS, payload,
				  ARRAY_SIZE(payload), (uint8_t *)info);
}

static int rts54_get_bus_info(const struct device *dev,
			      struct pdc_bus_info_t *info)
{
	const struct pdc_config_t *cfg =
		(const struct pdc_config_t *)dev->config;

	if (info == NULL) {
		return -EINVAL;
	}

	info->bus_type = PDC_BUS_TYPE_I2C;
	info->i2c = cfg->i2c;

	return 0;
}

static int rts54_get_vbus_voltage(const struct device *dev, uint16_t *voltage)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (voltage == NULL) {
		return -EINVAL;
	}

	return rts54_get_rtk_status(dev, 16, 2, CMD_GET_VBUS_VOLTAGE,
				    (uint8_t *)voltage);
}

static int rts54_set_ccom(const struct device *dev, enum ccom_t ccom,
			  enum drp_mode_t dm)
{
	struct pdc_data_t *data = dev->data;
	uint8_t byte = 0;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	switch (ccom) {
	case CCOM_RP:
		byte = 0x02;
		break;
	case CCOM_DRP:
		byte = 0x01;
		switch (dm) {
		case DRP_NORMAL:
			/* No Try.Src or Try.Snk */
			break;
		case DRP_TRY_SRC:
			byte |= (1 << 3);
			break;
		case DRP_TRY_SNK:
			byte |= (2 << 3);
			break;
		}
		break;
	case CCOM_RD:
		byte = 0;
		break;
	}

	/* We always want Accessory Support */
	byte |= (1 << 2);

	uint8_t payload[] = {
		SET_TPC_CSD_OPERATION_MODE.cmd,
		SET_TPC_CSD_OPERATION_MODE.len,
		SET_TPC_CSD_OPERATION_MODE.sub,
		0x00,
		byte,
	};

	return rts54_post_command(dev, CMD_SET_CCOM, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_set_uor(const struct device *dev, union uor_t uor)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		SET_UOR.cmd, SET_UOR.len, SET_UOR.sub, 0x00, uor.raw_value,
	};

	return rts54_post_command(dev, CMD_SET_UOR, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_set_pdr(const struct device *dev, union pdr_t pdr)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		SET_PDR.cmd, SET_PDR.len, SET_PDR.sub, 0x00, pdr.raw_value,
	};

	return rts54_post_command(dev, CMD_SET_PDR, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_get_current_pdo(const struct device *dev, uint32_t *pdo)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (pdo == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		GET_CURRENT_PARTNER_SRC_PDO.cmd,
		GET_CURRENT_PARTNER_SRC_PDO.len,
		GET_CURRENT_PARTNER_SRC_PDO.sub,
		0x00,
	};

	return rts54_post_command(dev, CMD_GET_CURRENT_PARTNER_SRC_PDO, payload,
				  ARRAY_SIZE(payload), (uint8_t *)pdo);

	return 0;
}

static bool rts54_is_init_done(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	return data->init_done;
}

static const struct pdc_driver_api_t pdc_driver_api = {
	.is_init_done = rts54_is_init_done,
	.get_ucsi_version = rts54_get_ucsi_version,
	.reset = rts54_pdc_reset,
	.connector_reset = rts54_connector_reset,
	.get_capability = rts54_get_capability,
	.get_connector_capability = rts54_get_connector_capability,
	.set_ccom = rts54_set_ccom,
	.set_uor = rts54_set_uor,
	.set_pdr = rts54_set_pdr,
	.set_sink_path = rts54_set_sink_path,
	.get_connector_status = rts54_get_connector_status,
	.get_pdos = rts54_get_pdos,
	.get_rdo = rts54_get_rdo,
	.set_rdo = rts54_set_rdo,
	.get_error_status = rts54_get_error_status,
	.get_vbus_voltage = rts54_get_vbus_voltage,
	.get_current_pdo = rts54_get_current_pdo,
	.set_handler_cb = rts54_set_handler_cb,
	.read_power_level = rts54_read_power_level,
	.get_info = rts54_get_info,
	.get_bus_info = rts54_get_bus_info,
	.set_power_level = rts54_set_power_level,
	.reconnect = rts54_reconnect,
};

static void interrupt_handler(struct k_work *item)
{
	irq_pending = true;
}

static void pdc_interrupt_callback(const struct device *dev,
				   struct gpio_callback *cb, uint32_t pins)
{
	struct pdc_data_t *data = CONTAINER_OF(cb, struct pdc_data_t, gpio_cb);

	k_work_submit(&data->work);
}

static int pdc_init(const struct device *dev)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	int rv;

	rv = i2c_is_ready_dt(&cfg->i2c);
	if (rv < 0) {
		LOG_ERR("device %s not ready", cfg->i2c.bus->name);
		return -ENODEV;
	}

	rv = gpio_is_ready_dt(&cfg->irq_gpios);
	if (rv < 0) {
		LOG_ERR("device %s not ready", cfg->irq_gpios.port->name);
		return -ENODEV;
	}

	if (!irq_init_done) {
		irq_shared_port = cfg->irq_gpios.port;
		irq_share_pin = cfg->irq_gpios.pin;

		rv = gpio_pin_configure_dt(&cfg->irq_gpios, GPIO_INPUT);
		if (rv < 0) {
			LOG_ERR("Unable to configure GPIO");
			return rv;
		}

		gpio_init_callback(&data->gpio_cb, pdc_interrupt_callback,
				   BIT(cfg->irq_gpios.pin));

		rv = gpio_add_callback(cfg->irq_gpios.port, &data->gpio_cb);
		if (rv < 0) {
			LOG_ERR("Unable to add callback");
			return rv;
		}

		rv = gpio_pin_interrupt_configure_dt(&cfg->irq_gpios,
						     GPIO_INT_EDGE_FALLING);
		if (rv < 0) {
			LOG_ERR("Unable to configure interrupt");
			return rv;
		}

		k_work_init(&data->work, interrupt_handler);
		irq_init_done = true;
	} else {
		if (irq_shared_port != cfg->irq_gpios.port ||
		    irq_share_pin != cfg->irq_gpios.pin) {
			LOG_ERR("All rts54xx ports must use the same interrupt");
			return -EINVAL;
		}
	}

	k_mutex_init(&data->mtx);

	data->dev = dev;
	data->cmd = CMD_NONE;
	pdc_data[cfg->connector_number] = data;

	/* Set initial state */
	data->init_local_state = INIT_PDC_ENABLE;
	smf_set_initial(SMF_CTX(data), &states[ST_INIT]);

	/* Create the thread for this port */
	cfg->create_thread(dev);

	/* Trigger an interrupt on startup */
	irq_pending = true;

	LOG_INF("Realtek RTS545x PDC DRIVER");

	return 0;
}

static void rts54xx_thread(void *dev, void *unused1, void *unused2)
{
	struct pdc_data_t *data = ((const struct device *)dev)->data;

	while (1) {
		smf_run_state(SMF_CTX(data));
		k_sleep(K_MSEC(T_PING_STATUS));
	}
}

#define PDC_DEFINE(inst)                                                      \
	K_THREAD_STACK_DEFINE(thread_stack_area_##inst,                       \
			      CONFIG_USBC_PDC_RTS54XX_STACK_SIZE);            \
                                                                              \
	static void create_thread_##inst(const struct device *dev)            \
	{                                                                     \
		struct pdc_data_t *data = dev->data;                          \
                                                                              \
		data->thread = k_thread_create(                               \
			&data->thread_data, thread_stack_area_##inst,         \
			K_THREAD_STACK_SIZEOF(thread_stack_area_##inst),      \
			rts54xx_thread, (void *)dev, 0, 0,                    \
			CONFIG_USBC_PDC_RTS54XX_THREAD_PRIORITY, K_ESSENTIAL, \
			K_NO_WAIT);                                           \
		k_thread_name_set(data->thread, "RTS54XX" STRINGIFY(inst));   \
	}                                                                     \
                                                                              \
	static struct pdc_data_t pdc_data_##inst;                             \
                                                                              \
	static const struct pdc_config_t pdc_config##inst = {                 \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                            \
		.irq_gpios = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),          \
		.connector_number =                                           \
			USBC_PORT_FROM_DRIVER_NODE(DT_DRV_INST(inst), pdc),   \
		.create_thread = create_thread_##inst,                        \
	};                                                                    \
                                                                              \
	DEVICE_DT_INST_DEFINE(inst, pdc_init, NULL, &pdc_data_##inst,         \
			      &pdc_config##inst, POST_KERNEL,                 \
			      CONFIG_APPLICATION_INIT_PRIORITY,               \
			      &pdc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PDC_DEFINE)
