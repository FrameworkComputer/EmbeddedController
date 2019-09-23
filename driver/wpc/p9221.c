/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * IDT P9221-R7 Wireless Power Receiver driver.
 */

#include "p9221.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "power.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "util.h"
#include <stdbool.h>
#include "printf.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, "WPC " format, ## args)

#define P9221_TX_TIMEOUT_MS		(20 * 1000*1000)
#define P9221_DCIN_TIMEOUT_MS		(2 * 1000*1000)
#define P9221_VRECT_TIMEOUT_MS		(2 * 1000*1000)
#define P9221_NOTIFIER_DELAY_MS		(80*1000)
#define P9221R7_ILIM_MAX_UA		(1600 * 1000)
#define P9221R7_OVER_CHECK_NUM		3

#define OVC_LIMIT			1
#define OVC_THRESHOLD			1400000
#define OVC_BACKOFF_LIMIT		900000
#define OVC_BACKOFF_AMOUNT		100000

/* P9221  parameters */
static struct wpc_charger_info p9221_charger_info = {
	.online = false,
	.i2c_port = I2C_PORT_WPC,
	.pp_buf_valid = false,
};

static struct wpc_charger_info *wpc = &p9221_charger_info;

static void p9221_set_offline(void);

static const uint32_t p9221_ov_set_lut[] = {
	17000000, 20000000, 15000000, 13000000,
	11000000, 11000000, 11000000, 11000000
};

static int p9221_reg_is_8_bit(uint16_t reg)
{
	switch (reg) {
	case P9221_CHIP_REVISION_REG:
	case P9221R7_VOUT_SET_REG:
	case P9221R7_ILIM_SET_REG:
	case P9221R7_CHARGE_STAT_REG:
	case P9221R7_EPT_REG:
	case P9221R7_SYSTEM_MODE_REG:
	case P9221R7_COM_CHAN_RESET_REG:
	case P9221R7_COM_CHAN_SEND_SIZE_REG:
	case P9221R7_COM_CHAN_SEND_IDX_REG:
	case P9221R7_COM_CHAN_RECV_SIZE_REG:
	case P9221R7_COM_CHAN_RECV_IDX_REG:
	case P9221R7_DEBUG_REG:
	case P9221R7_EPP_Q_FACTOR_REG:
	case P9221R7_EPP_TX_GUARANTEED_POWER_REG:
	case P9221R7_EPP_TX_POTENTIAL_POWER_REG:
	case P9221R7_EPP_TX_CAPABILITY_FLAGS_REG:
	case P9221R7_EPP_RENEGOTIATION_REG:
	case P9221R7_EPP_CUR_RPP_HEADER_REG:
	case P9221R7_EPP_CUR_NEGOTIATED_POWER_REG:
	case P9221R7_EPP_CUR_MAXIMUM_POWER_REG:
	case P9221R7_EPP_CUR_FSK_MODULATION_REG:
	case P9221R7_EPP_REQ_RPP_HEADER_REG:
	case P9221R7_EPP_REQ_NEGOTIATED_POWER_REG:
	case P9221R7_EPP_REQ_MAXIMUM_POWER_REG:
	case P9221R7_EPP_REQ_FSK_MODULATION_REG:
	case P9221R7_VRECT_TARGET_REG:
	case P9221R7_VRECT_KNEE_REG:
	case P9221R7_FOD_SECTION_REG:
	case P9221R7_VRECT_ADJ_REG:
	case P9221R7_ALIGN_X_ADC_REG:
	case P9221R7_ALIGN_Y_ADC_REG:
	case P9221R7_ASK_MODULATION_DEPTH_REG:
	case P9221R7_OVSET_REG:
	case P9221R7_EPP_TX_SPEC_REV_REG:
		return true;
	default:
		return false;
	}
}

static int p9221_read8(uint16_t reg, int *val)
{
	return i2c_read_offset16(wpc->i2c_port, P9221_R7_ADDR_FLAGS,
				 reg, val, 1);
}

static int p9221_write8(uint16_t reg, int val)
{
	return i2c_write_offset16(wpc->i2c_port, P9221_R7_ADDR_FLAGS,
				  reg, val, 1);
}

static int p9221_read16(uint16_t reg, int *val)
{
	return i2c_read_offset16(wpc->i2c_port, P9221_R7_ADDR_FLAGS,
				 reg, val, 2);
}

static int p9221_write16(uint16_t reg, int val)
{
	return i2c_write_offset16(wpc->i2c_port, P9221_R7_ADDR_FLAGS,
				  reg, val, 2);
}

static int p9221_block_read(uint16_t reg, uint8_t *data, int len)
{
	return i2c_read_offset16_block(wpc->i2c_port, P9221_R7_ADDR_FLAGS,
				       reg, data, len);
}

static int p9221_block_write(uint16_t reg, uint8_t *data, int len)
{
	return i2c_write_offset16_block(wpc->i2c_port, P9221_R7_ADDR_FLAGS,
					reg, data, len);
}

static int p9221_set_cmd_reg(uint8_t cmd)
{
	int cur_cmd;
	int retry;
	int ret;

	for (retry = 0; retry < P9221_COM_CHAN_RETRIES; retry++) {
		ret = p9221_read8(P9221_COM_REG, &cur_cmd);
		if (ret == EC_SUCCESS && cur_cmd == 0)
			break;
		msleep(25);
	}

	if (retry >= P9221_COM_CHAN_RETRIES) {
		CPRINTS("Failed to wait for cmd free %02x", cur_cmd);
		return EC_ERROR_TIMEOUT;
	}

	ret = p9221_write8(P9221_COM_REG, cmd);
	if (ret)
		CPRINTS("Failed to set cmd reg %02x: %d", cmd, ret);

	return ret;
}

/* Convert a register value to uV, Hz, or uA */
static int p9221_convert_reg_r7(uint16_t reg, uint16_t raw_data, uint32_t *val)
{
	switch (reg) {
	case P9221R7_ALIGN_X_ADC_REG:	/* raw */
	case P9221R7_ALIGN_Y_ADC_REG:	/* raw */
		*val = raw_data;
		break;
	case P9221R7_VOUT_ADC_REG:	/* 12-bit ADC raw */
	case P9221R7_IOUT_ADC_REG:	/* 12-bit ADC raw */
	case P9221R7_DIE_TEMP_ADC_REG:	/* 12-bit ADC raw */
	case P9221R7_EXT_TEMP_REG:
		*val = raw_data & 0xFFF;
		break;
	case P9221R7_VOUT_SET_REG:	/* 0.1V -> uV */
		*val = raw_data * 100 * 1000;
		break;
	case P9221R7_IOUT_REG:		/* mA -> uA */
	case P9221R7_VRECT_REG:		/* mV -> uV */
	case P9221R7_VOUT_REG:		/* mV -> uV */
	case P9221R7_OP_FREQ_REG:	/* kHz -> Hz */
	case P9221R7_TX_PINGFREQ_REG:	/* kHz -> Hz */
		*val = raw_data * 1000;
		break;
	case P9221R7_ILIM_SET_REG:	/* 100mA -> uA, 200mA offset */
		*val = ((raw_data * 100) + 200) * 1000;
		break;
	case P9221R7_OVSET_REG:		/* uV */
		raw_data &= P9221R7_OVSET_MASK;
		*val = p9221_ov_set_lut[raw_data];
		break;
	default:
		return -2;
	}

	return 0;
}

static int p9221_reg_read_converted(uint16_t reg, uint32_t *val)
{
	int ret;
	int data;

	if (p9221_reg_is_8_bit(reg))
		ret = p9221_read8(reg, &data);
	else
		ret = p9221_read16(reg, &data);

	if (ret)
		return ret;

	return p9221_convert_reg_r7(reg, data, val);
}

static int p9221_is_online(void)
{
	int chip_id;

	if (p9221_read16(P9221_CHIP_ID_REG, &chip_id)
			|| chip_id != P9221_CHIP_ID)
		return false;
	else
		return true;
}

int wpc_chip_is_online(void)
{
	return p9221_is_online();
}


void p9221_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_WPC);
}

static int p9221r7_clear_interrupts(uint16_t mask)
{
	int ret;

	ret = p9221_write16(P9221R7_INT_CLEAR_REG, mask);
	if (ret) {
		CPRINTS("Failed to clear INT reg: %d", ret);
		return ret;
	}

	ret = p9221_set_cmd_reg(P9221_COM_CLEAR_INT_MASK);
	if (ret)
		CPRINTS("Failed to reset INT: %d", ret);

	return ret;
}

/*
 * Enable interrupts on the P9221 R7, note we don't really need to disable
 * interrupts since when the device goes out of field, the P9221 is reset.
 */
static int p9221_enable_interrupts_r7(void)
{
	uint16_t mask = 0;
	int ret;

	CPRINTS("Enable interrupts");

	mask = P9221R7_STAT_LIMIT_MASK | P9221R7_STAT_CC_MASK
		| P9221_STAT_VRECT;

	p9221r7_clear_interrupts(mask);

	ret = p9221_write8(P9221_INT_ENABLE_REG, mask);
	if (ret)
		CPRINTS("Failed to enable INTs: %d", ret);
	return ret;
}

static int p9221_send_csp(uint8_t status)
{
	int ret;

	CPRINTS("Send CSP=%d", status);
	mutex_lock(&wpc->cmd_lock);

	ret = p9221_write8(P9221R7_CHARGE_STAT_REG, status);
	if (ret == EC_SUCCESS)
		ret = p9221_set_cmd_reg(P9221R7_COM_SENDCSP);

	mutex_unlock(&wpc->cmd_lock);
	return ret;
}

static int p9221_send_eop(uint8_t reason)
{
	int rv;

	CPRINTS("Send EOP reason=%d", reason);
	mutex_lock(&wpc->cmd_lock);

	rv = p9221_write8(P9221R7_EPT_REG, reason);
	if (rv == EC_SUCCESS)
		rv = p9221_set_cmd_reg(P9221R7_COM_SENDEPT);

	mutex_unlock(&wpc->cmd_lock);
	return rv;
}

static void print_current_samples(uint32_t *iout_val, int count)
{
	int i;
	char temp[P9221R7_OVER_CHECK_NUM * 9 + 1] = { 0 };

	for (i = 0; i < count ; i++)
		snprintf(temp + i * 9, sizeof(temp) - i * 9,
			  "%08x ", iout_val[i]);
	CPRINTS("OVER IOUT_SAMPLES: %s", temp);
}


/*
 * Number of times to poll the status to see if the current limit condition
 * was transient or not.
 */
static void p9221_limit_handler_r7(uint16_t orign_irq_src)
{
	uint8_t reason;
	int i;
	int ret;
	int ovc_count = 0;
	uint32_t iout_val[P9221R7_OVER_CHECK_NUM] = { 0 };
	int irq_src = (int)orign_irq_src;

	CPRINTS("OVER INT: %02x", irq_src);

	if (irq_src & P9221R7_STAT_OVV) {
		reason = P9221_EOP_OVER_VOLT;
		goto send_eop;
	}

	if (irq_src & P9221R7_STAT_OVT) {
		reason = P9221_EOP_OVER_TEMP;
		goto send_eop;
	}

	if ((irq_src & P9221R7_STAT_UV) && !(irq_src & P9221R7_STAT_OVC))
		return;

	reason = P9221_EOP_OVER_CURRENT;
	for (i = 0; i < P9221R7_OVER_CHECK_NUM; i++) {
		ret = p9221r7_clear_interrupts(
				irq_src & P9221R7_STAT_LIMIT_MASK);
		msleep(50);
		if (ret)
			continue;

		ret = p9221_reg_read_converted(P9221R7_IOUT_REG, &iout_val[i]);
		if (ret) {
			CPRINTS("Failed to read IOUT[%d]: %d", i, ret);
			continue;
		} else if (iout_val[i] > OVC_THRESHOLD) {
			ovc_count++;
		}

		ret = p9221_read16(P9221_STATUS_REG, &irq_src);
		if (ret) {
			CPRINTS("Failed to read status: %d", ret);
			continue;
		}

		if ((irq_src & P9221R7_STAT_OVC) == 0) {
			print_current_samples(iout_val, i + 1);
			CPRINTS("OVER condition %04x cleared after %d tries",
				irq_src, i);
			return;
		}

		CPRINTS("OVER status is still %04x, retry", irq_src);
	}

	if (ovc_count < OVC_LIMIT) {
		print_current_samples(iout_val, P9221R7_OVER_CHECK_NUM);
		CPRINTS("ovc_threshold=%d, ovc_count=%d, ovc_limit=%d",
			OVC_THRESHOLD, ovc_count, OVC_LIMIT);
		return;
	}

send_eop:
	CPRINTS("OVER is %04x, sending EOP %d", irq_src, reason);

	ret = p9221_send_eop(reason);
	if (ret)
		CPRINTS("Failed to send EOP %d: %d", reason, ret);
}

static void p9221_abort_transfers(void)
{
	wpc->tx_busy = false;
	wpc->tx_done = true;
	wpc->rx_done = true;
	wpc->rx_len = 0;
}

/* Handler for r7 and R7 chips */
static void p9221r7_irq_handler(uint16_t irq_src)
{
	int res;

	if (irq_src & P9221R7_STAT_LIMIT_MASK)
		p9221_limit_handler_r7(irq_src);

	/* Receive complete */
	if (irq_src & P9221R7_STAT_CCDATARCVD) {
		int rxlen = 0;

		res = p9221_read8(P9221R7_COM_CHAN_RECV_SIZE_REG, &rxlen);
		if (res)
			CPRINTS("Failed to read len: %d", res);

		if (rxlen) {
			res = p9221_block_read(P9221R7_DATA_RECV_BUF_START,
					       wpc->rx_buf, rxlen);
			if (res) {
				CPRINTS("Failed to read CC data: %d", res);
				rxlen = 0;
			}

			wpc->rx_len = rxlen;
			wpc->rx_done = true;
		}
	}

	/* Send complete */
	if (irq_src & P9221R7_STAT_CCSENDBUSY) {
		wpc->tx_busy = false;
		wpc->tx_done = true;
	}

	/* Proprietary packet */
	if (irq_src & P9221R7_STAT_PPRCVD) {
		res = p9221_block_read(P9221R7_DATA_RECV_BUF_START,
				       wpc->pp_buf, sizeof(wpc->pp_buf));
		if (res) {
			CPRINTS("Failed to read PP: %d", res);
			wpc->pp_buf_valid = false;
			return;
		}

		/* We only care about PP which come with 0x4F header */
		wpc->pp_buf_valid = (wpc->pp_buf[0] == 0x4F);

		hexdump(wpc->pp_buf, sizeof(wpc->pp_buf));
	}

	/* CC Reset complete */
	if (irq_src & P9221R7_STAT_CCRESET)
		p9221_abort_transfers();
}

static int p9221_is_epp(void)
{
	int ret, reg;
	uint32_t vout_uv;

	if (p9221_read8(P9221R7_SYSTEM_MODE_REG, &reg) == EC_SUCCESS)
		return reg & P9221R7_SYSTEM_MODE_EXTENDED_MASK;

	/* Check based on power supply voltage */
	ret = p9221_reg_read_converted(P9221R7_VOUT_ADC_REG, &vout_uv);
	if (ret) {
		CPRINTS("Failed to read VOUT_ADC: %d", ret);
		return false;
	}

	CPRINTS("Voltage is %duV", vout_uv);
	if (vout_uv > P9221_EPP_THRESHOLD_UV)
		return true;

	return false;
}

static void p9221_config_fod(void)
{

	int epp;
	uint8_t *fod;
	int fod_len;
	int ret;
	int retries = 3;

	CPRINTS("Config FOD");

	epp = p9221_is_epp();
	fod_len = epp ? board_get_epp_fod(&fod) : board_get_fod(&fod);
	if (!fod_len || !fod) {
		CPRINTS("FOD data not found");
		return;
	}

	while (retries) {
		uint8_t fod_read[fod_len];

		CPRINTS("Writing %s FOD (n=%d try=%d)",
			epp ? "EPP" : "BPP", fod_len, retries);

		ret = p9221_block_write(P9221R7_FOD_REG, fod, fod_len);
		if (ret)
			goto no_fod;

		/* Verify the FOD has been written properly */
		ret = p9221_block_read(P9221R7_FOD_REG, fod_read, fod_len);
		if (ret)
			goto no_fod;

		if (memcmp(fod, fod_read, fod_len) == 0)
			return;

		hexdump(fod_read, fod_len);

		retries--;
		msleep(100);
	}

no_fod:
	CPRINTS("Failed to set FOD. retries:%d ret:%d", retries, ret);
}

static void p9221_set_online(void)
{
	int ret;

	CPRINTS("Set online");

	wpc->online = true;

	wpc->tx_busy = false;
	wpc->tx_done = true;
	wpc->rx_done = false;
	wpc->charge_supplier = CHARGE_SUPPLIER_WPC_BPP;

	ret = p9221_enable_interrupts_r7();
	if (ret)
		CPRINTS("Failed to enable INT: %d", ret);

	/* NOTE: depends on _is_epp() which is not valid until DC_IN */
	p9221_config_fod();
}

static void p9221_vbus_check_timeout(void)
{
	CPRINTS("Timeout VBUS, online=%d", wpc->online);
	if (wpc->online)
		p9221_set_offline();

}
DECLARE_DEFERRED(p9221_vbus_check_timeout);

static void p9221_set_offline(void)
{
	CPRINTS("Set offline");

	wpc->online = false;
	/* Reset PP buf so we can get a new serial number next time around */
	wpc->pp_buf_valid = false;

	p9221_abort_transfers();

	hook_call_deferred(&p9221_vbus_check_timeout_data, -1);
}

/* P9221_NOTIFIER_DELAY_MS from VRECTON */
static int p9221_notifier_check_det(void)
{
	if (wpc->online)
		goto done;

	/* send out a FOD but is_epp() is still invalid */
	p9221_set_online();

	/* Give the vbus 2 seconds to come up. */
	CPRINTS("Waiting VBUS");
	hook_call_deferred(&p9221_vbus_check_timeout_data, -1);
	hook_call_deferred(&p9221_vbus_check_timeout_data,
			   P9221_DCIN_TIMEOUT_MS);

done:
	wpc->p9221_check_det = false;
	return 0;
}

static int p9221_get_charge_supplier(void)
{
	if (!wpc->online)
		return EC_ERROR_UNKNOWN;

	if (p9221_is_epp()) {
		uint32_t tx_id;
		int txmf_id;
		int ret;

		wpc->charge_supplier = CHARGE_SUPPLIER_WPC_EPP;

		ret = p9221_read16(P9221R7_EPP_TX_MFG_CODE_REG, &txmf_id);
		if (ret || txmf_id != P9221_GPP_TX_MF_ID)
			return ret;

		ret = p9221_block_read(P9221R7_PROP_TX_ID_REG,
				       (uint8_t *) &tx_id,
				       P9221R7_PROP_TX_ID_SIZE);
		if (ret)
			return ret;

		if (tx_id & P9221R7_PROP_TX_ID_GPP_MASK)
			wpc->charge_supplier = CHARGE_SUPPLIER_WPC_GPP;

		CPRINTS("txmf_id=0x%04x tx_id=0x%08x supplier=%d",
			txmf_id, tx_id, wpc->charge_supplier);
	} else {
		wpc->charge_supplier = CHARGE_SUPPLIER_WPC_BPP;
		CPRINTS("supplier=%d", wpc->charge_supplier);
	}

	return EC_SUCCESS;
}

static int p9221_get_icl(int charge_supplier)
{
	switch (charge_supplier) {
	case CHARGE_SUPPLIER_WPC_EPP:
	case CHARGE_SUPPLIER_WPC_GPP:
		return P9221_DC_ICL_EPP_MA;
	case CHARGE_SUPPLIER_WPC_BPP:
	default:
		return P9221_DC_ICL_BPP_MA;
	}
}

static int p9221_get_ivl(int charge_supplier)
{
	switch (charge_supplier) {
	case CHARGE_SUPPLIER_WPC_EPP:
	case CHARGE_SUPPLIER_WPC_GPP:
		return P9221_DC_IVL_EPP_MV;
	case CHARGE_SUPPLIER_WPC_BPP:
	default:
		return P9221_DC_IVL_BPP_MV;
	}
}

static void p9221_update_charger(int type, struct charge_port_info *chg)
{
	if (!chg)
		charge_manager_update_dualrole(0, CAP_UNKNOWN);
	else
		charge_manager_update_dualrole(0, CAP_DEDICATED);

	charge_manager_update_charge(type, 0, chg);
}

static int p9221_reg_write_converted_r7(uint16_t reg, uint32_t val)
{
	int ret = 0;
	uint16_t data;
	int i;
	/* Do the appropriate conversion */
	switch (reg) {
	case P9221R7_ILIM_SET_REG:
		/* uA -> 0.1A, offset 0.2A */
		if ((val < 200000) || (val > 1600000))
			return -EC_ERROR_INVAL;
		data = (val / (100 * 1000)) - 2;
		break;
	case P9221R7_VOUT_SET_REG:
		/* uV -> 0.1V */
		val /= 1000;
		if (val < 3500 || val > 9000)
			return -EC_ERROR_INVAL;
		data = val / 100;
		break;
	case P9221R7_OVSET_REG:
		/* uV */
		for (i = 0; i < ARRAY_SIZE(p9221_ov_set_lut); i++) {
			if (val == p9221_ov_set_lut[i])
				break;
		}
		if (i == ARRAY_SIZE(p9221_ov_set_lut))
			return -EC_ERROR_INVAL;
		data = i;
		break;
	default:
		return -EC_ERROR_INVAL;
	}
	if (p9221_reg_is_8_bit(reg))
		ret = p9221_write8(reg, data);
	else
		ret = p9221_write16(reg, data);
	return ret;
}

static int p9221_set_dc_icl(void)
{
	/* Increase the IOUT limit */
	if (p9221_reg_write_converted_r7(P9221R7_ILIM_SET_REG,
					 P9221R7_ILIM_MAX_UA))
		CPRINTS("%s set rx_iout limit fail.", __func__);

	return EC_SUCCESS;
}


static void p9221_notifier_check_vbus(void)
{
	struct charge_port_info chg;

	wpc->p9221_check_vbus = false;

	CPRINTS("%s online:%d vbus:%d", __func__, wpc->online,
		wpc->vbus_status);

	/*
	 * We now have confirmation from DC_IN, kill the timer, p9221_online
	 * will be set by this function.
	 */
	hook_call_deferred(&p9221_vbus_check_timeout_data, -1);

	if (wpc->vbus_status) {
		/* WPC VBUS on ,Always write FOD, check dc_icl, send CSP */
		p9221_set_dc_icl();
		p9221_config_fod();

		p9221_send_csp(1);

		/* when wpc vbus attached after 2s, set wpc online */
		if (!wpc->online)
			p9221_set_online();

		/* WPC VBUS on , update charge voltage and current */
		p9221_get_charge_supplier();
		chg.voltage = p9221_get_ivl(wpc->charge_supplier);
		chg.current = p9221_get_icl(wpc->charge_supplier);

		p9221_update_charger(wpc->charge_supplier, &chg);
	} else {
		/*
		 * Vbus detached, set wpc offline and update wpc charge voltage
		 * and current to zero.
		 */
		if (wpc->online) {
			p9221_set_offline();
			p9221_update_charger(wpc->charge_supplier, NULL);
		}
	}

	CPRINTS("check_vbus changed on:%d vbus:%d", wpc->online,
		wpc->vbus_status);

}

static void p9221_detect_work(void)
{

	CPRINTS("%s online:%d check_vbus:%d check_det:%d vbus:%d", __func__,
		wpc->online, wpc->p9221_check_vbus, wpc->p9221_check_det,
		wpc->vbus_status);

	/* Step 1 */
	if (wpc->p9221_check_det)
		p9221_notifier_check_det();

	/* Step 2 */
	if (wpc->p9221_check_vbus)
		p9221_notifier_check_vbus();

}
DECLARE_DEFERRED(p9221_detect_work);

void p9221_notify_vbus_change(int vbus)
{
	wpc->p9221_check_vbus = true;
	wpc->vbus_status = vbus;
	hook_call_deferred(&p9221_detect_work_data, P9221_NOTIFIER_DELAY_MS);
}

void wireless_power_charger_task(void *u)
{
	while (1) {
		int ret, irq_src;
		task_wait_event(-1);

		ret = p9221_read16(P9221_INT_REG, &irq_src);
		if (ret) {
			CPRINTS("Failed to read INT REG");
			continue;
		}

		CPRINTS("INT SRC 0x%04x", irq_src);

		if (p9221r7_clear_interrupts(irq_src))
			continue;

		if (irq_src & P9221_STAT_VRECT) {
			CPRINTS("VRECTON, online=%d", wpc->online);
			if (!wpc->online) {
				wpc->p9221_check_det = true;
				hook_call_deferred(&p9221_detect_work_data,
						   P9221_NOTIFIER_DELAY_MS);
			}
		}

		p9221r7_irq_handler(irq_src);
	}
}
