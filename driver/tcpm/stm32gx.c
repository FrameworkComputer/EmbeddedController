/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TCPM for STM32Gx UCPD module */

#include "chip/stm32/ucpd-stm32gx.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "registers.h"
#include "stm32gx.h"
#include "system.h"
#include "task.h"
#include "tcpci.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "hooks.h"

#if defined(CONFIG_USB_PD_VBUS_DETECT_TCPC) || \
	defined(CONFIG_USB_PD_TCPC_LOW_POWER)
#error "Unsupported config options of Stm32gx PD driver"
#endif

/* Wait time for vconn power switch to turn off. */
#ifndef PD_STM32GX_VCONN_TURN_OFF_DELAY_US
#define PD_STM32GX_VCONN_TURN_OFF_DELAY_US 500
#endif

static int cached_rp[CONFIG_USB_PD_PORT_MAX_COUNT];


static int stm32gx_tcpm_get_message_raw(int port, uint32_t *buf, int *head)
{
	/* TODO(b/167601672): Need to implement this for USB-PD support */
	return EC_SUCCESS;
}

static int stm32gx_tcpm_init(int port)
{
	return stm32gx_ucpd_init(port);
}

static int stm32gx_tcpm_release(int port)
{
	return stm32gx_ucpd_release(port);
}

static int stm32gx_tcpm_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
	enum tcpc_cc_voltage_status *cc2)
{
	/* Get cc_state value for each CC line */
	stm32gx_ucpd_get_cc(port, cc1, cc2);

	return EC_SUCCESS;
}

static int stm32gx_tcpm_select_rp_value(int port, int rp_sel)
{
	cached_rp[port] = rp_sel;

	return EC_SUCCESS;
}

static int stm32gx_tcpm_set_cc(int port, int pull)
{
	return stm32gx_ucpd_set_cc(port, pull, cached_rp[port]);
}

static int stm32gx_tcpm_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	return stm32gx_ucpd_set_polarity(port, polarity);
}

static int stm32gx_tcpm_set_vconn(int port, int enable)
{
	/*
	 * TODO(b/167601672): VCONN is not provided by ucpd peripheral, so the
	 * only action required here will be to remove Rp from the CC line that
	 * is supplying VCONN.
	 */
	return EC_SUCCESS;
}

static int stm32gx_tcpm_set_msg_header(int port, int power_role, int data_role)
{
	/* TODO(b/167601672): Need to implement this for USB-PD support */
	return EC_SUCCESS;
}

static int stm32gx_tcpm_set_rx_enable(int port, int enable)
{
	/* TODO(b/167601672): Need to implement this for USB-PD support */
	return EC_SUCCESS;
}

static int stm32gx_tcpm_transmit(int port,
			enum tcpm_transmit_type type,
			uint16_t header,
			const uint32_t *data)
{
	/* TODO(b/167601672): Need to implement this for USB-PD support */
	return EC_SUCCESS;
}

static int stm32gx_tcpm_sop_prime_disable(int port)
{
	/* TODO(b/167601672): Need to implement this for USB-PD support */
	return EC_SUCCESS;
}


static int stm32gx_tcpm_get_chip_info(int port, int live,
			struct ec_response_pd_chip_info_v1 *chip_info)
{
	/* TODO(b/167601672): Need to implement this for USB-PD support */
	return EC_SUCCESS;
}

static void stm32gx_tcpm_sw_reset(void)
{
	/*
	 * TODO(b/167601672): Not sure if this hook is required for UCPD as
	 * opposed to TCPCI compliant TCPC. Leaving this a placeholder so I
	 * don't forget to pull this back in, if required.
	 */
}
DECLARE_HOOK(HOOK_USB_PD_DISCONNECT, stm32gx_tcpm_sw_reset, HOOK_PRIO_DEFAULT);

const struct tcpm_drv stm32gx_tcpm_drv = {
	.init			= &stm32gx_tcpm_init,
	.release		= &stm32gx_tcpm_release,
	.get_cc			= &stm32gx_tcpm_get_cc,
	.select_rp_value	= &stm32gx_tcpm_select_rp_value,
	.set_cc			= &stm32gx_tcpm_set_cc,
	.set_polarity		= &stm32gx_tcpm_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
       .sop_prime_disable      = &stm32gx_tcpm_sop_prime_disable,
#endif

	.set_vconn		= &stm32gx_tcpm_set_vconn,
	.set_msg_header		= &stm32gx_tcpm_set_msg_header,
	.set_rx_enable		= &stm32gx_tcpm_set_rx_enable,
	.get_message_raw	= &stm32gx_tcpm_get_message_raw,
	.transmit		= &stm32gx_tcpm_transmit,
	.get_chip_info		= &stm32gx_tcpm_get_chip_info,
};
