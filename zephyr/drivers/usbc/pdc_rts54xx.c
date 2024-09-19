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
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys_clock.h>
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
#define T_PING_STATUS 20

/**
 * @brief Error Recovery Delay Counter (time delay is 60mS)
 */
#define N_ERROR_RECOVERY_DELAY_COUNT (60 / T_PING_STATUS)

/**
 * @brief Max number of error recovery attempts
 */
#define N_MAX_ERROR_RECOVERY_COUNT 4

/**
 * @brief Number of times to try an I2C transaction
 */
#define N_I2C_TRANSACTION_COUNT 10

/**
 * @brief Number of times to send a ping status
 */
#define N_RETRY_COUNT 200

/**
 * @brief Number of times to try and initialize the driver
 */
#define N_INIT_RETRY_ATTEMPT_MAX 2

/**
 * @brief Connector Status VBUS Voltage Scale Factor is 5mV
 */
#define VOLTAGE_SCALE_FACTOR 5

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
 *
 * These are based on the Realtek spec version 3.3.25.
 *
 * "Data Byte 0" is the first byte after "Byte Count" and is available
 * at .rd_buf[1].
 */
#define RTS54XX_GET_IC_STATUS_RUNNING_FLASH_CODE 1
#define RTS54XX_GET_IC_STATUS_FWVER_MAJOR_OFFSET 4
#define RTS54XX_GET_IC_STATUS_FWVER_MINOR_OFFSET 5
#define RTS54XX_GET_IC_STATUS_FWVER_PATCH_OFFSET 6
#define RTS54XX_GET_IC_STATUS_VID_L 10
#define RTS54XX_GET_IC_STATUS_VID_H 11
#define RTS54XX_GET_IC_STATUS_PID_L 12
#define RTS54XX_GET_IC_STATUS_PID_H 13
#define RTS54XX_GET_IC_STATUS_RUNNING_FLASH_BANK 15
#define RTS54XX_GET_IC_STATUS_PD_REV_MAJOR_OFFSET 23
#define RTS54XX_GET_IC_STATUS_PD_REV_MINOR_OFFSET 24
#define RTS54XX_GET_IC_STATUS_PD_VER_MAJOR_OFFSET 25
#define RTS54XX_GET_IC_STATUS_PD_VER_MINOR_OFFSET 26
#define RTS54XX_GET_IC_STATUS_PROG_NAME_STR 27
#define RTS54XX_GET_IC_STATUS_PROG_NAME_STR_LEN 12

/* FW project name length should not exceed the max length supported in struct
 * pdc_info_t
 */
BUILD_ASSERT(RTS54XX_GET_IC_STATUS_PROG_NAME_STR_LEN <=
	     (sizeof(((struct pdc_info_t *)0)->project_name) - 1));

/**
 * @brief Extra bits supported by the Realtek SET_NOTIFICATION_ENABLE command.
 */
#define RTS54XX_NOTIFY_DP_STATUS BIT(21)
#define RTS54XX_NOTIFY_EXT_BIT_OFFSET 16

/**
 * @brief Macro to transition to init or idle state and return
 */
#define TRANSITION_TO_INIT_OR_IDLE_STATE(data)  \
	transition_to_init_or_idle_state(data); \
	return

/**
 * @brief IRQ Event set by the interrupt handler.
 */
#define RTS54XX_IRQ_EVENT BIT(0)

/**
 * @brief Event set to run next state of state machine.
 */
#define RTS54XX_NEXT_STATE_READY BIT(1)

/**
 * @brief Number of RTS54XX ports detected
 */
#define NUM_PDC_RTS54XX_PORTS DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

/**
 * @brief RTS54XX I2C block read command
 */
#define RTS54XX_BLOCK_READ_CMD 0x80

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
#define REALTEK_PD_COMMAND 0x0e

static const struct smbus_cmd_t VENDOR_CMD_ENABLE = { 0x01, 0x03, 0xDA };
static const struct smbus_cmd_t SET_NOTIFICATION_ENABLE = { 0x08, 0x06, 0x01 };
static const struct smbus_cmd_t SET_PDO = { 0x08, 0x03, 0x03 };
static const struct smbus_cmd_t SET_RDO = { 0x08, 0x06, 0x04 };
static const struct smbus_cmd_t SET_TPC_RP = { 0x08, 0x03, 0x05 };
static const struct smbus_cmd_t SET_TPC_CSD_OPERATION_MODE = { 0x08, 0x03,
							       0x1D };
static const struct smbus_cmd_t SET_TPC_RECONNECT = { 0x08, 0x03, 0x1F };
static const struct smbus_cmd_t FORCE_SET_POWER_SWITCH = { 0x08, 0x03, 0x21 };
static const struct smbus_cmd_t GET_RDO = { 0x08, 0x02, 0x84 };
static const struct smbus_cmd_t GET_VDO = { 0x08, 0x03, 0x9A };
static const struct smbus_cmd_t GET_CURRENT_PARTNER_SRC_PDO = { 0x08, 0x02,
								0xA7 };
static const struct smbus_cmd_t RTS_SET_FRS_FUNCTION = { 0x08, 0x03, 0xE1 };
static const struct smbus_cmd_t GET_RTK_STATUS = { 0x09, 0x03 };
static const struct smbus_cmd_t RTS_UCSI_PPM_RESET = { 0x0E, 0x02, 0x01 };
static const struct smbus_cmd_t RTS_UCSI_CONNECTOR_RESET = { 0x0E, 0x03, 0x03 };
static const struct smbus_cmd_t RTS_UCSI_GET_CAPABILITY = { 0x0E, 0x02, 0x06 };
static const struct smbus_cmd_t RTS_UCSI_GET_CONNECTOR_CAPABILITY = { 0x0E,
								      0x03,
								      0x07 };
static const struct smbus_cmd_t RTS_UCSI_SET_UOR = { 0x0E, 0x04, 0x09 };
static const struct smbus_cmd_t RTS_UCSI_SET_PDR = { 0x0E, 0x04, 0x0B };
static const struct smbus_cmd_t RTS_UCSI_GET_PDOS = { .cmd = 0x0E,
						      .len = 0x05,
						      .sub = 0x10 };
static const struct smbus_cmd_t RTS_UCSI_GET_CONNECTOR_STATUS = { 0x0E, 0x3,
								  0x12 };
static const struct smbus_cmd_t RTS_UCSI_GET_ERROR_STATUS = { 0x0E, 0x03,
							      0x13 };
static const struct smbus_cmd_t RTS_UCSI_READ_POWER_LEVEL = { 0x0E, 0x05,
							      0x1E };
static const struct smbus_cmd_t RTS_UCSI_SET_CCOM = { 0x0E, 0x04, 0x08 };
static const struct smbus_cmd_t GET_IC_STATUS = { 0x3A, 0x03 };
static const struct smbus_cmd_t SET_RETIMER_FW_UPDATE_MODE = { 0x20, 0x03,
							       0x00 };
static const struct smbus_cmd_t RTS_UCSI_GET_CABLE_PROPERTY = { 0x0E, 0x03,
								0x11 };
static const struct smbus_cmd_t GET_PCH_DATA_STATUS = { 0x08, 0x02, 0xE0 };
static const struct smbus_cmd_t ACK_CC_CI = { 0x0A, 0x07, 0x00 };
static const struct smbus_cmd_t RTS_UCSI_GET_LPM_PPM_INFO = { 0x0E, 0x03,
							      0x22 };

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
 * @brief PDC port flags
 */
enum pdc_flags_t {
	/** PDC is currently processing IRQ. */
	PDC_HANDLING_IRQ,
	/** Number of supported PDC flags. */
	PDC_FLAGS_COUNT,
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
	/** Error Recovery State */
	ST_ERROR_RECOVERY,
	/** Disable State */
	ST_DISABLE,
	/** PDC communication suspended */
	ST_SUSPENDED,
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
	INIT_PDC_COMPLETE,
	/** Initialization error */
	INIT_ERROR,
	/** Wait for command to send */
	INIT_PDC_CMD_WAIT
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
	/** Set DRP_MODE */
	CMD_SET_DRP_MODE,
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
	/** Set the Fast Role Swap */
	CMD_SET_FRS_FUNCTION,
	/** Set the Rp TypeC current */
	CMD_SET_TPC_RP,
	/** TypeC reconnect */
	CMD_SET_TPC_RECONNECT,
	/** set Retimer into FW Update Mode */
	CMD_SET_RETIMER_FW_UPDATE_MODE,
	/** Get the cable properties */
	CMD_GET_CABLE_PROPERTY,
	/** Get VDO(s) of PDC, Cable, or Port partner */
	CMD_GET_VDO,
	/** CMD_GET_IDENTITY_DISCOVERY */
	CMD_GET_IDENTITY_DISCOVERY,
	/** CMD_GET_IS_VCONN_SOURCING */
	CMD_GET_IS_VCONN_SOURCING,
	/** CMD_SET_PDO */
	CMD_SET_PDO,
	/** Get PDC ALT MODE Status Register value */
	CMD_GET_PCH_DATA_STATUS,
	/** CMD_ACK_CC_CI */
	CMD_ACK_CC_CI,
	/** Raw UCSI call.
	 * Special handling of the data read from a PDC will be skipped. */
	CMD_RAW_UCSI,
	/** CMD_GET_LPM_PPM_INFO */
	CMD_GET_LPM_PPM_INFO,
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
	/** Notification enable bits */
	union notification_enable_t bits;
	/** Create thread function */
	void (*create_thread)(const struct device *dev);
	/** If true, do not apply PDC FW updates to this port */
	bool no_fw_update;
};

/**
 * @brief PDC Data object
 */
struct pdc_data_t {
	/** State machine context */
	struct smf_ctx ctx;
	/** Init's local state variable */
	enum init_state_t init_local_state;
	/** Init's current state */
	enum init_state_t init_local_current_state;
	/** Init's next state */
	enum init_state_t init_local_next_state;
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
	/** Timepoint for when we can next call ping status. */
	k_timepoint_t next_ping_status;
	/** Ping status retry counter */
	uint8_t ping_retry_counter;
	/** Number of time the init process has been attempted */
	uint8_t init_retry_counter;
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
	/** GPIO interrupt callback */
	struct gpio_callback gpio_cb;
	/** Error status */
	union error_status_t error_status;
	/** CCI Event */
	union cci_event_t cci_event;
	/** CC Event callback */
	struct pdc_callback *cc_cb;
	/** CC Event one-time callback. If it's NULL, cci_cb will be called. */
	struct pdc_callback *cc_cb_tmp;
	/** Asynchronous (CI) Event callbacks */
	sys_slist_t ci_cb_list;
	/** Information about the PDC */
	struct pdc_info_t info;
	/** Init done flag */
	bool init_done;
	/** Error recovery delay counter */
	uint16_t error_recovery_delay_counter;
	/** Error recovery counter */
	uint16_t error_recovery_counter;
	/** Error Status used during initialization */
	union error_status_t es;
	/* Driver specific events to handle. */
	struct k_event driver_event;
	/** Port specific PDC flags */
	atomic_t flags;
	/* Currently running UCSI command. */
	enum ucsi_command_t active_ucsi_cmd;
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
	[CMD_SET_DRP_MODE] = "SET_DRP_MODE",
	[CMD_SET_SINK_PATH] = "SET_SINK_PATH",
	[CMD_READ_POWER_LEVEL] = "READ_POWER_LEVEL",
	[CMD_GET_RDO] = "GET_RDO",
	[CMD_SET_TPC_RP] = "SET_TPC_RP",
	[CMD_SET_TPC_RECONNECT] = "SET_TPC_RECONNECT",
	[CMD_SET_RDO] = "SET_RDO",
	[CMD_GET_CURRENT_PARTNER_SRC_PDO] = "GET_CURRENT_PARTNER_SRC_PDO",
	[CMD_SET_FRS_FUNCTION] = "SET_FRS_FUNCTION",
	[CMD_SET_RETIMER_FW_UPDATE_MODE] = "SET_RETIMER_FW_UPDATE_MODE",
	[CMD_GET_CABLE_PROPERTY] = "GET_CABLE_PROPERTY",
	[CMD_GET_VDO] = "GET VDO",
	[CMD_GET_IDENTITY_DISCOVERY] = "CMD_GET_IDENTITY_DISCOVERY",
	[CMD_GET_IS_VCONN_SOURCING] = "CMD_GET_IS_VCONN_SOURCING",
	[CMD_SET_PDO] = "CMD_SET_PDO",
	[CMD_GET_PCH_DATA_STATUS] = "CMD_GET_PCH_DATA_STATUS",
	[CMD_ACK_CC_CI] = "CMD_ACK_CC_CI",
	[CMD_RAW_UCSI] = "CMD_RAW_UCSI",
	[CMD_GET_LPM_PPM_INFO] = "CMD_GET_LPM_PPM_INFO",
};

/**
 * @brief List of human readable state names for console debugging
 */
/* TODO(b/325128262): Bug to explore simplifying the the state machine */
static const char *const state_names[] = {
	[ST_INIT] = "INIT",
	[ST_IDLE] = "IDLE",
	[ST_WRITE] = "WRITE",
	[ST_PING_STATUS] = "PING_STATUS",
	[ST_READ] = "READ",
	[ST_ERROR_RECOVERY] = "ERROR_RECOVERY",
	[ST_DISABLE] = "PDC_DISABLED",
	[ST_SUSPENDED] = "PDC_SUSPENDED",
};

static const struct device *irq_shared_port;
static int irq_share_pin;
static bool irq_init_done;
static const struct smf_state states[];
static int rts54_enable(const struct device *dev);
static int rts54_reset(const struct device *dev);
static int rts54_set_notification_enable(const struct device *dev,
					 union notification_enable_t bits,
					 uint16_t ext_bits);
static int rts54_get_info(const struct device *dev, struct pdc_info_t *info,
			  bool live);
static int rts54_get_error_status(const struct device *dev,
				  union error_status_t *es);

/**
 * @brief PDC port data used in interrupt handler
 */
static struct pdc_data_t *pdc_data[CONFIG_USB_PD_PORT_MAX_COUNT];

/**
 * @brief Pointer to thread specific k_event that handles interrupts.
 */
static struct k_event *irq_event;

static enum state_t get_state(struct pdc_data_t *data)
{
	return data->ctx.current - &states[0];
}

static void set_state(struct pdc_data_t *data, const enum state_t next_state)
{
	data->last_state = get_state(data);
	smf_set_state(SMF_CTX(data), &states[next_state]);
	k_event_post(&data->driver_event, RTS54XX_NEXT_STATE_READY);
}

/**
 * Atomic flag to suspend sending new commands to chip
 *
 * This flag is shared across driver instances.
 *
 * TODO(b/323371550) When more than one PDC is supported, this flag will need
 * to be tracked per-chip.
 */
static atomic_t suspend_comms_flag = ATOMIC_INIT(0);

static void suspend_comms(void)
{
	atomic_set(&suspend_comms_flag, 1);
}

static void enable_comms(void)
{
	atomic_set(&suspend_comms_flag, 0);
}

static bool check_comms_suspended(void)
{
	return atomic_get(&suspend_comms_flag) != 0;
}

static void print_current_state(struct pdc_data_t *data)
{
	const struct pdc_config_t *cfg = data->dev->config;
	int st = get_state(data);

	if (st == ST_WRITE) {
		if (data->cmd == CMD_RAW_UCSI) {
			LOG_INF("ST%d: %s RAW:%s", cfg->connector_number,
				state_names[st],
				get_ucsi_command_name(data->active_ucsi_cmd));
		} else {
			LOG_INF("ST%d: %s %s", cfg->connector_number,
				state_names[st], cmd_names[data->cmd]);
		}
	} else if (st == ST_ERROR_RECOVERY) {
		LOG_INF("ST%d: %s %s %d", cfg->connector_number,
			state_names[st], cmd_names[data->cmd],
			data->error_recovery_counter);
	} else {
		LOG_INF("ST%d: %s", cfg->connector_number,
			state_names[get_state(data)]);
	}
}

static void call_cci_event_cb(struct pdc_data_t *data)
{
	const struct pdc_config_t *cfg = data->dev->config;
	const union cci_event_t cci = data->cci_event;

	if (!data->init_done) {
		return;
	}

	LOG_INF("C%d: CCI=0x%x", cfg->connector_number, cci.raw_value);

	/*
	 * CC and CI events are separately reported. So, we need to call only
	 * one callback or the other.
	 */
	if (cci.connector_change) {
		pdc_fire_callbacks(&data->ci_cb_list, data->dev, cci);
	} else if (data->cc_cb_tmp) {
		data->cc_cb_tmp->handler(data->dev, data->cc_cb_tmp, cci);
	} else if (data->cc_cb) {
		data->cc_cb->handler(data->dev, data->cc_cb, cci);
	}

	data->cci_event.raw_value = 0;
}

static int get_ara(const struct device *dev, uint8_t *ara)
{
	const struct pdc_config_t *cfg = dev->config;

	return i2c_read(cfg->i2c.bus, ara, 1, SMBUS_ADDRESS_ARA);
}

static void perform_pdc_init(struct pdc_data_t *data)
{
	data->init_retry_counter = 0;
	data->error_status.raw_value = 0;
	/* Set initial local state of Init */
	data->init_local_state = INIT_PDC_ENABLE;
	set_state(data, ST_INIT);
}

/**
 * @brief This function should be called after any I2C transfer that failed.
 * It increments a counter, notifies the subsystem of the I2C error and then
 * enters the recovery state. NOTE: the data->i2c_transaction_retry_counter
 * should be set to zero in the calling states entry action.
 */
static bool max_i2c_retry_reached(struct pdc_data_t *data, int type)
{
	const struct pdc_config_t *cfg = data->dev->config;

	data->i2c_transaction_retry_counter++;
	if (data->i2c_transaction_retry_counter > N_I2C_TRANSACTION_COUNT) {
		/* MAX I2C transactions exceeded */
		LOG_ERR("C%d: %s i2c error", cfg->connector_number,
			(type & I2C_MSG_READ) ? "Read" : "Write");
		/*
		 * The command was not successfully completed,
		 * so set cci.error to 1b.
		 */
		data->cci_event.error = 1;
		/* Command has completed */
		data->cci_event.command_completed = 1;
		/* Clear busy event */
		data->cci_event.busy = 0;
		/* Set error, I2C read error */
		if (type & I2C_MSG_READ) {
			data->error_status.i2c_read_error = 1;
		} else {
			data->error_status.i2c_write_error = 1;
		}
		/* Notify system of status change */
		call_cci_event_cb(data);
		return true;
	}
	return false;
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
	uint8_t cmd = RTS54XX_BLOCK_READ_CMD;
	int rv;

	msg[0].buf = &cmd;
	msg[0].len = 1;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = data->rd_buf;
	msg[1].len = data->ping_status.data_len + 1;
	msg[1].flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP;

	rv = i2c_transfer_dt(&cfg->i2c, msg, 2);
	if (rv < 0) {
		return rv;
	}

	data->rd_buf_len = data->ping_status.data_len;

	if (IS_ENABLED(CONFIG_USBC_PDC_TRACE_MSG)) {
		pdc_trace_msg_resp(cfg->connector_number,
				   PDC_TRACE_CHIP_TYPE_RTS54XX, data->rd_buf,
				   data->ping_status.data_len + 1);
	}

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
	/* pdc_init_failed is cleared when init process is complete */
	data->es.pdc_init_failed = 1;
	data->cmd = CMD_NONE;
}

static void init_write_cmd_and_change_state(struct pdc_data_t *data,
					    enum init_state_t next)
{
	data->init_local_current_state = data->init_local_state;
	data->init_local_next_state = next;
	data->init_local_state = INIT_PDC_CMD_WAIT;
	set_state(data, ST_WRITE);
}

static void init_display_error_status(struct pdc_data_t *data)
{
	const struct pdc_config_t *cfg = data->dev->config;
	int cnum = cfg->connector_number;

	if (data->es.unrecognized_command) {
		LOG_ERR("C%d: Unrecognized Command", cnum);
	}

	if (data->es.non_existent_connector_number) {
		LOG_ERR("C%d: Invalid Connector Number", cnum);
	}

	if (data->es.invalid_command_specific_param) {
		LOG_ERR("C%d: Invalid Param", cnum);
	}

	if (data->es.incompatible_connector_partner) {
		LOG_ERR("C%d: Invalid Connector Partner", cnum);
	}

	if (data->es.cc_communication_error) {
		LOG_ERR("C:%d CC Comm Error", cnum);
	}

	if (data->es.cmd_unsuccessful_dead_batt) {
		LOG_ERR("C:%d Dead Batt Error", cnum);
	}

	if (data->es.contract_negotiation_failed) {
		LOG_ERR("C:%d Contract Negotiation Failed", cnum);
	}
}

static void st_init_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	const struct pdc_config_t *cfg = data->dev->config;
	int cnum = cfg->connector_number;
	int rv;

	/* Do not start executing commands if suspended */
	if (check_comms_suspended()) {
		set_state(data, ST_SUSPENDED);
		return;
	}

	switch (data->init_local_state) {
	case INIT_PDC_ENABLE:
		rv = rts54_enable(data->dev);
		if (rv) {
			LOG_ERR("C:%d, Internal(INIT_PDC_ENABLE)", cnum);
			set_state(data, ST_DISABLE);
			return;
		}
		init_write_cmd_and_change_state(data, INIT_PDC_GET_IC_STATUS);
		return;
	case INIT_PDC_GET_IC_STATUS:
		rv = rts54_get_info(data->dev, &data->info, true);
		if (rv) {
			LOG_ERR("C:%d, Internal(INIT_PDC_GET_IC_STATUS)", cnum);
			set_state(data, ST_DISABLE);
			return;
		}
		init_write_cmd_and_change_state(
			data, INIT_PDC_SET_NOTIFICATION_ENABLE);
		return;
	case INIT_PDC_SET_NOTIFICATION_ENABLE:
		rv = rts54_set_notification_enable(
			data->dev, cfg->bits,
			RTS54XX_NOTIFY_DP_STATUS >>
				RTS54XX_NOTIFY_EXT_BIT_OFFSET);
		if (rv) {
			LOG_ERR("C:%d, Internal(INIT_PDC_SET_NOTIFICATION_ENABLE)",
				cnum);
			set_state(data, ST_DISABLE);
			return;
		}
		init_write_cmd_and_change_state(data, INIT_PDC_RESET);
		return;
	case INIT_PDC_RESET:
		rv = rts54_reset(data->dev);
		if (rv) {
			LOG_ERR("C:%d, Internal(INIT_PDC_RESET)", cnum);
			set_state(data, ST_DISABLE);
			return;
		}
		init_write_cmd_and_change_state(data, INIT_PDC_COMPLETE);
		return;
	case INIT_PDC_COMPLETE:
		data->es.pdc_init_failed = 0;
		/* Init is complete, so transition to Idle state */
		set_state(data, ST_IDLE);
		data->init_done = true;
		return;
	case INIT_ERROR:
		/* Get error status, and re-start the init process */
		rts54_get_error_status(data->dev, &data->es);
		init_write_cmd_and_change_state(data, INIT_PDC_ENABLE);
		return;
	case INIT_PDC_CMD_WAIT:
		/* If PDC_RESET was sent, check the reset_completed flag */
		if (data->init_local_current_state == INIT_PDC_RESET) {
			if (!data->cci_event.reset_completed) {
				return;
			}
		} else if (!data->cci_event.command_completed) {
			return;
		}

		if (data->cci_event.error) {
			/* I2C read Error. No way to recover, so disable the PDC
			 */
			if (data->error_status.i2c_read_error) {
				LOG_INF("C%d: PDC I2C problem",
					cfg->connector_number);
				set_state(data, ST_DISABLE);
				return;
			}

			/* PDC not responding to Ping Status reads. Try error
			 * recovery */
			if (data->error_status.pdc_internal_error) {
				LOG_INF("C%d: PDC not responding",
					cfg->connector_number);
				set_state(data, ST_ERROR_RECOVERY);
				return;
			}

			/* PDC not responding to Error Status reads. Try error
			 * recovery */
			if (data->init_local_current_state == INIT_ERROR) {
				LOG_INF("C%d: PDC error status read fail ",
					cfg->connector_number);
				set_state(data, ST_ERROR_RECOVERY);
				return;
			}

			/* PDC returned an error */
			data->init_local_state = INIT_ERROR;
		} else {
			/* PDC Error status was read */
			if (data->init_local_current_state == INIT_ERROR) {
				/* Display error read from ping_status */
				init_display_error_status(data);
				/* Retry init or disable this port */
				if (data->init_retry_counter <=
				    N_INIT_RETRY_ATTEMPT_MAX) {
					data->init_retry_counter++;
					data->init_local_state =
						INIT_PDC_ENABLE;
				} else {
					set_state(data, ST_DISABLE);
				}
				return;
			}

			data->init_local_state = data->init_local_next_state;
		}
		break;
	}
}

/**
 * @brief Called from the main thread to handle interrupts
 */
static void handle_irqs(struct pdc_data_t *data)
{
	uint8_t ara;
	int rv;

	/*
	 * Since we use edge triggered interrupts, we need to check ARA for all
	 * ports. Earliest port on bus will respond to ARAs in order and we need
	 * to iterate until there are no ARA responses left to get interrupt
	 * line de-asserted fully.
	 *
	 * This assumes that this driver is valid for all PD controllers on the
	 * system.
	 */
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
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
					cfg->connector_number + 1;
				/* Set the interrupt event */
				pdc_int_data->cci_event
					.vendor_defined_indicator = 1;
				/* Set local interrupt handling flag */
				atomic_set_bit(&pdc_int_data->flags,
					       PDC_HANDLING_IRQ);
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
	data->active_ucsi_cmd = 0;
}

static void st_idle_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	/* Do not start executing commands if suspended */
	if (check_comms_suspended()) {
		set_state(data, ST_SUSPENDED);
		return;
	}

	/*
	 * Priority of events:
	 *  1: CMD_TRIGGER_PDC_RESET
	 *  2: Non-Reset command
	 */
	if (data->cmd == CMD_TRIGGER_PDC_RESET) {
		perform_pdc_init(data);
	} else if (data->cmd != CMD_NONE) {
		set_state(data, ST_WRITE);
	}
}

static void st_write_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);

	/* This state can only be entered from the Init and Idle states */
	assert(data->last_state == ST_INIT || data->last_state == ST_IDLE);

	/* Clear I2C transaction retry counter */
	data->i2c_transaction_retry_counter = 0;
	/* Only clear Error Status if the subsystem isn't going to read it */
	if (data->cmd != CMD_GET_ERROR_STATUS) {
		/* Clear the Error Status */
		data->error_status.raw_value = 0;
	}
	/* Clear the CCI Event */
	data->cci_event.raw_value = 0;
}

static void st_write_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	int rv;

	/* Write the command */
	rv = rts54_i2c_write(data->dev);
	if (rv < 0) {
		if (max_i2c_retry_reached(data, I2C_MSG_WRITE)) {
			set_state(data, ST_ERROR_RECOVERY);
		}
		return;
	}

	/* I2C transaction succeeded. Set timepoint for next ping status. */
	data->next_ping_status = sys_timepoint_calc(K_MSEC(T_PING_STATUS));
	set_state(data, ST_PING_STATUS);
}

static void st_ping_status_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);

	/* This state can only be entered from the Write Status state */
	assert(data->last_state == ST_WRITE);

	/* Clear I2c Transaction Retry Counter */
	data->i2c_transaction_retry_counter = 0;
	/* Clear Ping Rety Counter */
	data->ping_retry_counter = 0;
	/* Clear Ping Status */
	data->ping_status.raw_value = 0;
	/* Clear the CCI Event */
	data->cci_event.raw_value = 0;
}

static void st_ping_status_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	const struct pdc_config_t *cfg = data->dev->config;
	int rv;

	/*
	 * Make sure that we've waited sufficient time before re-reading ping
	 * status. Otherwise PDC may be starved of time to execute commands.
	 */
	if (!sys_timepoint_expired(data->next_ping_status)) {
		k_sleep(sys_timepoint_timeout(data->next_ping_status));
	}

	/* Read the Ping Status */
	rv = get_ping_status(data->dev);

	/* Reset time until next ping status. */
	data->next_ping_status = sys_timepoint_calc(K_MSEC(T_PING_STATUS));

	if (rv < 0) {
		if (max_i2c_retry_reached(data, I2C_MSG_READ)) {
			set_state(data, ST_ERROR_RECOVERY);
		}
		return;
	}

	switch (data->ping_status.cmd_sts) {
	case CMD_BUSY:
		/*
		 * Busy and Deferred are handled the same,
		 * so fall through
		 */
		__attribute__((fallthrough));
	case CMD_DEFERRED:
		/*
		 * The command has not been processed.
		 * Stay here and resend get ping status.
		 */
		data->ping_retry_counter++;
		if (data->ping_retry_counter > N_RETRY_COUNT) {
			/* MAX Ping Retries exceeded */
			LOG_ERR("C%d: Failed to read Ping Status",
				cfg->connector_number);
			/*
			 * The command was not successfully completed,
			 * so set cci.error to 1b.
			 */
			data->cci_event.error = 1;
			/* Command completed */
			data->cci_event.command_completed = 1;
			/* Clear busy event */
			data->cci_event.busy = 0;
			/* Error reading ping status */
			data->error_status.pdc_internal_error = 1;

			/* Notify system of status change */
			call_cci_event_cb(data);

			/* An error occurred, try to recover */
			set_state(data, ST_ERROR_RECOVERY);
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
		/* Clear busy event */
		data->cci_event.busy = 0;

		if (data->cmd == CMD_PPM_RESET) {
			/* The PDC has been reset,
			 * so set cci.reset_completed to 1b.
			 */
			data->cci_event.reset_completed = 1;
			/* Notify system of status change */
			call_cci_event_cb(data);
			LOG_DBG("C%d: Realtek PDC reset complete",
				cfg->connector_number);
			/* All done, return to Init or Idle state */
			TRANSITION_TO_INIT_OR_IDLE_STATE(data);
		} else {
			LOG_DBG("C%d: ping_status: %02x", cfg->connector_number,
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
		LOG_ERR("C%d: Ping Status Error", cfg->connector_number);
		/*
		 * The command was not successfully completed,
		 * so set cci.error to 1b.
		 */
		data->cci_event.error = 1;
		/* Command completed */
		data->cci_event.command_completed = 1;
		/* Clear busy event */
		data->cci_event.busy = 0;
		/* Notify system of status change */
		call_cci_event_cb(data);

		/* A command error occurred, return to idle state. The subsystem
		 * should read the status_register to determine the cause. */
		TRANSITION_TO_INIT_OR_IDLE_STATE(data);
		break;
	default:
		/* Ping Status returned an unknown command */
		LOG_ERR("C%d: unknown ping_status: %02x", cfg->connector_number,
			data->ping_status.raw_value);
		/* An error occurred, try to recover */
		set_state(data, ST_ERROR_RECOVERY);
		return;
	}
}

static void st_read_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);

	/* This state can only be entered from the Ping Status state */
	assert(data->last_state == ST_PING_STATUS);

	/* Clear the CCI Event */
	data->cci_event.raw_value = 0;
	/* Clear I2c Transaction Retry Counter */
	data->i2c_transaction_retry_counter = 0;
	/* Set the port the CCI Event occurred on */
}

static void st_read_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	const struct pdc_config_t *cfg = data->dev->config;
	uint8_t offset;
	uint8_t len;
	int rv;

	/*
	 * The data->user_buf is checked for NULL before a command is queued.
	 * The check here gauards against an eronious ping_status indicating
	 * data is available for a command that doesn't send data.
	 */
	if (!data->user_buf) {
		LOG_ERR("NULL read buffer pointer");
		/*
		 * The command was not successfully completed,
		 * so set cci.error to 1b.
		 */
		data->cci_event.error = 1;
		/* Command completed */
		data->cci_event.command_completed = 1;
		/* Null buffer error */
		data->error_status.null_buffer_error = 1;
		/* Notify system of status change */
		call_cci_event_cb(data);

		/* An error occurred, return to idle state */
		TRANSITION_TO_INIT_OR_IDLE_STATE(data);
	}

	rv = rts54_i2c_read(data->dev);
	if (rv < 0) {
		if (max_i2c_retry_reached(data, I2C_MSG_READ)) {
			set_state(data, ST_ERROR_RECOVERY);
		}
		return;
	}

	/* Get length of data returned */
	len = data->rd_buf[0];

	/* Skip over length byte */
	offset = 1;

	/* Copy the received data to the user's buffer */
	switch (data->cmd) {
	case CMD_GET_IC_STATUS: {
		struct pdc_info_t *info = (struct pdc_info_t *)data->user_buf;

		/* Realtek Is running flash code: Data Byte0 */
		info->is_running_flash_code =
			data->rd_buf[RTS54XX_GET_IC_STATUS_RUNNING_FLASH_CODE];

		/* Realtek FW main version: Data Byte3..5 */
		info->fw_version =
			data->rd_buf[RTS54XX_GET_IC_STATUS_FWVER_MAJOR_OFFSET]
				<< 16 |
			data->rd_buf[RTS54XX_GET_IC_STATUS_FWVER_MINOR_OFFSET]
				<< 8 |
			data->rd_buf[RTS54XX_GET_IC_STATUS_FWVER_PATCH_OFFSET];

		/* Realtek VID PID: Data Byte9..12 (little-endian) */
		info->vid_pid =
			data->rd_buf[RTS54XX_GET_IC_STATUS_VID_H] << 24 |
			data->rd_buf[RTS54XX_GET_IC_STATUS_VID_L] << 16 |
			data->rd_buf[RTS54XX_GET_IC_STATUS_PID_H] << 8 |
			data->rd_buf[RTS54XX_GET_IC_STATUS_PID_L];

		/* Realtek Running flash bank offset: Data Byte14 */
		info->running_in_flash_bank =
			data->rd_buf[RTS54XX_GET_IC_STATUS_RUNNING_FLASH_BANK];

		/* Realtek PD Revision: Data Byte22..23 (big-endian) */
		info->pd_revision =
			data->rd_buf[RTS54XX_GET_IC_STATUS_PD_REV_MAJOR_OFFSET]
				<< 8 |
			data->rd_buf[RTS54XX_GET_IC_STATUS_PD_REV_MINOR_OFFSET];

		/* Realtek PD Version: Data Byte24..25 (big-endian) */
		info->pd_version =
			data->rd_buf[RTS54XX_GET_IC_STATUS_PD_VER_MAJOR_OFFSET]
				<< 8 |
			data->rd_buf[RTS54XX_GET_IC_STATUS_PD_VER_MINOR_OFFSET];

		/* Project name string is supported on version >= 0.3.x */
		memcpy(info->project_name,
		       &data->rd_buf[RTS54XX_GET_IC_STATUS_PROG_NAME_STR],
		       RTS54XX_GET_IC_STATUS_PROG_NAME_STR_LEN);
		info->project_name[RTS54XX_GET_IC_STATUS_PROG_NAME_STR_LEN] =
			'\0';

		/* Only print this log on init */
		if (data->init_local_state != INIT_PDC_COMPLETE) {
			LOG_INF("C%d: Realtek: FW Version: %u.%u.%u (%s)",
				cfg->connector_number,
				PDC_FWVER_GET_MAJOR(info->fw_version),
				PDC_FWVER_GET_MINOR(info->fw_version),
				PDC_FWVER_GET_PATCH(info->fw_version),
				info->project_name);
			LOG_INF("C%d: Realtek: PD Version: %u, Rev %u",
				cfg->connector_number, info->pd_version,
				info->pd_revision);
		}

		/* Fill in the chip type (driver compat string) */
		strncpy(info->driver_name, STRINGIFY(DT_DRV_COMPAT),
			sizeof(info->driver_name));
		info->driver_name[sizeof(info->driver_name) - 1] = '\0';

		info->no_fw_update = cfg->no_fw_update;

		/* Retain a cached copy of this data */
		data->info = *info;

		break;
	}
	case CMD_GET_VBUS_VOLTAGE: {
		union connector_status_t *status =
			(union connector_status_t *)(data->rd_buf + offset);
		*(uint16_t *)data->user_buf = status->voltage_reading *
					      status->voltage_scale *
					      VOLTAGE_SCALE_FACTOR;
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
		/*
		 * Note: If Realtek did indicate Hard Reset, then it would also
		 * make sense to notify the host of PD_STATUS_EVENT_HARD_RESET.
		 * However, this would be redundant with the notification that
		 * will be generated later, upon completion of
		 * GET_CONNECTOR_STATUS.
		 */
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
	case CMD_GET_IDENTITY_DISCOVERY: {
		bool *disc_state = (bool *)data->user_buf;

		/* Realtek Altmode related state, Byte 14 bits 0-2*/
		*disc_state = (data->rd_buf[14] & 0x07);
		break;
	}
	case CMD_GET_IS_VCONN_SOURCING: {
		bool *vconn_sourcing = (bool *)data->user_buf;

		/* Realtek PD Sourcing VCONN, Byte 11, bit 5 */
		*vconn_sourcing = (data->rd_buf[11] & 0x20);
		break;
	}
	case CMD_GET_CONNECTOR_STATUS:
		memcpy(data->user_buf, data->rd_buf + offset, len);

		/*
		 * If this is the first connector status since an IRQ, it may
		 * be in response to an Attention message. Check current partner
		 * flags and status change bits to determine if it was likely an
		 * Attention message (DP Status).
		 *
		 * TODO(b/356955093) Remove this when the PDC firmware supports
		 * IRQs on Attention messages.
		 */
		if (atomic_test_and_clear_bit(&data->flags, PDC_HANDLING_IRQ)) {
			union connector_status_t *status =
				(union connector_status_t *)data->user_buf;
			if ((status->conn_partner_flags &
			     CONNECTOR_PARTNER_FLAG_ALTERNATE_MODE) &&
			    !status->raw_conn_status_change_bits) {
				union conn_status_change_bits_t
					status_change_bits = { 0 };
				status_change_bits.attention = 1;
				status->raw_conn_status_change_bits =
					status_change_bits.raw_value;
			}
		}
		break;
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

static void st_error_recovery_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);
	data->error_recovery_counter++;
	data->error_recovery_delay_counter = 0;

	/*TODO: ADD ERROR RECOVERY CODE */
}

static void st_error_recovery_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	/* Don't continue trying if we are suspending communication */
	if (check_comms_suspended()) {
		set_state(data, ST_SUSPENDED);
		return;
	}

	if (data->error_recovery_counter >= N_MAX_ERROR_RECOVERY_COUNT) {
		set_state(data, ST_DISABLE);
		return;
	}

	/* Current recovery is just delaying and performing a PDC init */
	/* TODO(b/325633531): Investigate using timestamps instead of counters
	 */
	if (data->error_recovery_delay_counter < N_ERROR_RECOVERY_DELAY_COUNT) {
		data->error_recovery_delay_counter++;
		return;
	}

	/* Perform PDC Init */
	perform_pdc_init(data);
}

static void st_disable_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);
	/* If entering from ST_INIT state */
	data->init_done = true;
	data->error_status.port_disabled = 1;
}

static void st_disable_run(void *o)
{
	/* Stay here until reset */
}

static void st_suspended_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);
}

static void st_suspended_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	/* Stay here while suspended */
	if (check_comms_suspended()) {
		return;
	}

	/* Otherwise, return back to init state...
	 *
	 * Start the driver initialization routine to put everything
	 * back into a known state (This includes a driver + PDC reset)
	 */
	perform_pdc_init(data);
}

/* Populate cmd state table */
static const struct smf_state states[] = {
	[ST_INIT] =
		SMF_CREATE_STATE(st_init_entry, st_init_run, NULL, NULL, NULL),
	[ST_IDLE] =
		SMF_CREATE_STATE(st_idle_entry, st_idle_run, NULL, NULL, NULL),
	[ST_WRITE] = SMF_CREATE_STATE(st_write_entry, st_write_run, NULL, NULL,
				      NULL),
	[ST_PING_STATUS] = SMF_CREATE_STATE(
		st_ping_status_entry, st_ping_status_run, NULL, NULL, NULL),
	[ST_READ] =
		SMF_CREATE_STATE(st_read_entry, st_read_run, NULL, NULL, NULL),
	[ST_ERROR_RECOVERY] = SMF_CREATE_STATE(st_error_recovery_entry,
					       st_error_recovery_run, NULL,
					       NULL, NULL),
	[ST_DISABLE] = SMF_CREATE_STATE(st_disable_entry, st_disable_run, NULL,
					NULL, NULL),
	[ST_SUSPENDED] = SMF_CREATE_STATE(st_suspended_entry, st_suspended_run,
					  NULL, NULL, NULL),

};

/**
 * @brief Helper method for setting up a command call.
 * @param dev PDC device pointer
 * @param cmd Command to execute
 * @param buf Command payload to copy into write buffer
 * @param len Length of paylaod buffer
 * @param user_buf Pointer to buffer where response data will be written.
 * @return 0 on success
 * @return -EBUSY if command is already pending.
 * @return -ECONNREFUSED if chip communication is disabled
 */
static int rts54_post_command_with_callback(const struct device *dev,
					    enum cmd_t cmd, const uint8_t *buf,
					    uint8_t len, uint8_t *user_buf,
					    struct pdc_callback *callback)
{
	struct pdc_data_t *data = dev->data;

	/* Return an error if chip communication is suspended */
	if (check_comms_suspended()) {
		return -ECONNREFUSED;
	}

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
	data->cc_cb_tmp = callback;

	/* If sending a raw UCSI command, byte[2] is the actual UCSI command
	 * being executed.
	 */
	if (cmd == CMD_RAW_UCSI && buf) {
		data->active_ucsi_cmd = data->wr_buf[2];
	}

	if (IS_ENABLED(CONFIG_USBC_PDC_TRACE_MSG)) {
		const struct pdc_config_t *cfg = dev->config;

		pdc_trace_msg_req(cfg->connector_number,
				  PDC_TRACE_CHIP_TYPE_RTS54XX, data->wr_buf,
				  data->wr_buf_len);
	}

	k_mutex_unlock(&data->mtx);
	/* Posting the event reduces latency to start executing the command. */
	k_event_post(&data->driver_event, RTS54XX_NEXT_STATE_READY);

	return 0;
}

static int rts54_post_command(const struct device *dev, enum cmd_t cmd,
			      const uint8_t *buf, uint8_t len,
			      uint8_t *user_buf)
{
	return rts54_post_command_with_callback(dev, cmd, buf, len, user_buf,
						NULL);
}

/**
 * @param offset Starting location in PD Status information payload.
 *               Note that offset values refer to the payload data
 *               following the byte-count byte present in all response
 *               messages. For example, the 4 PD status bytes are at
 *               offset 0, not 1.
 */
static int rts54_get_rtk_status(const struct device *dev, uint8_t offset,
				uint8_t len, enum cmd_t cmd, uint8_t *buf)
{
	if (buf == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		GET_RTK_STATUS.cmd, GET_RTK_STATUS.len, offset, 0x00, len,
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
				struct pdc_callback *callback)
{
	struct pdc_data_t *data = dev->data;

	data->cc_cb = callback;

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

static int rts54_set_retimer_update_mode(const struct device *dev, bool enable)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* 0: FW update starts, 1: FW update ends */
	enable = !enable;

	uint8_t payload[] = {
		SET_RETIMER_FW_UPDATE_MODE.cmd,
		SET_RETIMER_FW_UPDATE_MODE.len,
		SET_RETIMER_FW_UPDATE_MODE.sub,
		0x00,
		enable,
	};

	return rts54_post_command(dev, CMD_SET_RETIMER_FW_UPDATE_MODE, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_read_power_level(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/*
	 * TODO(b/326276531): The implementation of this command is not yet
	 * complete. The fields 'time to read power` and `time interval between
	 * readings` are not being set and need to be both passed into this
	 * function from the PDC subsys API and set below.
	 */
	uint8_t payload[] = {
		RTS_UCSI_READ_POWER_LEVEL.cmd,
		RTS_UCSI_READ_POWER_LEVEL.len,
		RTS_UCSI_READ_POWER_LEVEL.sub,
		0x00, /* Data Length --> set to 0x00 */
		0x00, /* Connector number  */
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

	if (get_state(data) == ST_DISABLE) {
		perform_pdc_init(data);
		return 0;
	}

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
		RTS_UCSI_PPM_RESET.cmd,
		RTS_UCSI_PPM_RESET.len,
		RTS_UCSI_PPM_RESET.sub,
		0x00,
	};

	return rts54_post_command(dev, CMD_PPM_RESET, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_connector_reset(const struct device *dev,
				 union connector_reset_t reset)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	uint8_t payload[] = { RTS_UCSI_CONNECTOR_RESET.cmd,
			      RTS_UCSI_CONNECTOR_RESET.len,
			      RTS_UCSI_CONNECTOR_RESET.sub, 0x00,
			      reset.raw_value };

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
	default:
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

	/*
	 * Apply the same value to both TPC Rp and PD Rp as 0 is a reserved
	 * value and without setting both fields, the command will fail.
	 *
	 * bits 1:0 reserved
	 * bits 3:2 TPC Rp
	 * bits 5:4 PD Rp
	 */
	byte |= (byte << 2);

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
		RTS_UCSI_GET_CAPABILITY.cmd,
		RTS_UCSI_GET_CAPABILITY.len,
		RTS_UCSI_GET_CAPABILITY.sub,
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
		RTS_UCSI_GET_CONNECTOR_CAPABILITY.cmd,
		RTS_UCSI_GET_CONNECTOR_CAPABILITY.len,
		RTS_UCSI_GET_CONNECTOR_CAPABILITY.sub,
		0x00, /* Data Length --> set to 0x00 */
		0x00, /* Connector number --> don't care for Realtek */
	};

	return rts54_post_command(dev, CMD_GET_CONNECTOR_CAPABILITY, payload,
				  ARRAY_SIZE(payload), (uint8_t *)caps);
}

static int rts54_get_connector_status(const struct device *dev,
				      union connector_status_t *cs)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (cs == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		RTS_UCSI_GET_CONNECTOR_STATUS.cmd,
		RTS_UCSI_GET_CONNECTOR_STATUS.len,
		RTS_UCSI_GET_CONNECTOR_STATUS.sub,
		0x00, /* Data Length --> set to 0x00 */
		0x00, /* Connector number --> don't care for Realtek */
	};

	return rts54_post_command(dev, CMD_GET_CONNECTOR_STATUS, payload,
				  ARRAY_SIZE(payload), (uint8_t *)cs);
}

static int rts54_get_cable_property(const struct device *dev,
				    union cable_property_t *cp)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (cp == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		RTS_UCSI_GET_CABLE_PROPERTY.cmd,
		RTS_UCSI_GET_CABLE_PROPERTY.len,
		RTS_UCSI_GET_CABLE_PROPERTY.sub,
		0x00,
		0x00,
	};

	return rts54_post_command(dev, CMD_GET_CABLE_PROPERTY, payload,
				  ARRAY_SIZE(payload), (uint8_t *)cp);
}

static int rts54_get_error_status(const struct device *dev,
				  union error_status_t *es)
{
	struct pdc_data_t *data = dev->data;

	if (es == NULL) {
		return -EINVAL;
	}

	/* Port is disabled. Return the last read error_status.
	 */
	if (get_state(data) == ST_DISABLE) {
		es->raw_value = data->error_status.raw_value;
		return 0;
	}

	if ((get_state(data) != ST_IDLE) && (get_state(data) != ST_INIT)) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		RTS_UCSI_GET_ERROR_STATUS.cmd,
		RTS_UCSI_GET_ERROR_STATUS.len,
		RTS_UCSI_GET_ERROR_STATUS.sub,
		0x00, /* Data Length --> set to 0x00 */
		0x00, /* Connector number --> don't care for Realtek */
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
			  enum pdo_source_t source, uint32_t *pdos)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	union get_pdos_t *get_pdo;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (pdos == NULL) {
		return -EINVAL;
	}

	/* b/366470065 - The vendor specific GET_PDO command fails to generate
	 * the appropriate PD message if the requested PDO type has not
	 * been received.
	 *
	 * Use the UCSI version which has the correct behavior.
	 */
	memset((uint8_t *)pdos, 0, sizeof(uint32_t) * num_pdos);

	uint8_t payload[] = {
		RTS_UCSI_GET_PDOS.cmd,
		RTS_UCSI_GET_PDOS.len,
		RTS_UCSI_GET_PDOS.sub,
		0x00, /* data length - must be zero */
		0x00,
		0x00,
		0x00,
	};

	BUILD_ASSERT(ARRAY_SIZE(payload) == sizeof(RTS_UCSI_GET_PDOS) +
						    /* length byte */ 1 +
						    sizeof(union get_pdos_t));

	get_pdo = (union get_pdos_t *)&payload[4];
	get_pdo->connector_number = cfg->connector_number + 1;
	get_pdo->pdo_source = source;
	get_pdo->pdo_offset = pdo_offset;
	get_pdo->number_of_pdos = num_pdos - 1;
	get_pdo->pdo_type = pdo_type;
	get_pdo->source_caps = CURRENT_SUPPORTED_SOURCE_CAPS;
	get_pdo->range = SPR_RANGE;

	return rts54_post_command(dev, CMD_GET_PDOS, payload,
				  ARRAY_SIZE(payload), (uint8_t *)pdos);
}

static int rts54_get_info(const struct device *dev, struct pdc_info_t *info,
			  bool live)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;

	if (info == NULL) {
		return -EINVAL;
	}

	/* If caller is OK with a non-live value and we have one, we can
	 * immediately return a cached value.
	 */
	if (!live) {
		k_mutex_lock(&data->mtx, K_FOREVER);

		/* Check FW ver and VID/PID fields for valid values to ensure
		 * we have a resident value.
		 */
		if (data->info.fw_version == PDC_FWVER_INVALID ||
		    data->info.vid_pid == PDC_VIDPID_INVALID) {
			k_mutex_unlock(&data->mtx);

			/* No cached value. Caller should request a live read */
			return -EAGAIN;
		}

		*info = data->info;
		k_mutex_unlock(&data->mtx);

		LOG_DBG("C%d: Use cached chip info (%u.%u.%u)",
			cfg->connector_number,
			PDC_FWVER_GET_MAJOR(data->info.fw_version),
			PDC_FWVER_GET_MINOR(data->info.fw_version),
			PDC_FWVER_GET_PATCH(data->info.fw_version));
		return 0;
	}

	/* Handle a live read */

	if ((get_state(data) != ST_IDLE) && (get_state(data) != ST_INIT)) {
		return -EBUSY;
	}

	/* Post a command and perform a chip operation */
	uint8_t payload[] = {
		GET_IC_STATUS.cmd, GET_IC_STATUS.len, 0, 0x00, 38,
	};

	LOG_DBG("C%d: Get live chip info", cfg->connector_number);

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

	uint8_t payload[] = {
		RTS_UCSI_GET_CONNECTOR_STATUS.cmd,
		RTS_UCSI_GET_CONNECTOR_STATUS.len,
		RTS_UCSI_GET_CONNECTOR_STATUS.sub,
		0x00, /* Data Length --> set to 0x00 */
		0x00, /* Connector number --> don't care for Realtek */
	};

	return rts54_post_command(dev, CMD_GET_VBUS_VOLTAGE, payload,
				  ARRAY_SIZE(payload), (uint8_t *)voltage);
}

static int rts54_set_ccom(const struct device *dev, enum ccom_t ccom)
{
	struct pdc_data_t *data = dev->data;
	uint16_t conn_opmode = 0;
	/*
	 * From bit 32, the first 7 bits are connector. The next 4 bits are for
	 * the CC operation mode.
	 */
	const uint8_t opmode_offset = 7;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	switch (ccom) {
	case CCOM_RP:
		conn_opmode = 1 << (opmode_offset + 0);
		break;
	case CCOM_RD:
		conn_opmode = 1 << (opmode_offset + 1);
		break;
	case CCOM_DRP:
		conn_opmode = 1 << (opmode_offset + 2);
		break;
	}

	uint8_t payload[] = {
		RTS_UCSI_SET_CCOM.cmd, RTS_UCSI_SET_CCOM.len,
		RTS_UCSI_SET_CCOM.sub, 0x00 /* data length */,
		conn_opmode & 0xff,    (conn_opmode >> 8) & 0xff,
	};

	return rts54_post_command(dev, CMD_SET_CCOM, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_set_drp_mode(const struct device *dev, enum drp_mode_t dm)
{
	struct pdc_data_t *data = dev->data;
	uint8_t opmode = 0;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* Set CSD mode to DRP */
	opmode = 0x01;
	switch (dm) {
	case DRP_NORMAL:
		/* No Try.Src or Try.Snk
		 * opmode |= (0 << 3);
		 */
		break;
	case DRP_TRY_SRC:
		opmode |= (1 << 3);
		break;
	case DRP_TRY_SNK:
		opmode |= (2 << 3);
		break;
	case DRP_INVALID:
	default:
		LOG_ERR("Invalid DRP mode: %d", dm);
		break;
	}

	/* We always want Accessory Support */
	opmode |= (1 << 2);

	uint8_t payload[] = {
		SET_TPC_CSD_OPERATION_MODE.cmd,
		SET_TPC_CSD_OPERATION_MODE.len,
		SET_TPC_CSD_OPERATION_MODE.sub,
		0x00,
		opmode,
	};

	return rts54_post_command(dev, CMD_SET_DRP_MODE, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_set_uor(const struct device *dev, union uor_t uor)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		RTS_UCSI_SET_UOR.cmd, RTS_UCSI_SET_UOR.len,
		RTS_UCSI_SET_UOR.sub, 0x00,
		uor.raw_value & 0xff, (uor.raw_value >> 8) & 0xff
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
		RTS_UCSI_SET_PDR.cmd, RTS_UCSI_SET_PDR.len,
		RTS_UCSI_SET_PDR.sub, 0x00,
		pdr.raw_value & 0xff, (pdr.raw_value >> 8) & 0xff
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
}

static int rts54_set_frs(const struct device *dev, bool enable)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	uint8_t payload[] = {
		[0] = RTS_SET_FRS_FUNCTION.cmd,
		[1] = RTS_SET_FRS_FUNCTION.len,
		[2] = RTS_SET_FRS_FUNCTION.sub,
		[3] = 0x00,
		[4] = enable,
	};

	return rts54_post_command(dev, CMD_SET_FRS_FUNCTION, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_get_identity_discovery(const struct device *dev,
					bool *disc_state)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (disc_state == NULL) {
		return -EINVAL;
	}

	return rts54_get_rtk_status(dev, 0, 14, CMD_GET_IDENTITY_DISCOVERY,
				    (uint8_t *)disc_state);
}

static int rts54_is_vconn_sourcing(const struct device *dev,
				   bool *vconn_sourcing)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (vconn_sourcing == NULL) {
		return -EINVAL;
	}

	return rts54_get_rtk_status(dev, 0, 11, CMD_GET_IS_VCONN_SOURCING,
				    (uint8_t *)vconn_sourcing);
}

static int rts54_get_pch_data_status(const struct device *dev, uint8_t port_num,
				     uint8_t *status_reg)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (status_reg == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		GET_PCH_DATA_STATUS.cmd,
		GET_PCH_DATA_STATUS.len,
		GET_PCH_DATA_STATUS.sub,
		port_num,
	};

	rts54_post_command(dev, CMD_GET_PCH_DATA_STATUS, payload,
			   ARRAY_SIZE(payload), status_reg);
	return 0;
}

static bool rts54_is_init_done(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	return data->init_done;
}

static int rts54_get_vdo(const struct device *dev, union get_vdo_t vdo_req,
			 uint8_t *vdo_req_list, uint32_t *vdo)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (vdo == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = {
		GET_VDO.cmd,
		GET_VDO.len + vdo_req.num_vdos,
		GET_VDO.sub,
		0x00, /*  3: Port num */
		vdo_req.raw_value, /*  4: Origin + number of VDOs */
		0x00, /*  5: VDO type 0 */
		0x00, /*  6: VDO type 1 */
		0x00, /*  7: VDO type 2 */
		0x00, /*  8: VDO type 3 */
		0x00, /*  9: VDO type 4 */
		0x00, /* 10: VDO type 5 */
		0x00, /* 11: VDO type 6 */
		0x00, /* 12: VDO type 7 */
	};

	/* Copy the list of VDO types being requested in the cmd message */
	memcpy(&payload[5], vdo_req_list, vdo_req.num_vdos);

	return rts54_post_command(dev, CMD_GET_VDO, payload,
				  GET_VDO.len + vdo_req.num_vdos + 2,
				  (uint8_t *)vdo);
}

/** Allow 3 seconds for the driver to suspend itself. */
#define SUSPEND_TIMEOUT_USEC (3 * USEC_PER_SEC)

static int rts54_set_comms_state(const struct device *dev, bool comms_active)
{
	struct pdc_data_t *data = dev->data;

	if (comms_active) {
		/* Re-enable communications. Clearing the suspend flag will
		 * trigger a reset. Note: if the driver is in the disabled
		 * state due to a previous comms failure, it will remain
		 * disabled. (Thus, suspending/resuming comms on a disabled
		 * PDC driver is a no-op)
		 */
		enable_comms();

	} else {
		/* Request communication to be stopped. This allows in-progress
		 * operations to complete first.
		 */
		suspend_comms();

		if (get_state(data) == ST_DISABLE) {
			/* The driver is already permanently shut down. */
			return 0;
		}

		/* Wait for driver to enter the suspended state */
		if (!WAIT_FOR((get_state(data) == ST_SUSPENDED),
			      SUSPEND_TIMEOUT_USEC,
			      k_sleep(K_MSEC(T_PING_STATUS)))) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int rts54_set_pdo(const struct device *dev, enum pdo_type_t type,
			 uint32_t *pdo, int count)
{
	struct pdc_data_t *data = dev->data;
	uint8_t pdo_info;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/*
	 * TODO(b/319643480): Current implementation only supports setting the
	 * first SNK or SRC CAP.
	 */
	if (count != 1) {
		count = 1;
		LOG_WRN("rts54xx: set_pdos only sets the first PDO passed in");
	}

	pdo_info = (count & 0x7) | (type << 3);

	uint8_t payload[] = {
		SET_PDO.cmd,   SET_PDO.len + sizeof(uint32_t) * count,
		SET_PDO.sub,   0x00,
		pdo_info,      BYTE0(pdo[0]),
		BYTE1(pdo[0]), BYTE2(pdo[0]),
		BYTE3(pdo[0]),
	};

	return rts54_post_command(dev, CMD_SET_PDO, payload,
				  ARRAY_SIZE(payload), NULL);
}

#define SMBUS_MAX_BLOCK_SIZE 32

static int rts54_execute_ucsi_cmd(const struct device *dev,
				  uint8_t ucsi_command, uint8_t data_size,
				  uint8_t *command_specific,
				  uint8_t *lpm_data_out,
				  struct pdc_callback *callback)
{
	struct pdc_data_t *data = dev->data;
	uint8_t cmd_buffer[SMBUS_MAX_BLOCK_SIZE];

	if (get_state(data) != ST_IDLE)
		return -EBUSY;

	cmd_buffer[0] = REALTEK_PD_COMMAND;
	cmd_buffer[1] = data_size + 2;
	cmd_buffer[2] = ucsi_command; /* sub-cmd */
	cmd_buffer[3] = 0;
	memcpy(&cmd_buffer[4], command_specific, data_size);

	/* Convert standard UCSI command to Realtek vendor specific formats. */
	switch (ucsi_command) {
	case UCSI_ACK_CC_CI: {
		/* Note: Change acknowledgements should be intercepted by the
		 * PPM and handled by the pdc_api instead.
		 */
		data_size = 5;
		memset(cmd_buffer, 0, ACK_CC_CI.len + 2);
		cmd_buffer[0] = ACK_CC_CI.cmd;
		cmd_buffer[1] = ACK_CC_CI.len;

		break;
	}
	default:
		break;
	}

	return rts54_post_command_with_callback(dev, CMD_RAW_UCSI, cmd_buffer,
						data_size + 4, lpm_data_out,
						callback);
}

static int rts54_manage_callback(const struct device *dev,
				 struct pdc_callback *callback, bool set)
{
	struct pdc_data_t *const data = dev->data;

	return pdc_manage_callbacks(&data->ci_cb_list, callback, set);
}

static int rts54_ack_cc_ci(const struct device *dev,
			   union conn_status_change_bits_t ci, bool cc,
			   uint16_t vendor_defined)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	uint8_t payload[] = { ACK_CC_CI.cmd,
			      ACK_CC_CI.len,
			      ACK_CC_CI.sub,
			      0x00,
			      BYTE0(ci.raw_value),
			      BYTE1(ci.raw_value),
			      BYTE0(vendor_defined),
			      BYTE1(vendor_defined),
			      cc };

	return rts54_post_command(dev, CMD_ACK_CC_CI, payload,
				  ARRAY_SIZE(payload), NULL);
}

static int rts54_get_lpm_ppm_info(const struct device *dev,
				  struct lpm_ppm_info_t *info)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (info == NULL) {
		return -EINVAL;
	}

	uint8_t payload[] = { RTS_UCSI_GET_LPM_PPM_INFO.cmd,
			      RTS_UCSI_GET_LPM_PPM_INFO.len,
			      RTS_UCSI_GET_LPM_PPM_INFO.sub, 0x00, 0x00 };

	return rts54_post_command(dev, CMD_GET_LPM_PPM_INFO, payload,
				  ARRAY_SIZE(payload), (uint8_t *)info);
}

static const struct pdc_driver_api_t pdc_driver_api = {
	.is_init_done = rts54_is_init_done,
	.get_ucsi_version = rts54_get_ucsi_version,
	.reset = rts54_pdc_reset,
	.connector_reset = rts54_connector_reset,
	.get_capability = rts54_get_capability,
	.get_connector_capability = rts54_get_connector_capability,
	.set_ccom = rts54_set_ccom,
	.set_drp_mode = rts54_set_drp_mode,
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
	.update_retimer = rts54_set_retimer_update_mode,
	.get_cable_property = rts54_get_cable_property,
	.get_vdo = rts54_get_vdo,
	.get_identity_discovery = rts54_get_identity_discovery,
	.set_comms_state = rts54_set_comms_state,
	.is_vconn_sourcing = rts54_is_vconn_sourcing,
	.set_pdos = rts54_set_pdo,
	.get_pch_data_status = rts54_get_pch_data_status,
	.execute_ucsi_cmd = rts54_execute_ucsi_cmd,
	.manage_callback = rts54_manage_callback,
	.ack_cc_ci = rts54_ack_cc_ci,
	.get_lpm_ppm_info = rts54_get_lpm_ppm_info,
	.set_frs = rts54_set_frs,
};

static void pdc_interrupt_callback(const struct device *dev,
				   struct gpio_callback *cb, uint32_t pins)
{
	k_event_post(irq_event, RTS54XX_IRQ_EVENT);
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

	k_event_init(&data->driver_event);

	if (!irq_init_done) {
		irq_shared_port = cfg->irq_gpios.port;
		irq_share_pin = cfg->irq_gpios.pin;

		irq_event = &data->driver_event;

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

		/* Trigger IRQ on startup to read any pending interrupts */
		k_event_post(irq_event, RTS54XX_IRQ_EVENT);
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
	data->error_recovery_counter = 0;
	data->init_retry_counter = 0;

	pdc_data[cfg->connector_number] = data;

	/* Set initial state */
	data->init_local_state = INIT_PDC_ENABLE;
	smf_set_initial(SMF_CTX(data), &states[ST_INIT]);

	/* Create the thread for this port */
	cfg->create_thread(dev);

	LOG_INF("C%d: Realtek RTS545x PDC DRIVER", cfg->connector_number);

	return 0;
}

static void rts54xx_thread(void *dev, void *unused1, void *unused2)
{
	const struct pdc_config_t *cfg = ((const struct device *)dev)->config;
	struct pdc_data_t *data = ((const struct device *)dev)->data;
	uint32_t events;
	bool irq_pending_for_idle = false;

	while (1) {
		smf_run_state(SMF_CTX(data));

		events = k_event_wait(&data->driver_event,
				      RTS54XX_IRQ_EVENT |
					      RTS54XX_NEXT_STATE_READY,
				      false, K_MSEC(T_PING_STATUS));

		if (events & RTS54XX_IRQ_EVENT) {
			irq_pending_for_idle = true;
		}

		k_event_clear(&data->driver_event, events);

		/* We only handle irq on idle. */
		if (get_state(data) == ST_IDLE && irq_pending_for_idle) {
			irq_pending_for_idle = false;
			if (check_comms_suspended()) {
				LOG_INF("C%d: Ignoring interrupt",
					cfg->connector_number);
				continue;
			}
			handle_irqs(data);
		}
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
		.bits.command_completed = 1,                                  \
		.bits.external_supply_change = 1,                             \
		.bits.power_operation_mode_change = 1,                        \
		.bits.attention = 0,                                          \
		.bits.fw_update_request = 0,                                  \
		.bits.provider_capability_change_supported = 1,               \
		.bits.negotiated_power_level_change = 1,                      \
		.bits.pd_reset_complete = 1,                                  \
		.bits.support_cam_change = 1,                                 \
		.bits.battery_charging_status_change = 1,                     \
		.bits.security_request_from_port_partner = 0,                 \
		.bits.connector_partner_change = 1,                           \
		.bits.power_direction_change = 1,                             \
		.bits.set_retimer_mode = 0,                                   \
		.bits.connect_change = 1,                                     \
		.bits.error = 1,                                              \
		.create_thread = create_thread_##inst,                        \
		.no_fw_update = DT_INST_PROP(inst, no_fw_update),             \
	};                                                                    \
                                                                              \
	DEVICE_DT_INST_DEFINE(inst, pdc_init, NULL, &pdc_data_##inst,         \
			      &pdc_config##inst, POST_KERNEL,                 \
			      CONFIG_APPLICATION_INIT_PRIORITY,               \
			      &pdc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PDC_DEFINE)

#ifdef CONFIG_ZTEST

struct pdc_data_t;

#define PDC_TEST_DEFINE(inst) &pdc_data_##inst,

static struct pdc_data_t *pdc_data[] = { DT_INST_FOREACH_STATUS_OKAY(
	PDC_TEST_DEFINE) };

/*
 * Wait for drivers to become idle.
 */
/* LCOV_EXCL_START */
bool pdc_rts54xx_test_idle_wait(void)
{
	int num_finished;

	/* Wait for up to 20 * 100ms for all drivers to become idle. */
	for (int i = 0; i < 20; i++) {
		num_finished = 0;

		k_msleep(100);
		for (int port = 0; port < ARRAY_SIZE(pdc_data); port++) {
			if (get_state(pdc_data[port]) == ST_IDLE &&
			    pdc_data[port]->cmd == CMD_NONE) {
				num_finished++;
			}
		}

		if (num_finished == ARRAY_SIZE(pdc_data)) {
			return true;
		}
	}

	return false;
}
/* LCOV_EXCL_STOP */

#endif
