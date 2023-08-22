/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "dma.h"
#include "hooks.h"
#include "i2c.h"
#include "link_defs.h"
#include "registers.h"
#include "timer.h"
#include "usb_descriptor.h"
#include "usb_power.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)

static int usb_power_init_inas(struct usb_power_config const *config);
static int usb_power_read(struct usb_power_config const *config);
static int usb_power_write_line(struct usb_power_config const *config);

void usb_power_deferred_rx(struct usb_power_config const *config)
{
	int rx_count = rx_ep_pending(config->endpoint);

	/* Handle an incoming command if available */
	if (rx_count)
		usb_power_read(config);
}

void usb_power_deferred_tx(struct usb_power_config const *config)
{
	struct usb_power_state *state = config->state;

	if (!tx_ep_is_ready(config->endpoint))
		return;

	/* We've replied, set up the next read. */
	if (!rx_ep_is_active(config->endpoint)) {
		/* Remove any active dma region from output buffer */
		state->reports_xmit_active = state->reports_tail;

		/* Wait for the next command */
		usb_read_ep(config->endpoint, config->ep->out_databuffer_max,
			    config->ep->out_databuffer);
		return;
	}
}

/* Reset stream */
void usb_power_event(struct usb_power_config const *config,
		     enum usb_ep_event evt)
{
	if (evt != USB_EVENT_RESET)
		return;

	config->ep->out_databuffer = config->state->rx_buf;
	config->ep->out_databuffer_max = sizeof(config->state->rx_buf);
	config->ep->in_databuffer = config->state->tx_buf;
	config->ep->in_databuffer_max = sizeof(config->state->tx_buf);

	epN_reset(config->endpoint);

	/* Flush any queued data */
	hook_call_deferred(config->ep->rx_deferred, 0);
	hook_call_deferred(config->ep->tx_deferred, 0);
}

/* Write one or more power records to USB */
static int usb_power_write_line(struct usb_power_config const *config)
{
	struct usb_power_state *state = config->state;
	struct usb_power_report *r =
		(struct usb_power_report *)(state->reports_data_area +
					    (USB_POWER_RECORD_SIZE(
						     state->ina_count) *
					     state->reports_tail));
	/* status + size + timestamps + power list */
	size_t bytes = USB_POWER_RECORD_SIZE(state->ina_count);

	/* TODO(nsanders): TX can't handle more than about 512 bytes.
	 * Unfortunately the docs for this chip can't be found, so why is kind
	 * of a mystery. It may have to do with the tx fifo size or DMA
	 * continuation. For now just don't send more than 512 bytes.
	 */
	int max_records = 512 / bytes;

	/* Check if queue has active data. */
	if (config->state->reports_head != config->state->reports_tail) {
		int recordcount = 1;

		/* We'll concatenate all the upcoming recrds. */
		if (config->state->reports_tail < config->state->reports_head)
			recordcount = config->state->reports_head -
				      config->state->reports_tail;
		else
			recordcount =
				state->max_cached - config->state->reports_tail;

		if (recordcount > max_records) {
			CPRINTS("Truncate records read to %d from %d",
				max_records, recordcount);
			recordcount = max_records;
		}

		state->reports_xmit_active = state->reports_tail;
		state->reports_tail =
			(state->reports_tail + recordcount) % state->max_cached;

		usb_write_ep(config->endpoint, bytes * recordcount, r);
		return bytes * recordcount;
	}

	return 0;
}

static int usb_power_state_reset(struct usb_power_config const *config)
{
	struct usb_power_state *state = config->state;

	state->state = USB_POWER_STATE_OFF;
	state->reports_head = 0;
	state->reports_tail = 0;
	state->reports_xmit_active = 0;

	CPRINTS("[RESET] STATE -> OFF");
	return USB_POWER_SUCCESS;
}

static int usb_power_state_stop(struct usb_power_config const *config)
{
	struct usb_power_state *state = config->state;

	/* Only a valid transition from CAPTURING */
	if (state->state != USB_POWER_STATE_CAPTURING) {
		CPRINTS("[STOP] Error not capturing.");
		return USB_POWER_ERROR_NOT_CAPTURING;
	}

	state->state = USB_POWER_STATE_OFF;
	state->reports_head = 0;
	state->reports_tail = 0;
	state->reports_xmit_active = 0;
	state->stride_bytes = 0;
	CPRINTS("[STOP] STATE: CAPTURING -> OFF");
	return USB_POWER_SUCCESS;
}

static int usb_power_state_start(struct usb_power_config const *config,
				 union usb_power_command_data *cmd, int count)
{
	struct usb_power_state *state = config->state;
	int integration_us = cmd->start.integration_us;
	int ret;

	if (state->state != USB_POWER_STATE_SETUP) {
		CPRINTS("[START] Error not setup.");
		return USB_POWER_ERROR_NOT_SETUP;
	}

	if (count != sizeof(struct usb_power_command_start)) {
		CPRINTS("[START] Error count %d is not %d", (int)count,
			sizeof(struct usb_power_command_start));
		return USB_POWER_ERROR_READ_SIZE;
	}

	if (integration_us == 0) {
		CPRINTS("[START] integration_us cannot be 0");
		return USB_POWER_ERROR_UNKNOWN;
	}

	/* Calculate the reports array */
	state->stride_bytes = USB_POWER_RECORD_SIZE(state->ina_count);
	state->max_cached = USB_POWER_MAX_CACHED(state->ina_count);

	state->integration_us = integration_us;
	ret = usb_power_init_inas(config);

	if (ret)
		return USB_POWER_ERROR_INVAL;

	state->state = USB_POWER_STATE_CAPTURING;
	CPRINTS("[START] STATE: SETUP -> CAPTURING %dus", integration_us);

	/* Find our starting time. */
	config->state->base_time = get_time().val;

	hook_call_deferred(config->deferred_cap, state->integration_us);
	return USB_POWER_SUCCESS;
}

static int usb_power_state_settime(struct usb_power_config const *config,
				   union usb_power_command_data *cmd, int count)
{
	if (count != sizeof(struct usb_power_command_settime)) {
		CPRINTS("[SETTIME] Error: count %d is not %d", (int)count,
			sizeof(struct usb_power_command_settime));
		return USB_POWER_ERROR_READ_SIZE;
	}

	/* Find the offset between microcontroller clock and host clock. */
	if (cmd->settime.time)
		config->state->wall_offset = cmd->settime.time - get_time().val;
	else
		config->state->wall_offset = 0;

	return USB_POWER_SUCCESS;
}

static int usb_power_state_addina(struct usb_power_config const *config,
				  union usb_power_command_data *cmd, int count)
{
	struct usb_power_state *state = config->state;
	struct usb_power_ina_cfg *ina;
	int i;

	/* Only valid from OFF or SETUP */
	if ((state->state != USB_POWER_STATE_OFF) &&
	    (state->state != USB_POWER_STATE_SETUP)) {
		CPRINTS("[ADDINA] Error incorrect state.");
		return USB_POWER_ERROR_NOT_SETUP;
	}

	if (count != sizeof(struct usb_power_command_addina)) {
		CPRINTS("[ADDINA] Error count %d is not %d", (int)count,
			sizeof(struct usb_power_command_addina));
		return USB_POWER_ERROR_READ_SIZE;
	}

	if (state->ina_count >= USB_POWER_MAX_READ_COUNT) {
		CPRINTS("[ADDINA] Error INA list full");
		return USB_POWER_ERROR_FULL;
	}

	/* Transition to SETUP state if necessary and clear INA data */
	if (state->state == USB_POWER_STATE_OFF) {
		state->state = USB_POWER_STATE_SETUP;
		state->ina_count = 0;
	}

	if ((cmd->addina.type < USBP_INA231_POWER) ||
	    (cmd->addina.type > USBP_INA231_SHUNTV)) {
		CPRINTS("[ADDINA] Error INA type 0x%x invalid",
			(int)(cmd->addina.type));
		return USB_POWER_ERROR_INVAL;
	}

	if (cmd->addina.rs == 0) {
		CPRINTS("[ADDINA] Error INA resistance cannot be zero!");
		return USB_POWER_ERROR_INVAL;
	}

	/* Select INA to configure */
	ina = state->ina_cfg + state->ina_count;

	ina->port = cmd->addina.port;
	ina->addr_flags = cmd->addina.addr_flags;
	ina->rs = cmd->addina.rs;
	ina->type = cmd->addina.type;

	/*
	 * INAs can be shared, in that they will have various values
	 * (and therefore registers) read from them each cycle, including
	 * power, voltage, current. If only a single value is read,
	 * we an use i2c_readagain for faster transactions as we don't
	 * have to respecify the address.
	 */
	ina->shared = 0;
#ifdef USB_POWER_VERBOSE
	ina->shared = 1;
#endif

	/* Check if shared with previously configured INAs. */
	for (i = 0; i < state->ina_count; i++) {
		struct usb_power_ina_cfg *tmp = state->ina_cfg + i;

		if ((tmp->port == ina->port) &&
		    (tmp->addr_flags == ina->addr_flags)) {
			ina->shared = 1;
			tmp->shared = 1;
		}
	}

	state->ina_count += 1;
	return USB_POWER_SUCCESS;
}

static int usb_power_read(struct usb_power_config const *config)
{
	/*
	 * If there is a USB packet waiting we process it and generate a
	 * response.
	 */
	uint8_t count = rx_ep_pending(config->endpoint);
	uint8_t result = USB_POWER_SUCCESS;
	union usb_power_command_data *cmd =
		(union usb_power_command_data *)config->ep->out_databuffer;

	struct usb_power_state *state = config->state;
	struct dwc_usb_ep *ep = config->ep;

	/* Bytes to return */
	int in_msgsize = 1;

	if (count < 2)
		return EC_ERROR_INVAL;

	/* State machine. */
	switch (cmd->command) {
	case USB_POWER_CMD_RESET:
		result = usb_power_state_reset(config);
		break;

	case USB_POWER_CMD_STOP:
		result = usb_power_state_stop(config);
		break;

	case USB_POWER_CMD_START:
		result = usb_power_state_start(config, cmd, count);
		if (result == USB_POWER_SUCCESS) {
			/* Send back actual integration time. */
			ep->in_databuffer[1] = (state->integration_us >> 0) &
					       0xff;
			ep->in_databuffer[2] = (state->integration_us >> 8) &
					       0xff;
			ep->in_databuffer[3] = (state->integration_us >> 16) &
					       0xff;
			ep->in_databuffer[4] = (state->integration_us >> 24) &
					       0xff;
			in_msgsize += 4;
		}
		break;

	case USB_POWER_CMD_ADDINA:
		result = usb_power_state_addina(config, cmd, count);
		break;

	case USB_POWER_CMD_SETTIME:
		result = usb_power_state_settime(config, cmd, count);
		break;

	case USB_POWER_CMD_NEXT:
		if (state->state == USB_POWER_STATE_CAPTURING) {
			int ret;

			ret = usb_power_write_line(config);
			if (ret)
				return EC_SUCCESS;

			result = USB_POWER_ERROR_BUSY;
		} else {
			CPRINTS("[STOP] Error not capturing.");
			result = USB_POWER_ERROR_NOT_CAPTURING;
		}
		break;

	default:
		CPRINTS("[ERROR] Unknown command 0x%04x", (int)cmd->command);
		result = USB_POWER_ERROR_UNKNOWN;
		break;
	}

	/* Return result code if applicable. */
	ep->in_databuffer[0] = result;

	usb_write_ep(config->endpoint, in_msgsize, ep->in_databuffer);

	return EC_SUCCESS;
}

/******************************************************************************
 * INA231 interface.
 * List the registers and fields here.
 * TODO(nsanders): combine with the currently incompatible common INA drivers.
 */

#define INA231_REG_CONF 0
#define INA231_REG_RSHV 1
#define INA231_REG_BUSV 2
#define INA231_REG_PWR 3
#define INA231_REG_CURR 4
#define INA231_REG_CAL 5
#define INA231_REG_EN 6

#define INA231_CONF_AVG(val) (((int)(val & 0x7)) << 9)
#define INA231_CONF_BUS_TIME(val) (((int)(val & 0x7)) << 6)
#define INA231_CONF_SHUNT_TIME(val) (((int)(val & 0x7)) << 3)
#define INA231_CONF_MODE(val) (((int)(val & 0x7)) << 0)
#define INA231_MODE_OFF 0x0
#define INA231_MODE_SHUNT 0x5
#define INA231_MODE_BUS 0x6
#define INA231_MODE_BOTH 0x7

int reg_type_mapping(enum usb_power_ina_type ina_type)
{
	switch (ina_type) {
	case USBP_INA231_POWER:
		return INA231_REG_PWR;
	case USBP_INA231_BUSV:
		return INA231_REG_BUSV;
	case USBP_INA231_CURRENT:
		return INA231_REG_CURR;
	case USBP_INA231_SHUNTV:
		return INA231_REG_RSHV;

	default:
		return INA231_REG_CONF;
	}
}

uint16_t ina2xx_readagain(uint8_t port, uint16_t addr_flags)
{
	int res;
	uint16_t val;

	res = i2c_xfer(port, addr_flags, NULL, 0, (uint8_t *)&val,
		       sizeof(uint16_t));

	if (res) {
		CPRINTS("INA2XX I2C readagain failed p:%d a:%02x", (int)port,
			(int)I2C_STRIP_FLAGS(addr_flags));
		return 0x0bad;
	}
	return (val >> 8) | ((val & 0xff) << 8);
}

uint16_t ina2xx_read(uint8_t port, uint16_t addr_flags, uint8_t reg)
{
	int res;
	int val;

	res = i2c_read16(port, addr_flags, reg, &val);
	if (res) {
		CPRINTS("INA2XX I2C read failed p:%d a:%02x, r:%02x", (int)port,
			(int)I2C_STRIP_FLAGS(addr_flags), (int)reg);
		return 0x0bad;
	}
	return (val >> 8) | ((val & 0xff) << 8);
}

int ina2xx_write(uint8_t port, uint16_t addr_flags, uint8_t reg, uint16_t val)
{
	int res;
	uint16_t be_val = (val >> 8) | ((val & 0xff) << 8);

	res = i2c_write16(port, addr_flags, reg, be_val);
	if (res)
		CPRINTS("INA2XX I2C write failed");
	return res;
}

/******************************************************************************
 * Background tasks
 *
 * Here we setup the INAs and read them at the specified interval.
 * INA samples are stored in a ringbuffer that can be fetched using the
 * USB commands.
 */

/* INA231 integration and averaging time presets, indexed by register value */
#define NELEMS(x) (sizeof(x) / sizeof((x)[0]))
static const int average_settings[] = { 1, 4, 16, 64, 128, 256, 512, 1024 };
static const int conversion_time_us[] = { 140,	204,  332,  588,
					  1100, 2116, 4156, 8244 };

static int usb_power_init_inas(struct usb_power_config const *config)
{
	struct usb_power_state *state = config->state;
	int i;
	int shunt_time = 0;
	int avg = 0;
	int target_us = state->integration_us;

	if (state->state != USB_POWER_STATE_SETUP) {
		CPRINTS("[ERROR] usb_power_init_inas while not SETUP");
		return -1;
	}

	/* Find an INA preset integration time less than specified */
	while (shunt_time < (NELEMS(conversion_time_us) - 1)) {
		if (conversion_time_us[shunt_time + 1] > target_us)
			break;
		shunt_time++;
	}

	/* Find an averaging setting from the INA presets that fits. */
	while (avg < (NELEMS(average_settings) - 1)) {
		if ((conversion_time_us[shunt_time] *
		     average_settings[avg + 1]) > target_us)
			break;
		avg++;
	}

	state->integration_us =
		conversion_time_us[shunt_time] * average_settings[avg];

	for (i = 0; i < state->ina_count; i++) {
		int value;
		int ret;
		struct usb_power_ina_cfg *ina = state->ina_cfg + i;

#ifdef USB_POWER_VERBOSE
		{
			int conf, cal;

			conf = ina2xx_read(ina->port, ina->addr_flags,
					   INA231_REG_CONF);
			cal = ina2xx_read(ina->port, ina->addr_flags,
					  INA231_REG_CAL);
			CPRINTS("[CAP] %d (%d,0x%02x): conf:%x, cal:%x", i,
				ina->port, I2C_STRIP_FLAGS(ina->addr_flags),
				conf, cal);
		}
#endif
		/*
		 * Calculate INA231 Calibration register
		 * CurrentLSB = uA per div = 80mV / (Rsh * 2^15)
		 * CurrentLSB 100x uA = 100x 80000000nV / (Rsh mOhm * 0x8000)
		 */
		/* TODO: allow voltage readings if no sense resistor. */
		if (ina->rs == 0)
			return -1;

		ina->scale = (100 * (80000000 / 0x8000)) / ina->rs;

		/*
		 * CAL = .00512 / (CurrentLSB * Rsh)
		 * CAL = 5120000 / (uA * mOhm)
		 */
		if (ina->scale == 0)
			return -1;
		value = (5120000 * 100) / (ina->scale * ina->rs);
		ret = ina2xx_write(ina->port, ina->addr_flags, INA231_REG_CAL,
				   value);
		if (ret != EC_SUCCESS) {
			CPRINTS("[CAP] usb_power_init_inas CAL FAIL: %d", ret);
			return ret;
		}
#ifdef USB_POWER_VERBOSE
		{
			int actual;

			actual = ina2xx_read(ina->port, ina->addr_flags,
					     INA231_REG_CAL);
			CPRINTS("[CAP] scale: %d uA/div, %d uW/div, cal:%x act:%x",
				ina->scale / 100, ina->scale * 25 / 100, value,
				actual);
		}
#endif
		/* Conversion time, shunt + bus, set average. */
		value = INA231_CONF_MODE(INA231_MODE_BOTH) |
			INA231_CONF_SHUNT_TIME(shunt_time) |
			INA231_CONF_BUS_TIME(shunt_time) | INA231_CONF_AVG(avg);
		ret = ina2xx_write(ina->port, ina->addr_flags, INA231_REG_CONF,
				   value);
		if (ret != EC_SUCCESS) {
			CPRINTS("[CAP] usb_power_init_inas CONF FAIL: %d", ret);
			return ret;
		}
#ifdef USB_POWER_VERBOSE
		{
			int actual;

			actual = ina2xx_read(ina->port, ina->addr_flags,
					     INA231_REG_CONF);
			CPRINTS("[CAP] %d (%d,0x%02x): conf:%x, act:%x", i,
				ina->port, I2C_STRIP_FLAGS(ina->addr_flags),
				value, actual);
		}
#endif
#ifdef USB_POWER_VERBOSE
		{
			int busv_mv = (ina2xx_read(ina->port, ina->addr_flags,
						   INA231_REG_BUSV) *
				       125) /
				      100;

			CPRINTS("[CAP] %d (%d,0x%02x): busv:%dmv", i, ina->port,
				I2C_STRIP_FLAGS(ina->addr_flags), busv_mv);
		}
#endif
		/* Initialize read from power register. This register address
		 * will be cached and all ina2xx_readagain() calls will read
		 * from the same address.
		 */
		ina2xx_read(ina->port, ina->addr_flags,
			    reg_type_mapping(ina->type));
#ifdef USB_POWER_VERBOSE
		CPRINTS("[CAP] %d (%d,0x%02x): type:%d", (int)(ina->type));
#endif
	}

	return EC_SUCCESS;
}

/*
 * Read each INA's power integration measurement.
 *
 * INAs recall the most recent address, so no register access write is
 * necessary, simply read 16 bits from each INA and fill the result into
 * the power record.
 *
 * If the power record ringbuffer is full, fail with USB_POWER_ERROR_OVERFLOW.
 */
static int usb_power_get_samples(struct usb_power_config const *config)
{
	uint64_t time = get_time().val;
	struct usb_power_state *state = config->state;
	struct usb_power_report *r =
		(struct usb_power_report *)(state->reports_data_area +
					    (USB_POWER_RECORD_SIZE(
						     state->ina_count) *
					     state->reports_head));
	struct usb_power_ina_cfg *inas = state->ina_cfg;
	int i;

	/* TODO(nsanders): Would we prefer to evict oldest? */
	if (((state->reports_head + 1) %
	     USB_POWER_MAX_CACHED(state->ina_count)) ==
	    state->reports_xmit_active) {
		return USB_POWER_ERROR_OVERFLOW;
	}

	r->status = USB_POWER_SUCCESS;
	r->size = state->ina_count;
	if (config->state->wall_offset)
		time = time + config->state->wall_offset;
	else
		time -= config->state->base_time;
	r->timestamp = time;

	for (i = 0; i < state->ina_count; i++) {
		int regval;
		struct usb_power_ina_cfg *ina = inas + i;

		/* Read INA231.
		 * ina2xx_read(ina->port, ina->addr, INA231_REG_PWR);
		 * Readagain cached this address so we'll save an I2C
		 * transaction.
		 */
		if (ina->shared)
			regval = ina2xx_read(ina->port, ina->addr_flags,
					     reg_type_mapping(ina->type));
		else
			regval = ina2xx_readagain(ina->port, ina->addr_flags);
		r->power[i] = regval;
#ifdef USB_POWER_VERBOSE
		{
			int current;
			int power;
			int voltage;
			int bvoltage;

			voltage = ina2xx_read(ina->port, ina->addr_flags,
					      INA231_REG_RSHV);
			bvoltage = ina2xx_read(ina->port, ina->addr_flags,
					       INA231_REG_BUSV);
			current = ina2xx_read(ina->port, ina->addr_flags,
					      INA231_REG_CURR);
			power = ina2xx_read(ina->port, ina->addr_flags,
					    INA231_REG_PWR);
			{
				int uV = ((int)voltage * 25) / 10;
				int mV = ((int)bvoltage * 125) / 100;
				int uA = (uV * 1000) / ina->rs;
				int CuA = (((int)current * ina->scale) / 100);
				int uW = (((int)power * ina->scale * 25) / 100);

				CPRINTS("[CAP] %d (%d,0x%02x): %dmV / %dmO = %dmA",
					i, ina->port,
					I2C_STRIP_FLAGS(ina->addr_flags),
					uV / 1000, ina->rs, uA / 1000);
				CPRINTS("[CAP] %duV %dmV %duA %dCuA "
					"%duW v:%04x, b:%04x, p:%04x",
					uV, mV, uA, CuA, uW, voltage, bvoltage,
					power);
			}
		}
#endif
	}

	/* Mark this slot as used. */
	state->reports_head = (state->reports_head + 1) %
			      USB_POWER_MAX_CACHED(state->ina_count);

	return EC_SUCCESS;
}

/*
 * This function is called every [interval] uS, and reads the accumulated
 * values of the INAs, and reschedules itself for the next interval.
 *
 * It will stop collecting frames if a ringbuffer overflow is
 * detected, or a stop request is seen..
 */
void usb_power_deferred_cap(struct usb_power_config const *config)
{
	int ret;
	/* TODO(nsanders): is there a better global state locaton for this? */
	static bool no_overflow = true;
	uint64_t timeout = get_time().val + config->state->integration_us;
	uint64_t timein;

	/* Exit if we have stopped capturing in the meantime. */
	if (config->state->state != USB_POWER_STATE_CAPTURING)
		return;

	/* Get samples for this timeslice */
	ret = usb_power_get_samples(config);
	if ((ret == USB_POWER_ERROR_OVERFLOW) && no_overflow) {
		CPRINTS("[CAP] %s: OVERFLOW", __func__);
		no_overflow = false;
	} else if ((ret != USB_POWER_ERROR_OVERFLOW) && !no_overflow) {
		CPRINTS("[CAP] %s: OVERFLOW CLEAR", __func__);
		no_overflow = true;
	}

	/* Calculate time remaining until next slice. */
	timein = get_time().val;
	if (timeout > timein)
		timeout = timeout - timein;
	else
		timeout = 0;

	/* Double check if we are still capturing. */
	if (config->state->state == USB_POWER_STATE_CAPTURING)
		hook_call_deferred(config->deferred_cap, timeout);
}
