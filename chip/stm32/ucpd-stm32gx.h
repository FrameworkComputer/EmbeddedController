/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_UCPD_STM32GX_H
#define __CROS_EC_UCPD_STM32GX_H

/* STM32 UCPD driver for Chrome EC */

#include "usb_pd_tcpm.h"

/*
 * UCPD is fed directly from HSI which is @ 16MHz. The ucpd_clk goes to
 * a prescaler who's output feeds the 'half-bit' divider which is used
 * to generate clock for delay counters and BMC Rx/Tx blocks. The rx is
 * designed to work in freq ranges of 6 <--> 18 MHz, however recommended
 * range is 9 <--> 18 MHz.
 *
 *          ------- @ 16 MHz ---------   @ ~600 kHz   -------------
 * HSI ---->| /psc |-------->| /hbit |--------------->| trans_cnt |
 *          -------          ---------    |           -------------
 *                                        |           -------------
 *                                        |---------->| ifrgap_cnt|
 *                                                    -------------
 * Requirements:
 *   1. hbit_clk ~= 600 kHz: 16 MHz / 600 kHz = 26.67
 *   2. tTransitionWindow - 12 to 20 uSec
 *   3. tInterframGap - uSec
 *
 * hbit_clk = HSI_clk / 27 = 592.6 kHz = 1.687 uSec period
 * tTransitionWindow = 1.687 uS * 8 = 13.5 uS
 * tInterFrameGap = 1.687 uS * 17 = 28.68 uS
 */

#define UCPD_PSC_DIV 1
#define UCPD_HBIT_DIV 27
#define UCPD_TRANSWIN_CNT 8
#define UCPD_IFRGAP_CNT 17


/*
 * K-codes and ordered set defines. These codes and sets are used to encode
 * which type of USB-PD message is being sent. This information can be found in
 * the USB-PD spec section 5.4 - 5.6. This info is also included in the STM32G4
 * TRM (RM0440) 45.4.3
 */
#define UCPD_SYNC1 0x18u
#define UCPD_SYNC2 0x11u
#define UCPD_SYNC3 0x06u
#define UCPD_RST1  0x07u
#define UCPD_RST2  0x19u
#define UCPD_EOP   0x0Du

/* This order of this enum matches tcpm_sop_type */
enum ucpd_tx_ordset {
	TX_ORDERSET_SOP =	(UCPD_SYNC1 |
				(UCPD_SYNC1<<5u) |
				(UCPD_SYNC1<<10u) |
				(UCPD_SYNC2<<15u)),

	TX_ORDERSET_SOP_PRIME =	(UCPD_SYNC1 |
				(UCPD_SYNC1<<5u) |
				(UCPD_SYNC3<<10u) |
				(UCPD_SYNC3<<15u)),

	TX_ORDERSET_SOP_PRIME_PRIME =	(UCPD_SYNC1 |
				(UCPD_SYNC3<<5u) |
				(UCPD_SYNC1<<10u) |
				(UCPD_SYNC3<<15u)),

	TX_ORDERSET_SOP_PRIME_DEBUG =	(UCPD_SYNC1 |
					(UCPD_RST2<<5u) |
					(UCPD_RST2<<10u) |
					(UCPD_SYNC3<<15u)),

	TX_ORDERSET_SOP_PRIME_PRIME_DEBUG =	(UCPD_SYNC1 |
					(UCPD_RST2<<5u) |
					(UCPD_SYNC3<<10u) |
					(UCPD_SYNC2<<15u)),

	TX_ORDERSET_HARD_RESET =	(UCPD_RST1  |
					(UCPD_RST1<<5u) |
					(UCPD_RST1<<10u)  |
					(UCPD_RST2<<15u)),

	TX_ORDERSET_CABLE_RESET =	(UCPD_RST1 |
					(UCPD_SYNC1<<5u) |
					(UCPD_RST1<<10u)  |
					(UCPD_SYNC3<<15u)),
};


/**
 * STM32Gx UCPD implementation of tcpci .init method
 *
 * @param usbc_port -> USB-C Port number
 * @return EC_SUCCESS
 */
int stm32gx_ucpd_init(int usbc_port);

/**
 * STM32Gx UCPD implementation of tcpci .release method
 *
 * @param usbc_port -> USB-C Port number
 * @return EC_SUCCESS
 */
int stm32gx_ucpd_release(int usbc_port);

/**
 * STM32Gx UCPD implementation of tcpci .get_cc method
 *
 * @param usbc_port -> USB-C Port number
 * @param *cc1 -> pointer to cc1 result
 * @param *cc2 -> pointer to cc2 result
 * @return EC_SUCCESS
 */
int stm32gx_ucpd_get_cc(int usbc_port, enum tcpc_cc_voltage_status *cc1,
			enum tcpc_cc_voltage_status *cc2);

/**
 * STM32Gx equivalent for TCPCI role_control register
 *
 * @param usbc_port -> USB-C Port number
 * @return EC_SUCCESS
 */
int stm32gx_ucpd_get_role_control(int usbc_port);

/**
 * STM32Gx UCPD implementation of tcpci .set_cc method
 *
 * @param usbc_port -> USB-C Port number
 * @param cc_pull -> Rp or Rd selection
 * @param rp -> value of Rp (if cc_pull == Rp)
 * @return EC_SUCCESS
 */
int stm32gx_ucpd_set_cc(int usbc_port, int cc_pull, int rp);

/**
 * STM32Gx UCPD implementation of tcpci .set_cc method
 *
 * @param usbc_port -> USB-C Port number
 * @param polarity -> CC1 or CC2 selection
 * @return EC_SUCCESS
 */
int stm32gx_ucpd_set_polarity(int usbc_port, enum tcpc_cc_polarity polarity);

/**
 * STM32Gx UCPD implementation of tcpci .set_rx_enable method
 *
 * @param usbc_port -> USB-C Port number
 * @param enable -> on/off for USB-PD messages
 * @return EC_SUCCESS
 */
int stm32gx_ucpd_set_rx_enable(int port, int enable);

/**
 * STM32Gx UCPD implementation of tcpci .set_msg_header method
 *
 * @param usbc_port -> USB-C Port number
 * @param power_role -> port's current power role
 * @param data_role -> port's current data role
 * @return EC_SUCCESS
 */
int stm32gx_ucpd_set_msg_header(int port, int power_role, int data_role);

/**
 * STM32Gx UCPD implementation of tcpci .transmit method
 *
 * @param usbc_port -> USB-C Port number
 * @param type -> SOP/SOP'/SOP'' etc
 * @param header -> usb pd message header
 * @param *data -> pointer to message contents
 * @return EC_SUCCESS
 */
int stm32gx_ucpd_transmit(int port,
			enum tcpm_sop_type type,
			uint16_t header,
			  const uint32_t *data);

/**
 * STM32Gx UCPD implementation of tcpci .get_message_raw method
 *
 * @param usbc_port -> USB-C Port number
 * @param *payload -> pointer to where message should be written
 * @param *head -> pointer to message header
 * @return EC_SUCCESS
 */
int stm32gx_ucpd_get_message_raw(int port, uint32_t *payload, int *head);

/**
 * STM32Gx method to remove Rp when VCONN is being supplied
 *
 * @param usbc_port -> USB-C Port number
 * @param enable -> connect/disc Rp
 * @return EC_SUCCESS
 */
int stm32gx_ucpd_vconn_disc_rp(int port, int enable);

/**
 * STM32Gx UCPD implementation of tcpci .sop_prime_enable method
 *
 * @param usbc_port -> USB-C Port number
 * @param enable -> control of SOP'/SOP'' messages
 * @return EC_SUCCESS
 */
int stm32gx_ucpd_sop_prime_enable(int port, bool enable);

int stm32gx_ucpd_get_chip_info(int port, int live,
			       struct ec_response_pd_chip_info_v1 *chip_info);

/**
 * This function is used to enable/disable a ucpd debug feature that is used to
 * mark the ucpd message log when there is a usbc detach event.
 *
 * @param enable -> on/off control for debug feature
 */
void ucpd_cc_detect_notify_enable(int enable);

/**
 * This function is used to enable/disable rx bist test mode in the ucpd
 * driver. This mode is controlled at the PE layer. When this mode is enabled,
 * the ucpd receiver will not pass BIST data messages to the protocol layer and
 * only send GoodCRC replies.
 *
 * @param usbc_port -> USB-C Port number
 * @param enable -> on/off control for rx bist mode
 */
enum ec_error_list stm32gx_ucpd_set_bist_test_mode(const int port,
						   const bool enable);

#endif /* __CROS_EC_UCPD_STM32GX_H */
