/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_IOEXPANDERS_H
#define __CROS_EC_IOEXPANDERS_H

enum uservo_fastboot_mux_sel_t {
	MUX_SEL_USERVO = 0,
	MUX_SEL_FASTBOOT = 1
};

/*
 * Initialize Ioexpanders
 */
int init_ioexpanders(void);

/*
 * Calls the Ioexpanders Deferred handler for interrupts
 * Should be called from the ioexpanders IRQ handler
 */
int irq_ioexpanders(void);

/**
 * SBU Crosspoint select
 *
 * @param en	0 - HOST SBU to DUT SBU connected
 *		1 - STM UART to DUT SBU connected
 * @return EC_SUCCESS or EC_xxx on error
 */
int sbu_uart_sel(int en);

/**
 * Host KBC Controller reset
 *
 * @param en	0 - Assert reset
 *		1 - Deassert reset
 * @return EC_SUCCESS or EC_xxx on error
 */
int atmel_reset_l(int en);

/**
 * SBU Crosspoint polarity flip for DTU SBU to STM USART/Host SBU
 *
 * @param en	0 - Retain polarity (no inversion)
 *		1 - Swap P for N polarity
 * @return EC_SUCCESS or EC_xxx on error
 */
int sbu_flip_sel(int en);

/**
 * USB data path for general USB type A port
 *
 * @param en	0 - Host hub
 *		1 - DUT hub
 * @return EC_SUCCESS or EC_xxx on error
 */
int usb3_a0_mux_sel(int en);

/**
 * USB data path enable for general USB type A port, first on J2
 *
 * @param en	0 - Data connected / enabled
 *		1 - Data disconnected
 * @return EC_SUCCESS or EC_xxx on error
 */
int usb3_a0_mux_en_l(int en);

/**
 * Controls load switches for 5V to general USB type A
 *
 * @param en	0 - Disable power
 *		1 - Enable power
 * @return EC_SUCCESS or EC_xxx on error
 */
int usb3_a0_pwr_en(int en);

/**
 * Controls logic to select 1.8V or 3.3V UART from STM to DUT on SBU lines
 *
 * @param en	0 - 3.3V level
 *		1 - 1.8V level
 * @return EC_SUCCESS or EC_xxx on error
 */
int uart_18_sel(int en);

/**
 * Controls load switches for 5V to uservo USB type A port
 *
 * @param en	0 - Disable power
 *		1 - Enable power
 * @return EC_SUCCESS or EC_xxx on error
 */
int uservo_power_en(int en);

/**
 * USB data path enable from host hub to downstream userv or DUT peripheral
 *
 * @param sel	MUX_SEL_USERVO - hub connected to uservo
 *		MUX_SEL_FASTBOOT - hub connected to DUT
 * @return EC_SUCCESS or EC_xxx on error
 */
int uservo_fastboot_mux_sel(enum uservo_fastboot_mux_sel_t sel);

/**
 * Controls load switches for 5V to general USB type A second port
 *
 * @param en	0 - power off
 *		1 - power enabled
 * @return EC_SUCCESS or EC_xxx on error
 */
int usb3_a1_pwr_en(int en);

/**
 * USB data path for general USB type A port, second on J2
 *
 * @param en	0 - Host hub
 *		1 - DUT hub
 * @return EC_SUCCESS or EC_xxx on error
 */
int usb3_a1_mux_sel(int en);

/**
 * Reads the 3-bit Servo V4.1 version ID
 *
 * @return version ID
 */
int board_id_det(void);

/**
 * USBC 4:6 redriver enable
 *
 * @param en	0 - TUSB1064 disabled
 *		1 - TUSB1064 enabled
 * @return EC_SUCCESS or EC_xxx on error
 */
int cmux_en(int en);

/**
 * Reads the TypeA/TypeC DUT cable assembly pigtail
 *
 * @return	0 - for TypeA
 *		1 - for TypeC
 */
int dongle_det(void);

/**
 * Enable signal for supplemental power supply. This supply will support higher
 * wattage servo needs. 10ms after enabling this bit, the server supply should
 * switch over from the host supply and the higher wattage will be available
 *
 * @param en	0 - Alternate supply disabled
 *		1 - Supply enabled
 * @return EC_SUCCESS or EC_xxx on error
 */
int en_pp5000_alt_3p3(int en);

/**
 * Controls load switches for the RTL8153. By default, ethernet is enabled but
 * power can be removed as a way to clear any bad conditions
 *
 * @param en	0 - disable power
 *		1 - enable power
 * @return EC_SUCCESS or EC_xxx on error
 */
int en_pp3300_eth(int en);

/**
 * Controls load switches that enables 3.3V supply on the Display Port
 * connector. On by default
 *
 * @param en	0 - disable 3.3V to DP connector
 *		1 - enable 3.3V to DP connector
 * @return EC_SUCCESS or EC_xxx on error
 */
int en_pp3300_dp(int en);

/**
 * The rising edge of this signal clears the latched condition when thermal
 * or overcurrent fault has occurred from both CC1 and CC2 channels. Note
 * that if the CC drive circuitry continues to be overheated, it will reset
 * the fault regardless of the FAULT_CLEAR_CC signal.
 *
 * @param en	0 to 1 transition - clear fault
 *		1 to 0 transition, 0, or 1 - No change in fault
 * @return EC_SUCCESS or EC_xxx on error
 */
int fault_clear_cc(int en);

/**
 * CC1 Drive circuitry enable
 *
 * @param en	0 - disable CC1 high output drive (normal CC Operation by STM)
 *		1 - enable CC1 high drive output
 * @return EC_SUCCESS or EC_xxx on error
 */
int en_vout_buf_cc1(int en);

/**
 * CC2 Drive circuitry enable
 *
 * @param en	0 - disable CC2 high output drive (normal CC Operation by STM)
 *		1 - enable CC2 high drive output
 * @return EC_SUCCESS or EC_xxx on error
 */
int en_vout_buf_cc2(int en);

/**
 * Controls load switches for servo to power DUT Vusb
 *
 * @param en	0 - disable power
 *		1 - enable power
 * @return EC_SUCCESS or EC_xxx on error
 */
int dut_chg_en(int en);

/**
 * Get state of DUT Vusb
 *
 * @return	0 - power is disabled
 *		1 - power is enabled
 * @return EC_SUCCESS or EC_xxx on error
 */
int get_dut_chg_en(void);

/**
 * Selects power source for DUT Vusb from servo
 *
 * @param en	0 - 5V
 *		1 - charger Vbus
 * @return EC_SUCCESS or EC_xxx on error
 */
int host_or_chg_ctl(int en);

#define USERVO_FAULT_L		BIT(0)
#define USB3_A0_FAULT_L		BIT(1)
#define USB3_A1_FAULT_L		BIT(2)
#define USB_DUTCHG_FLT_ODL	BIT(3)
#define PP3300_DP_FAULT_L	BIT(4)
#define DAC_BUF1_LATCH_FAULT_L	BIT(5)
#define DAC_BUF2_LATCH_FAULT_L	BIT(6)
#define PP5000_SRC_SEL		BIT(7)

/**
 * Read any faults that may have occurred. A fault has occurred if the
 * corresponding bit is 0.
 *
 * BIT:
 *   0 (USERVO_FAULT_L) - Fault for port microservo USB A load switch
 *   1 (USB3_A0_FAULT_L) - Fault for general port USB A load switch
 *   2 (USB3_A1_FAULT_L) - Fault for general port USB A load switch
 *   3 (USB_DUTCHG_FLT_ODL) - Overcurrent fault on Charger or DUB CC/SBU lines
 *   4 (PP3300_DP_FAULT_L) - Overcurrent fault on DisplayPort
 *   5 (DAC_BUF1_LATCH_FAULT_L) - Fault to indicate CC drive circuitry has
 *				  exceeded thermal limits or exceeded current
 *				  limits; when faults occur, the driver is
 *				  disabled and needs to be reset.
 *   6 (DAC_BUF2_LATCH_FAULT_L) - Fault to indicate CC drive circuitry has
 *				  exceeded thermal limits or exceeded current
 *				  limits; when faults occur, the driver is
 *				  disabled and needs to be reset.
 *   7 (PP5000_SRC_SEL) - Used to monitor whether Host power or Servo Charger
 *			  USBC is providing source to PP5000. This may flip
 *			  sources upon fault and should be monitored.
 *			  0 - USBC Servo charger is source
 *			  1 - host cable is source
 */
int read_faults(void);

/**
 * Enables active discharge for USB DUT Charger
 *
 * @param en	0 - disable active discharge (default)
 *		1 - enable active discharge circuitry
 * @return EC_SUCCESS or EC_xxx on error
 */
int vbus_dischrg_en(int en);

/**
 * Enables Hub
 *
 * @param en	0 - place hub in suspend (low power state)
 *		1 - enable hub activity (including i2c)
 * @return EC_SUCCESS or EC_xxx on error
 */
int usbh_pwrdn_l(int en);

/**
 * Debug LED
 *
 * @param en	0 - LED is OFF
 *		1 - LED is ON
 * @return EC_SUCCESS or EC_xxx on error
 */
int tca_gpio_dbg_led_k_odl(int en);

#endif /* __CROS_EC_IOEXPANDERS_H */
