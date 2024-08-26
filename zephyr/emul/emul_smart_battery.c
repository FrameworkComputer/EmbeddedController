/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "crc8.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_smart_battery.h"
#include "emul/emul_stub_device.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_ZTEST
#include <zephyr/ztest.h>
#endif

#define DT_DRV_COMPAT zephyr_smart_battery_emul

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(smart_battery);

/** Run-time data used by the emulator */
struct sbat_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;

	/** Data required to simulate battery */
	struct sbat_emul_bat_data bat;
	/** Command that should be handled next */
	int cur_cmd;
	/** Message buffer which is used to handle smb transactions */
	uint8_t msg_buf[MSG_BUF_LEN];
	/** Total bytes that were generated in response to smb read operation */
	int num_to_read;
};

/** Check description in emul_smart_battery.h */
struct sbat_emul_bat_data *sbat_emul_get_bat_data(const struct emul *emul)
{
	struct sbat_emul_data *data;

	data = emul->data;

	return &data->bat;
}

/** Check description in emul_smart_battery.h */
uint16_t sbat_emul_date_to_word(unsigned int day, unsigned int month,
				unsigned int year)
{
	year -= MANUFACTURE_DATE_YEAR_OFFSET;
	year <<= MANUFACTURE_DATE_YEAR_SHIFT;
	year &= MANUFACTURE_DATE_YEAR_MASK;
	month <<= MANUFACTURE_DATE_MONTH_SHIFT;
	month &= MANUFACTURE_DATE_MONTH_MASK;
	day <<= MANUFACTURE_DATE_DAY_SHIFT;
	day &= MANUFACTURE_DATE_DAY_MASK;

	return day | month | year;
}

/**
 * @brief Compute CRC from the beginning of the message
 *
 * @param addr Smart battery address on SMBus
 * @param read If message for which CRC is computed is read. For read message
 *             byte command and repeated address is added to CRC
 * @param cmd Command used in read message
 *
 * @return pec CRC from first bytes of message
 */
static uint8_t sbat_emul_pec_head(uint8_t addr, int read, uint8_t cmd)
{
	uint8_t pec;

	addr <<= 1;

	pec = cros_crc8(&addr, 1);
	if (!read) {
		return pec;
	}

	pec = cros_crc8_arg(&cmd, 1, pec);
	addr |= I2C_MSG_READ;
	pec = cros_crc8_arg(&addr, 1, pec);

	return pec;
}

/**
 * @brief Convert from 10mW power units to mA current under given mV voltage
 *
 * @param mw Power in 10mW units
 * @param mv Voltage in mV units
 *
 * @return Current in mA units
 */
static uint16_t sbat_emul_10mw_to_ma(int mw, int mv)
{
	/* Smart battery use 10mW units, convert to mW */
	mw *= 10;
	/* Multiple by 1000 to get mA instead of A */
	return 1000 * mw / mv;
}

/**
 * @brief Convert from mA current to 10mW power under given mV voltage
 *
 * @param ma Current in mA units
 * @param mv Voltage in mV units
 *
 * @return Power in 10mW units
 */
static uint16_t sbat_emul_ma_to_10mw(int ma, int mv)
{
	int mw;
	/* Divide by 1000 to get mW instead of uW */
	mw = ma * mv / 1000;
	/* Smart battery use 10mW units, convert to 10mW */
	return mw / 10;
}

/**
 * @brief Get time in minutes how long it will take to get given amount of
 *        charge at given current flow
 *
 * @param bat Pointer to battery data to set error code in case of
 *            over/under flow in time calculation
 * @param rate Rate of current in mAh
 * @param cap Required amount of charge in mA
 * @param time Pointer to memory where calculated time will be stored
 *
 * @return 0 on success
 * @return -EINVAL when over or under flow occurred
 */
static int sbat_emul_get_time_to_complete(struct sbat_emul_bat_data *bat,
					  int rate, int cap, uint16_t *ret_time)
{
	int time;

	/* At negative rate process never ends, return maximum value */
	if (rate <= 0) {
		*ret_time = UINT16_MAX;

		return 0;
	}
	/* Convert capacity from mAh to mAmin */
	time = cap * 60 / rate;
	/* Check overflow */
	if (time >= UINT16_MAX) {
		*ret_time = UINT16_MAX;
		bat->error_code = STATUS_CODE_OVERUNDERFLOW;

		return -EINVAL;
	}
	/* Check underflow */
	if (time < 0) {
		*ret_time = 0;
		bat->error_code = STATUS_CODE_OVERUNDERFLOW;

		return -EINVAL;
	}

	*ret_time = time;

	return 0;
}

/**
 * @brief Get time in minutes how long it will take to charge battery
 *
 * @param bat Pointer to battery data
 * @param rate Rate of charging current in mAh
 * @param time Pointer to memory where calculated time will be stored
 *
 * @return 0 on success
 * @return -EINVAL when over or under flow occurred
 */
static int sbat_emul_time_to_full(struct sbat_emul_bat_data *bat, int rate,
				  uint16_t *time)
{
	int cap;

	cap = bat->full_cap - bat->cap;
	return sbat_emul_get_time_to_complete(bat, rate, cap, time);
}

/**
 * @brief Get time in minutes how long it will take to discharge battery. Note,
 *        that rate should be negative to indicate discharging.
 *
 * @param bat Pointer to battery data
 * @param rate Rate of charging current in mAh
 * @param time Pointer to memory where calculated time will be stored
 *
 * @return 0 on success
 * @return -EINVAL when over or under flow occurred
 */
static int sbat_emul_time_to_empty(struct sbat_emul_bat_data *bat, int rate,
				   uint16_t *time)
{
	int cap;

	/* Reverse to have discharging rate instead of charging rate */
	rate = -rate;
	cap = bat->cap;
	return sbat_emul_get_time_to_complete(bat, rate, cap, time);
}

/**
 * @brief Check if battery can supply for 10 seconds additional power/current
 *        set in at_rate register.
 *
 * @param bat Pointer to battery data
 * @param rate Rate of charging current in mAh
 * @param ok Pointer to memory where 0 is written if battery is able to supply
 *           additional power/curent or 1 is written if battery is unable
 *           to do so.
 *
 * @return 0 on success
 */
static int sbat_emul_read_at_rate_ok(struct sbat_emul_bat_data *bat,
				     uint16_t *ok)
{
	int rem_time_s;
	int rate;

	rate = bat->at_rate;
	if (bat->mode & MODE_CAPACITY) {
		rate = sbat_emul_10mw_to_ma(rate, bat->design_mv);
	}

	/* Add current battery usage */
	rate += bat->cur;
	if (rate >= 0) {
		/* Battery will be charged */
		*ok = 1;

		return 0;
	}
	/* Reverse to have discharging rate instead of charging rate */
	rate = -rate;

	rem_time_s = bat->cap * 3600 / rate;
	if (rem_time_s > 10) {
		/*
		 * Battery can support 10 seconds of additional at_rate
		 * current/power
		 */
		*ok = 1;
	} else {
		*ok = 0;
	}

	return 0;
}

/**
 * @brief Get battery status. This function use emulated status register and
 *        set or clear some of the flags based on other properties of emulated
 *        smart battery. Discharge bit, capacity alarm, time alarm, fully
 *        discharged bit and error code are controlled by battery properties.
 *        Terminate charge/discharge/overcharge alarms are set only if they are
 *        set in emulated status register and battery is charging/discharging,
 *        so they are partialy controlled by emulated status register.
 *        Other bits are controlled by emulated status register
 *
 * @param emul Pointer to smart battery emulator
 *
 * @return value which equals to computed status register
 */
static uint16_t sbat_emul_read_status(const struct emul *emul)
{
	uint16_t status, cap, rem_time, charge_percent;
	struct sbat_emul_bat_data *bat;

	bat = sbat_emul_get_bat_data(emul);

	status = bat->status;

	/*
	 * Over charged and terminate charger alarm cannot appear when battery
	 * is not charged
	 */
	if (bat->cur <= 0) {
		status &= ~(STATUS_TERMINATE_CHARGE_ALARM |
			    STATUS_OVERCHARGED_ALARM);
		status |= STATUS_DISCHARGING;
	}
	/* Terminate discharge alarm cannot appear when battery is charged */
	if (bat->cur >= 0) {
		status &= ~(STATUS_TERMINATE_DISCHARGE_ALARM |
			    STATUS_DISCHARGING);
	}

	sbat_emul_get_word_val(emul, SB_REMAINING_CAPACITY, &cap);
	if (bat->cap_alarm && cap < bat->cap_alarm) {
		status |= STATUS_REMAINING_CAPACITY_ALARM;
	} else {
		status &= ~STATUS_REMAINING_CAPACITY_ALARM;
	}

	sbat_emul_get_word_val(emul, SB_AVERAGE_TIME_TO_EMPTY, &rem_time);
	if (bat->time_alarm && rem_time < bat->time_alarm) {
		status |= STATUS_REMAINING_TIME_ALARM;
	} else {
		status &= ~STATUS_REMAINING_TIME_ALARM;
	}

	/* Unset fully discharged bit when charge is grater than 20% */
	sbat_emul_get_word_val(emul, SB_RELATIVE_STATE_OF_CHARGE,
			       &charge_percent);
	if (charge_percent > 20) {
		status &= ~STATUS_FULLY_DISCHARGED;
	} else {
		status |= STATUS_FULLY_DISCHARGED;
	}

	status |= bat->error_code & STATUS_ERR_CODE_MASK;

	return status;
}

/** Check description in emul_smart_battery.h */
int sbat_emul_get_word_val(const struct emul *emul, int cmd, uint16_t *val)
{
	struct sbat_emul_bat_data *bat;
	int mode_mw;
	int rate;

	bat = sbat_emul_get_bat_data(emul);
	mode_mw = bat->mode & MODE_CAPACITY;

	switch (cmd) {
	case SB_MANUFACTURER_ACCESS:
		*val = bat->mf_access;
		return 0;
	case SB_REMAINING_CAPACITY_ALARM:
		*val = bat->cap_alarm;
		return 0;
	case SB_REMAINING_TIME_ALARM:
		*val = bat->time_alarm;
		return 0;
	case SB_BATTERY_MODE:
		*val = bat->mode;
		return 0;
	case SB_AT_RATE:
		*val = bat->at_rate;
		return 0;
	case SB_AT_RATE_TIME_TO_FULL:
		/* Support for reporting time to full in mW mode is optional */
		if (mode_mw && !bat->at_rate_full_mw_support) {
			bat->error_code = STATUS_CODE_OVERUNDERFLOW;
			*val = UINT16_MAX;

			return -EINVAL;
		}

		rate = bat->at_rate;
		if (mode_mw) {
			rate = sbat_emul_10mw_to_ma(rate, bat->design_mv);
		}
		return sbat_emul_time_to_full(bat, rate, val);

	case SB_AT_RATE_TIME_TO_EMPTY:
		rate = bat->at_rate;
		if (mode_mw) {
			rate = sbat_emul_10mw_to_ma(rate, bat->design_mv);
		}
		return sbat_emul_time_to_empty(bat, rate, val);

	case SB_AT_RATE_OK:
		return sbat_emul_read_at_rate_ok(bat, val);
	case SB_TEMPERATURE:
		*val = bat->temp;
		return 0;
	case SB_VOLTAGE:
		*val = bat->volt;
		return 0;
	case SB_CURRENT:
		*val = bat->cur;
		return 0;
	case SB_AVERAGE_CURRENT:
		*val = bat->avg_cur;
		return 0;
	case SB_MAX_ERROR:
		*val = bat->max_error;
		return 0;
	case SB_RELATIVE_STATE_OF_CHARGE:
		/* Percent of charge according to full capacity */
		*val = 100 * bat->cap / bat->full_cap;
		return 0;
	case SB_ABSOLUTE_STATE_OF_CHARGE:
		/* Percent of charge according to design capacity */
		*val = 100 * bat->cap / bat->design_cap;
		return 0;
	case SB_REMAINING_CAPACITY:
		if (mode_mw) {
			*val = sbat_emul_ma_to_10mw(bat->cap, bat->design_mv);
		} else {
			*val = bat->cap;
		}
		return 0;
	case SB_FULL_CHARGE_CAPACITY:
		if (mode_mw) {
			*val = sbat_emul_ma_to_10mw(bat->full_cap,
						    bat->design_mv);
		} else {
			*val = bat->full_cap;
		}
		return 0;
	case SB_RUN_TIME_TO_EMPTY:
		rate = bat->cur;
		return sbat_emul_time_to_empty(bat, rate, val);
	case SB_AVERAGE_TIME_TO_EMPTY:
		rate = bat->avg_cur;
		return sbat_emul_time_to_empty(bat, rate, val);
	case SB_AVERAGE_TIME_TO_FULL:
		rate = bat->avg_cur;
		return sbat_emul_time_to_full(bat, rate, val);
	case SB_CHARGING_CURRENT:
		*val = bat->desired_charg_cur;
		return 0;
	case SB_CHARGING_VOLTAGE:
		*val = bat->desired_charg_volt;
		return 0;
	case SB_BATTERY_STATUS:
		*val = sbat_emul_read_status(emul);
		return 0;
	case SB_CYCLE_COUNT:
		*val = bat->cycle_count;
		return 0;
	case SB_DESIGN_CAPACITY:
		if (mode_mw) {
			*val = sbat_emul_ma_to_10mw(bat->design_cap,
						    bat->design_mv);
		} else {
			*val = bat->design_cap;
		}
		return 0;
	case SB_DESIGN_VOLTAGE:
		*val = bat->design_mv;
		return 0;
	case SB_SPECIFICATION_INFO:
		*val = bat->spec_info;
		return 0;
	case SB_MANUFACTURE_DATE:
		*val = bat->mf_date;
		return 0;
	case SB_SERIAL_NUMBER:
		*val = bat->sn;
		return 0;
	default:
		/* Unknown command or return value is not word */
		return 1;
	}
}

/** Check description in emul_smart_battery.h */
int sbat_emul_get_block_data(const struct emul *emul, int cmd, uint8_t **blk,
			     int *len)
{
	struct sbat_emul_bat_data *bat;
	struct sbat_emul_data *data;

	data = emul->data;
	bat = &data->bat;

	switch (cmd) {
	case SB_MANUFACTURER_NAME:
		*blk = bat->mf_name;
		*len = bat->mf_name_len;
		return 0;
	case SB_DEVICE_NAME:
		*blk = bat->dev_name;
		*len = bat->dev_name_len;
		return 0;
	case SB_DEVICE_CHEMISTRY:
		*blk = bat->dev_chem;
		*len = bat->dev_chem_len;
		return 0;
	case SB_MANUFACTURER_DATA:
		*blk = bat->mf_data;
		*len = bat->mf_data_len;
		return 0;
	case SB_MANUFACTURE_INFO:
		*blk = bat->mf_info;
		*len = bat->mf_info_len;
		return 0;
	default:
		/* Unknown command or return value is not word */
		return 1;
	}
}

/**
 * @brief Append PEC to read command response if battery support it
 *
 * @param data Pointer to smart battery emulator data
 * @param cmd Command for which PEC is calculated
 */
static void sbat_emul_append_pec(const struct emul *emul, int cmd)
{
	uint8_t pec;
	struct sbat_emul_data *data = emul->data;
	const struct i2c_common_emul_cfg *cfg = emul->cfg;

	if (BATTERY_SPEC_VERSION(data->bat.spec_info) ==
	    BATTERY_SPEC_VER_1_1_WITH_PEC) {
		pec = sbat_emul_pec_head(cfg->addr, 1, cmd);
		pec = cros_crc8_arg(data->msg_buf, data->num_to_read, pec);
		data->msg_buf[data->num_to_read] = pec;
		data->num_to_read++;
	}
}

/** Check description in emul_smart_battery.h */
void sbat_emul_set_response(const struct emul *emul, int cmd, uint8_t *buf,
			    int len, bool fail)
{
	struct sbat_emul_data *data;

	data = emul->data;

	if (fail) {
		data->bat.error_code = STATUS_CODE_UNKNOWN_ERROR;
		data->num_to_read = 0;
		return;
	}

	data->num_to_read = MIN(len, MSG_BUF_LEN - 1);
	memcpy(data->msg_buf, buf, data->num_to_read);
	data->bat.error_code = STATUS_CODE_OK;
	sbat_emul_append_pec(emul, cmd);
}

/**
 * @brief Function which handles read messages. It expects that data->cur_cmd
 *        is set to command number which should be handled. It guarantee that
 *        data->num_to_read is set to number of bytes in data->msg_buf on
 *        successful handling read request. On error, data->num_to_read is
 *        always set to 0.
 *
 * @param emul Pointer to smart battery emulator
 * @param reg Command selected by last write message. If data->cur_cmd is
 *            different than SBAT_EMUL_NO_CMD, then reg should equal to
 *            data->cur_cmd
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int sbat_emul_handle_read_msg(const struct emul *emul, int reg)
{
	struct sbat_emul_data *data;
	uint16_t word;
	uint8_t *blk;
	int ret, len;

	data = emul->data;

	if (data->cur_cmd == SBAT_EMUL_NO_CMD) {
		/* Unexpected read message without preceding command select */
		data->bat.error_code = STATUS_CODE_UNKNOWN_ERROR;
		return -EIO;
	}
	data->cur_cmd = SBAT_EMUL_NO_CMD;
	data->num_to_read = 0;

	/* Handle commands which return word */
	ret = sbat_emul_get_word_val(emul, reg, &word);
	if (ret < 0) {
		return -EIO;
	}
	if (ret == 0) {
		data->num_to_read = 2;
		data->msg_buf[0] = word & 0xff;
		data->msg_buf[1] = (word >> 8) & 0xff;
		data->bat.error_code = STATUS_CODE_OK;
		sbat_emul_append_pec(emul, reg);

		return 0;
	}

	/* Handle commands which return block */
	ret = sbat_emul_get_block_data(emul, reg, &blk, &len);
	if (ret < 0) {
		return -EIO;
	}
	if (ret == 0) {
		data->num_to_read = len + 1;
		data->msg_buf[0] = len;
		memcpy(&data->msg_buf[1], blk, len);
		data->bat.error_code = STATUS_CODE_OK;
		sbat_emul_append_pec(emul, reg);

		return 0;
	}

	/* Command is unknown. Wait for custom handler before failing. */
	data->num_to_read = 0;

	return 0;
}

/**
 * @brief Function which finalize write messages.
 *
 * @param emul Pointer to smart battery emulator
 * @param reg First byte of write message, usually selected command
 * @param bytes Number of bytes received in data->msg_buf
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int sbat_emul_finalize_write_msg(const struct emul *emul, int reg,
					int bytes)
{
	struct sbat_emul_bat_data *bat;
	struct sbat_emul_data *data;
	uint16_t word;
	uint8_t pec;

	data = emul->data;
	bat = &data->bat;

	/*
	 * Fail if:
	 *  - there are no bytes to handle
	 *  - there are too many bytes
	 *  - there is command byte and only one data byte
	 */
	if (bytes <= 0 || bytes > 4 || bytes == 2) {
		data->bat.error_code = STATUS_CODE_BADSIZE;
		LOG_ERR("wrong write message size (%d)", bytes);

		return -EIO;
	}

	/* There is only command for read */
	if (bytes == 1) {
		data->cur_cmd = reg;
		return 0;
	}

	/* Handle PEC */
	data->msg_buf[0] = reg;
	if (bytes == 4) {
		if (BATTERY_SPEC_VERSION(data->bat.spec_info) !=
		    BATTERY_SPEC_VER_1_1_WITH_PEC) {
			data->bat.error_code = STATUS_CODE_BADSIZE;
			LOG_ERR("Unexpected PEC; No support in this version");

			return -EIO;
		}
		pec = sbat_emul_pec_head(data->common.cfg->addr, 0, 0);
		pec = cros_crc8_arg(data->msg_buf, 3, pec);
		if (pec != data->msg_buf[3]) {
			data->bat.error_code = STATUS_CODE_UNKNOWN_ERROR;
			LOG_ERR("Wrong PEC 0x%x != 0x%x", pec,
				data->msg_buf[3]);

			return -EIO;
		}
	}

	word = ((int)data->msg_buf[2] << 8) | data->msg_buf[1];

	switch (data->msg_buf[0]) {
	case SB_MANUFACTURER_ACCESS:
		bat->mf_access = word;
		break;
	case SB_REMAINING_CAPACITY_ALARM:
		bat->cap_alarm = word;
		break;
	case SB_REMAINING_TIME_ALARM:
		bat->time_alarm = word;
		break;
	case SB_BATTERY_MODE:
		/* Allow to set only upper byte */
		bat->mode &= 0xff;
		bat->mode |= word & 0xff00;
		break;
	case SB_AT_RATE:
		bat->at_rate = word;
		break;
	default:
		data->bat.error_code = STATUS_CODE_ACCESS_DENIED;
		LOG_ERR("Unknown write command (0x%x)", data->msg_buf[0]);

		return -EIO;
	}

	data->bat.error_code = STATUS_CODE_OK;

	return 0;
}

/**
 * @brief Function called for each byte of write message which is saved in
 *        data->msg_buf
 *
 * @param emul Pointer to smart battery emulator
 * @param reg First byte of write message, usually selected command
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 */
static int sbat_emul_write_byte(const struct emul *emul, int reg, uint8_t val,
				int bytes)
{
	struct sbat_emul_data *data;

	data = emul->data;

	if (bytes < MSG_BUF_LEN) {
		data->msg_buf[bytes] = val;
	}

	return 0;
}

/**
 * @brief Function called for each byte of read message. Byte from data->msg_buf
 *        is copied to read message response.
 *
 * @param emul Pointer to smart battery emulator
 * @param reg First byte of last write message, usually selected command
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already readed
 *
 * @return 0 on success
 */
static int sbat_emul_read_byte(const struct emul *emul, int reg, uint8_t *val,
			       int bytes)
{
	struct sbat_emul_data *data;

	data = emul->data;

	if (data->num_to_read == 0) {
		data->bat.error_code = STATUS_CODE_UNSUPPORTED;
		LOG_ERR("Unknown read command (0x%x)", reg);

		return -EIO;
	}

	if (bytes < data->num_to_read) {
		*val = data->msg_buf[bytes];
	}

	return 0;
}

/**
 * @brief Get currently accessed register, which always equals to selected
 *        command.
 *
 * @param emul Pointer to smart battery emulator
 * @param reg First byte of last write message, usually selected command
 * @param bytes Number of bytes already handled from current message
 * @param read If currently handled is read message
 *
 * @return Currently accessed register
 */
static int sbat_emul_access_reg(const struct emul *emul, int reg, int bytes,
				bool read)
{
	return reg;
}

/* Device instantiation */

/**
 * @brief Set up a new Smart Battery emulator
 *
 * This should be called for each Smart Battery device that needs to be
 * emulated. It registers it with the I2C emulation controller.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int sbat_emul_init(const struct emul *emul, const struct device *parent)
{
	struct sbat_emul_data *data = emul->data;
	const struct i2c_common_emul_cfg *cfg = emul->cfg;

	data->common.i2c = parent;
	data->common.cfg = cfg;

	i2c_common_emul_init(&data->common);

	return 0;
}

/* The strings contain a null byte, but a block contains the length.*/
#define SMART_BATTERY_VALIDATE_STRING_PROPS_SIZE(n)                        \
	BUILD_ASSERT(sizeof(DT_INST_PROP(n, dev_chem)) <= MAX_BLOCK_SIZE); \
	BUILD_ASSERT(sizeof(DT_INST_PROP(n, mf_data)) <= MAX_BLOCK_SIZE);  \
	BUILD_ASSERT(sizeof(DT_INST_PROP(n, mf_info)) <= MAX_BLOCK_SIZE);  \
	BUILD_ASSERT(sizeof(DT_INST_PROP(n, mf_name)) <= MAX_BLOCK_SIZE);

DT_INST_FOREACH_STATUS_OKAY(SMART_BATTERY_VALIDATE_STRING_PROPS_SIZE)

#define SMART_BATTERY_EMUL(n)                                         \
	static struct sbat_emul_data sbat_emul_data_##n = {		\
		.bat = {						\
			.mf_access = DT_INST_PROP(n, mf_access),	\
			.at_rate_full_mw_support = DT_INST_PROP(n,	\
					at_rate_full_mw_support),	\
			.spec_info = ((DT_STRING_TOKEN(DT_DRV_INST(n),	\
						       version) <<	\
				       BATTERY_SPEC_VERSION_SHIFT) &	\
				      BATTERY_SPEC_VERSION_MASK) |	\
				     ((DT_INST_PROP(n, vscale) <<	\
				       BATTERY_SPEC_VSCALE_SHIFT) &	\
				      BATTERY_SPEC_VSCALE_MASK) |	\
				     ((DT_INST_PROP(n, ipscale) <<	\
				       BATTERY_SPEC_IPSCALE_SHIFT) &	\
				      BATTERY_SPEC_IPSCALE_MASK) |	\
				     BATTERY_SPEC_REVISION_1,		\
			.mode = (DT_INST_PROP(n,			\
					      int_charge_controller) *	\
				 MODE_INTERNAL_CHARGE_CONTROLLER) |	\
				(DT_INST_PROP(n, primary_battery) *	\
				 MODE_PRIMARY_BATTERY_SUPPORT),		\
			.design_mv = DT_INST_PROP(n, design_mv),	\
			.default_design_mv = DT_INST_PROP(n, design_mv),\
			.design_cap = DT_INST_PROP(n, design_cap),	\
			.temp = DT_INST_PROP(n, temperature),		\
			.volt = DT_INST_PROP(n, volt),			\
			.cur = DT_INST_PROP(n, cur),			\
			.avg_cur = DT_INST_PROP(n, avg_cur),		\
			.max_error = DT_INST_PROP(n, max_error),	\
			.cap = DT_INST_PROP(n, cap),			\
			.default_cap = DT_INST_PROP(n, cap),		\
			.full_cap = DT_INST_PROP(n, full_cap),		\
			.default_full_cap = DT_INST_PROP(n, full_cap),	\
			.desired_charg_cur = DT_INST_PROP(n,		\
						desired_charg_cur),	\
			.desired_charg_volt = DT_INST_PROP(n,		\
						desired_charg_volt),	\
			.cycle_count = DT_INST_PROP(n, cycle_count),	\
			.sn = DT_INST_PROP(n, serial_number),		\
			.mf_name = DT_INST_PROP(n, mf_name),		\
			.mf_name_len = sizeof(				\
					DT_INST_PROP(n, mf_name)) - 1,	\
			.mf_data = DT_INST_PROP(n, mf_data),		\
			.mf_data_len = sizeof(				\
					DT_INST_PROP(n, mf_data)) - 1,	\
			.dev_name = DT_INST_PROP(n, dev_name),		\
			.dev_name_len = sizeof(				\
					DT_INST_PROP(n, dev_name)) - 1,	\
			.dev_chem = DT_INST_PROP(n, dev_chem),		\
			.dev_chem_len = sizeof(				\
					DT_INST_PROP(n, dev_chem)) - 1,	\
			.mf_info = DT_INST_PROP(n, mf_info),		\
			.mf_info_len = sizeof(				\
					DT_INST_PROP(n, mf_info)) - 1,	\
			.mf_date = 0,					\
			.cap_alarm = 0,					\
			.time_alarm = 0,				\
			.at_rate = 0,					\
			.status = STATUS_INITIALIZED,			\
			.error_code = STATUS_CODE_OK,			\
		},							\
		.cur_cmd = SBAT_EMUL_NO_CMD,				\
		.common = {						\
			.start_write = NULL,				\
			.write_byte = sbat_emul_write_byte,		\
			.finish_write = sbat_emul_finalize_write_msg,	\
			.start_read = sbat_emul_handle_read_msg,	\
			.read_byte = sbat_emul_read_byte,		\
			.finish_read = NULL,				\
			.access_reg = sbat_emul_access_reg,		\
		},							\
	};        \
                                                                      \
	static const struct i2c_common_emul_cfg sbat_emul_cfg_##n = { \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),       \
		.data = &sbat_emul_data_##n.common,                   \
		.addr = DT_INST_REG_ADDR(n),                          \
	};                                                            \
	EMUL_DT_INST_DEFINE(n, sbat_emul_init, &sbat_emul_data_##n,   \
			    &sbat_emul_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(SMART_BATTERY_EMUL)

#define SMART_BATTERY_EMUL_CASE(n) \
	case DT_INST_DEP_ORD(n):   \
		return sbat_emul_data_##n.common.emul.target;

#ifdef CONFIG_ZTEST
static void emul_smart_battery_reset_capacity(const struct emul *emul)
{
	struct sbat_emul_data *bat_data = emul->data;
	bat_data->bat.cap = bat_data->bat.default_cap;
	bat_data->bat.full_cap = bat_data->bat.default_full_cap;
	bat_data->bat.design_mv = bat_data->bat.default_design_mv;
}

#define SBAT_EMUL_RESET_RULE_AFTER(n) \
	emul_smart_battery_reset_capacity(EMUL_DT_GET(DT_DRV_INST(n)));

static void emul_sbat_reset(const struct ztest_unit_test *test, void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

	DT_INST_FOREACH_STATUS_OKAY(SBAT_EMUL_RESET_RULE_AFTER)
}

ZTEST_RULE(emul_smart_battery_reset, NULL, emul_sbat_reset);
#endif /* CONFIG_ZTEST */

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

struct i2c_common_emul_data *
emul_smart_battery_get_i2c_common_data(const struct emul *emul)
{
	return emul->data;
}
