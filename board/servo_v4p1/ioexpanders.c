/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpander.h"
#include "ioexpanders.h"

#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/******************************************************************************
 * Initialize IOExpanders.
 */

#define PCAL6524HE_PORT TCA6424A_PORT
#define PCAL6524HE_ADDR TCA6424A_ADDR
#define PCAL6524HE_DEVICE_ID_ADDR 0x7c
#define PCAL6524HE_DEVICE_ID_REG 0x46
#define PCAL6524HE_DEVICE_ID0 0
#define PCAL6524HE_DEVICE_ID1 0x08
#define PCAL6524HE_DEVICE_ID2 0x30
#define PCAL6524HE_INT_MASK_REG_PORT1 0x55
#define PCAL6524HE_INT_MASK_REG_PORT2 0x56

static enum servo_board_id board_id_val = BOARD_ID_UNSET;

#ifdef SECTION_IS_RO

static int dut_chg_en_state;
static int bc12_charger;

/* Enable all ioexpander outputs. */
int init_ioexpanders(void)
{
	int fault, irqs;
	uint8_t dat[3];

	/*
	 * Due to shortages of the TI TCA6424 device, the NXP PCAL6524HE
	 * device has been selected as an alternative IOExpander. The two
	 * parts are mostly compatible except for some additional initialization
	 * of the PCAL6524HE.
	 *
	 * If the PCAL6524HE device is detected, the interrupt mask register for
	 * port1 is set to 0 and the port2 is set to 0xbe.
	 */

	/* Attempt to read the device id register of the PCAL6524HE device */
	i2c_read_block(PCAL6524HE_PORT, PCAL6524HE_DEVICE_ID_ADDR,
		       PCAL6524HE_DEVICE_ID_REG, dat, 3);

	if (dat[2] == PCAL6524HE_DEVICE_ID2 &&
	    dat[1] == PCAL6524HE_DEVICE_ID1 &&
	    dat[0] == PCAL6524HE_DEVICE_ID0) {
		CPRINTF("Detected PCAL6524HE\n");
		i2c_write8(PCAL6524HE_PORT, PCAL6524HE_ADDR,
			   PCAL6524HE_INT_MASK_REG_PORT1, 0);
		i2c_write8(PCAL6524HE_PORT, PCAL6524HE_ADDR,
			   PCAL6524HE_INT_MASK_REG_PORT2, 0xbe);
	} else {
		CPRINTF("Detected TCA6424A\n");
	}

	/* Clear any faults and other IRQs */
	fault = read_faults();
	irqs = read_irqs();

	if (!(fault & USB_DUTCHG_FLT_ODL)) {
		CPRINTF("FAULT: Overcurrent on Charger or DUT CC/SBU lines\n");
	}

	if ((!!(irqs & HOST_CHRG_DET) != bc12_charger) &&
	    (board_id_det() <= BOARD_ID_REV1)) {
		CPRINTF("BC1.2 charger %s\n",
			(irqs & HOST_CHRG_DET) ? "plugged" : "unplugged");
		bc12_charger = !!(irqs & HOST_CHRG_DET);
	}

	return EC_SUCCESS;
}

static void ioexpanders_irq(void)
{
	int fault, irqs;

	fault = read_faults();
	irqs = read_irqs();

	if (!(fault & USERVO_FAULT_L)) {
		ec_uservo_power_en(0);
		CPRINTF("FAULT: Microservo USB A port load switch\n");
	}

	if (!(fault & USB3_A0_FAULT_L)) {
		ec_usb3_a0_pwr_en(0);
		CPRINTF("FAULT: USB3 A0 port load switch\n");
	}

	if (!(fault & USB3_A1_FAULT_L)) {
		ec_usb3_a1_pwr_en(0);
		CPRINTF("FAULT: USB3 A1 port load switch\n");
	}

	if (!(fault & USB_DUTCHG_FLT_ODL)) {
		CPRINTF("FAULT: Overcurrent on Charger or DUT CC/SBU lines\n");
	}

	if (!(fault & PP3300_DP_FAULT_L)) {
		CPRINTF("FAULT: Overcurrent on DisplayPort\n");
	}

	if (!(fault & DAC_BUF1_LATCH_FAULT_L)) {
		CPRINTF("FAULT: CC1 drive circuitry has exceeded thermal "
			"or current limits. The CC1 DAC has been disabled "
			"and disconnected.\n");

		en_vout_buf_cc1(0);
	}

	if (!(fault & DAC_BUF2_LATCH_FAULT_L)) {
		CPRINTF("FAULT: CC2 drive circuitry has exceeded thermal "
			"or current limits. The CC2 DAC has been disabled "
			"and disconnected.\n");

		en_vout_buf_cc2(0);
	}

	/*
	 * In case of both DACs' faults, we should clear them only after
	 * disabling both DACs.
	 */
	if ((fault & (DAC_BUF1_LATCH_FAULT_L | DAC_BUF2_LATCH_FAULT_L)) !=
	    (DAC_BUF1_LATCH_FAULT_L | DAC_BUF2_LATCH_FAULT_L)) {
		fault_clear_cc(1);
		fault_clear_cc(0);
	}

	if ((!!(irqs & HOST_CHRG_DET) != bc12_charger) &&
	    (board_id_det() <= BOARD_ID_REV1)) {
		CPRINTF("BC1.2 charger %s\n",
			(irqs & HOST_CHRG_DET) ? "plugged" : "unplugged");
		bc12_charger = !!(irqs & HOST_CHRG_DET);
	}

	if (!(irqs & SYS_PWR_IRQ_ODL)) {
		CPRINTF("System full power threshold exceeded\n");
	}
}
DECLARE_DEFERRED(ioexpanders_irq);

int irq_ioexpanders(void)
{
	hook_call_deferred(&ioexpanders_irq_data, 0);
	return 0;
}

inline int sbu_uart_sel(int en)
{
	return ioex_set_level(IOEX_SBU_UART_SEL, en);
}

inline int atmel_reset_l(int en)
{
	return ioex_set_level(IOEX_ATMEL_RESET_L, en);
}

inline int sbu_flip_sel(int en)
{
	return ioex_set_level(IOEX_SBU_FLIP_SEL, en);
}

inline int usb3_a0_mux_sel(int en)
{
	return ioex_set_level(IOEX_USB3_A0_MUX_SEL, en);
}

inline int usb3_a0_mux_en_l(int en)
{
	return ioex_set_level(IOEX_USB3_A0_MUX_EN_L, en);
}

inline int ec_usb3_a0_pwr_en(int en)
{
	return ioex_set_level(IOEX_USB3_A0_PWR_EN, en);
}

inline int uart_18_sel(int en)
{
	return ioex_set_level(IOEX_UART_18_SEL, en);
}

inline int ec_uservo_power_en(int en)
{
	return ioex_set_level(IOEX_USERVO_POWER_EN, en);
}

inline int uservo_fastboot_mux_sel(enum uservo_fastboot_mux_sel_t sel)
{
	return ioex_set_level(IOEX_USERVO_FASTBOOT_MUX_SEL, (int)sel);
}

inline int ec_usb3_a1_pwr_en(int en)
{
	return ioex_set_level(IOEX_USB3_A1_PWR_EN, en);
}

inline int usb3_a1_mux_sel(int en)
{
	return ioex_set_level(IOEX_USB3_A1_MUX_SEL, en);
}

inline int board_id_det(void)
{
	if (board_id_val == BOARD_ID_UNSET) {
		int id;

		/* Cache board ID at init */
		if (ioex_get_port(IOEX_GET_INFO(IOEX_BOARD_ID_DET0)->ioex,
				  IOEX_GET_INFO(IOEX_BOARD_ID_DET0)->port, &id))
			return id;

		/* Board ID consists of bits 5, 4, and 3 */
		board_id_val = (id >> BOARD_ID_DET_OFFSET) & BOARD_ID_DET_MASK;
	}

	return board_id_val;
}

inline int dongle_det(void)
{
	int val;
	ioex_get_level(IOEX_DONGLE_DET, &val);
	return val;
}

inline int get_host_chrg_det(void)
{
	int val;
	ioex_get_level(IOEX_HOST_CHRG_DET, &val);
	return val;
}

inline int en_pp5000_alt_3p3(int en)
{
	return ioex_set_level(IOEX_EN_PP5000_ALT_3P3, en);
}

inline int en_pp3300_eth(int en)
{
	return ioex_set_level(IOEX_EN_PP3300_ETH, en);
}

inline int en_pp3300_dp(int en)
{
	return ioex_set_level(IOEX_EN_PP3300_DP, en);
}

inline int fault_clear_cc(int en)
{
	return ioex_set_level(IOEX_FAULT_CLEAR_CC, en);
}

inline int en_vout_buf_cc1(int en)
{
	return ioex_set_level(IOEX_EN_VOUT_BUF_CC1, en);
}

inline int en_vout_buf_cc2(int en)
{
	return ioex_set_level(IOEX_EN_VOUT_BUF_CC2, en);
}

int dut_chg_en(int en)
{
	dut_chg_en_state = en;
	return ioex_set_level(IOEX_DUT_CHG_EN, en);
}

int get_dut_chg_en(void)
{
	return dut_chg_en_state;
}

inline int host_or_chg_ctl(int en)
{
	return ioex_set_level(IOEX_HOST_OR_CHG_CTL, en);
}

inline int read_faults(void)
{
	int val;

	ioex_get_port(IOEX_GET_INFO(IOEX_USERVO_FAULT_L)->ioex,
		      IOEX_GET_INFO(IOEX_USERVO_FAULT_L)->port, &val);

	return val;
}

inline int read_irqs(void)
{
	int val;

	ioex_get_port(IOEX_GET_INFO(IOEX_SYS_PWR_IRQ_ODL)->ioex,
		      IOEX_GET_INFO(IOEX_SYS_PWR_IRQ_ODL)->port, &val);

	return val;
}

inline int vbus_dischrg_en(int en)
{
	return ioex_set_level(IOEX_VBUS_DISCHRG_EN, en);
}

inline int usbh_pwrdn_l(int en)
{
	return ioex_set_level(IOEX_USBH_PWRDN_L, en);
}

inline int tca_gpio_dbg_led_k_odl(int en)
{
	return ioex_set_level(IOEX_TCA_GPIO_DBG_LED_K_ODL, !en);
}

#else /* SECTION_IS_RO */

/*
 * Due to lack of flash in RW section, it is not possible to use IOEX subsystem
 * in it. Instead, RO section uses IOEX, and RW implements only required
 * function with raw i2c operation. This function is required by 'version'
 * console command and should work without any special initialization.
 */
inline int board_id_det(void)
{
	if (board_id_val == BOARD_ID_UNSET) {
		int id;
		int res;

		/* Cache board ID at init */
		res = i2c_read8(TCA6416A_PORT, TCA6416A_ADDR, BOARD_ID_DET_PORT,
				&id);
		if (res != EC_SUCCESS)
			return res;

		/* Board ID consists of bits 5, 4, and 3 */
		board_id_val = (id >> BOARD_ID_DET_OFFSET) & BOARD_ID_DET_MASK;
	}

	/* Board ID consists of bits 5, 4, and 3 */
	return board_id_val;
}

#endif /* SECTION_IS_RO */
