/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c.h"
#include "ioexpanders.h"
#include "tca6416a.h"
#include "tca6424a.h"

/******************************************************************************
 * Initialize IOExpanders.
 */

static int dut_chg_en_state;

/* Enable all ioexpander outputs. */
int init_ioexpanders(void)
{
	int ret;

	/*
	 * Init TCA6416A, PORT 0
	 * NAME                      | DIR | Initial setting
	 * -------------------------------------------------
	 * BIT-0 (SBU_UART_SEL)      | O   | 0
	 * BIT-1 (ATMEL_RESET_L)     | O   | 0
	 * BIT-2 (SBU_FLIP_SEL)      | O   | 1
	 * BIT-3 (USB3_A0_MUX_SEL)   | O   | 0
	 * BIT-4 (USB3_A0_MUX_EN_L)  | O   | 0
	 * BIT-5 (USB3_A0_PWR_EN)    | O   | 0
	 * BIT-6 (UART_18_SEL)       | O   | 0
	 * BIT-7 (USERVO_POWER_EN)   | O   | 0
	 */
	ret = tca6416a_write_byte(1, TCA6416A_OUT_PORT_0, 0x04);
	if (ret != EC_SUCCESS)
		return ret;

	ret = tca6416a_write_byte(1, TCA6416A_DIR_PORT_0, 0x00);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * Init TCA6416A, PORT 1
	 * NAME                            | DIR | Initial setting
	 * -------------------------------------------------------
	 * BIT-0 (USERVO_FASTBOOT_MUX_SEL) | O   | 0
	 * BIT-1 (USB3_A1_PWR_EN)          | O   | 0
	 * BIT-2 (USB3_A1_MUX_SEL)         | O   | 0
	 * BIT-3 (BOARD_ID)                | I   | x
	 * BIT-4 (BOARD ID)                | I   | x
	 * BIT-5 (BOARD_ID)                | I   | x
	 * BIT-6 (CMUX_EN)                 | O   | 1
	 * BIT-7 (DONGLE_DET)              | I   | x
	 */
	ret = tca6416a_write_byte(1, TCA6416A_OUT_PORT_1, 0x40);
	if (ret != EC_SUCCESS)
		return ret;

	ret = tca6416a_write_byte(1, TCA6416A_DIR_PORT_1, 0xb8);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * Init TCA6424A, PORT 0
	 * NAME                      | DIR | Initial setting
	 * -------------------------------------------------
	 * BIT-0 (EN_PP5000_ALT_3P3) | O   | 0
	 * BIT-1 (EN_PP3300_ETH)     | O   | 1
	 * BIT-2 (EN_PP3300_DP)      | O   | 0
	 * BIT-3 (FAULT_CLEAR_CC)    | O   | 0
	 * BIT-4 (EN_VOUT_BUF_CC1)   | O   | 0
	 * BIT-5 (EN_VOUT_BUF_CC2)   | O   | 0
	 * BIT-6 (DUT_CHG_EN)        | O   | 0
	 * BIT-7 (HOST_OR_CHG_CTL    | O   | 0
	 */
	ret = tca6424a_write_byte(1, TCA6424A_OUT_PORT_0, 0x02);
	if (ret != EC_SUCCESS)
		return ret;

	ret = tca6424a_write_byte(1, TCA6424A_DIR_PORT_0, 0x00);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * Init TCA6424A, PORT 1
	 * NAME                           | DIR | Initial setting
	 * ------------------------------------------------------
	 * BIT-0 (USERVO_FAULT_L)         | I   | x
	 * BIT-1 (USB3_A0_FAULT_L)        | I   | x
	 * BIT-2 (USB3_A1_FAULT_L)        | I   | x
	 * BIT-3 (USB_DUTCHG_FLT_ODL)     | I   | x
	 * BIT-4 (PP3300_DP_FAULT_L)      | I   | x
	 * BIT-5 (DAC_BUF1_LATCH_FAULT_L) | I   | x
	 * BIT-6 (DAC_BUF2_LATCH_FAULT_L) | I   | x
	 * BIT-7 (PP5000_SRC_SEL)         | I   | x
	 */
	ret = tca6424a_write_byte(1, TCA6424A_DIR_PORT_1, 0xff);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * Init TCA6424A, PORT 2
	 * NAME                      | DIR | Initial setting
	 * ------------------------------------------------
	 * BIT-0 (VBUS_DISCHRG_EN)   | O   | 0
	 * BIT-1 (USBH_PWRDN_L)      | O   | 1
	 * BIT-2 (UNUSED)            | I   | x
	 * BIT-3 (UNUSED)            | I   | x
	 * BIT-4 (UNUSED)            | I   | x
	 * BIT-5 (UNUSED)            | I   | x
	 * BIT-6 (UNUSED)            | I   | x
	 * BIT-7 (DBG_LED_K_ODL)     | O   | 0
	 */
	ret = tca6424a_write_byte(1, TCA6424A_OUT_PORT_2, 0x02);
	if (ret != EC_SUCCESS)
		return ret;

	ret = tca6424a_write_byte(1, TCA6424A_DIR_PORT_2, 0x7c);
	if (ret != EC_SUCCESS)
		return ret;

	/* Clear any faults */
	read_faults();

	return EC_SUCCESS;
}

inline int sbu_uart_sel(int en)
{
	return tca6416a_write_bit(1, TCA6416A_OUT_PORT_0, 0, en);
}

inline int atmel_reset_l(int en)
{
	return tca6416a_write_bit(1, TCA6416A_OUT_PORT_0, 1, en);
}

inline int sbu_flip_sel(int en)
{
	return tca6416a_write_bit(1, TCA6416A_OUT_PORT_0, 2, en);
}

inline int usb3_a0_mux_sel(int en)
{
	return tca6416a_write_bit(1, TCA6416A_OUT_PORT_0, 3, en);
}

inline int usb3_a0_mux_en_l(int en)
{
	return tca6416a_write_bit(1, TCA6416A_OUT_PORT_0, 4, en);
}

inline int usb3_a0_pwr_en(int en)
{
	return tca6416a_write_bit(1, TCA6416A_OUT_PORT_0, 5, en);
}

inline int uart_18_sel(int en)
{
	return tca6416a_write_bit(1, TCA6416A_OUT_PORT_0, 6, en);
}

inline int uservo_power_en(int en)
{
	return tca6416a_write_bit(1, TCA6416A_OUT_PORT_0, 7, en);
}

inline int uservo_fastboot_mux_sel(enum uservo_fastboot_mux_sel_t sel)
{
	return tca6416a_write_bit(1, TCA6416A_OUT_PORT_1, 0, sel);
}

inline int usb3_a1_pwr_en(int en)
{
	return tca6416a_write_bit(1, TCA6416A_OUT_PORT_1, 1, en);
}

inline int usb3_a1_mux_sel(int en)
{
	return tca6416a_write_bit(1, TCA6416A_OUT_PORT_1, 2, en);
}

inline int board_id_det(void)
{
	int id;

	id = tca6416a_read_byte(1, TCA6416A_IN_PORT_1);
	if (id < 0)
		return id;

	/* Board ID consists of bits 5, 4, and 3 */
	return (id >> 3) & 0x7;
}

inline int cmux_en(int en)
{
	return tca6416a_write_bit(1, TCA6416A_OUT_PORT_1, 6, en);
}

inline int dongle_det(void)
{
	return tca6416a_read_bit(1, TCA6416A_IN_PORT_1, 7);
}

inline int en_pp5000_alt_3p3(int en)
{
	return tca6424a_write_bit(1, TCA6424A_OUT_PORT_0, 0, en);
}

inline int en_pp3300_eth(int en)
{
	return tca6424a_write_bit(1, TCA6424A_OUT_PORT_0, 1, en);
}

inline int en_pp3300_dp(int en)
{
	return tca6424a_write_bit(1, TCA6424A_OUT_PORT_0, 2, en);
}

inline int fault_clear_cc(int en)
{
	return tca6424a_write_bit(1, TCA6424A_OUT_PORT_0, 3, en);
}

inline int en_vout_buf_cc1(int en)
{
	return tca6424a_write_bit(1, TCA6424A_OUT_PORT_0, 4, en);
}

inline int en_vout_buf_cc2(int en)
{
	return tca6424a_write_bit(1, TCA6424A_OUT_PORT_0, 5, en);
}

int dut_chg_en(int en)
{
	dut_chg_en_state = en;
	return tca6424a_write_bit(1, TCA6424A_OUT_PORT_0, 6, en);
}

int get_dut_chg_en(void)
{
	return dut_chg_en_state;
}

inline int host_or_chg_ctl(int en)
{
	return tca6424a_write_bit(1, TCA6424A_OUT_PORT_0, 7, en);
}

inline int read_faults(void)
{
	return tca6424a_read_byte(1, TCA6424A_IN_PORT_1);
}

inline int vbus_dischrg_en(int en)
{
	return tca6424a_write_bit(1, TCA6424A_OUT_PORT_2, 0, en);
}

inline int usbh_pwrdn_l(int en)
{
	return tca6424a_write_bit(1, TCA6424A_OUT_PORT_2, 1, en);
}

inline int tca_gpio_dbg_led_k_odl(int en)
{
	return tca6424a_write_bit(1, TCA6424A_OUT_PORT_2, 7, !en);
}
