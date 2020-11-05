/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Quiche board-specific configuration */

#include "common.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/stm32gx.h"
#include "driver/tcpm/tcpci.h"
#include "gpio.h"
#include "hooks.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_HOST_USBC_PPC_INT_ODL:
		sn5s330_interrupt(USB_PD_PORT_HOST);
		break;

	default:
		break;
	}
}

#include "gpio_list.h" /* Must come after other header files. */

/*
 * Table GPIO signals control both power rails and reset lines to various chips
 * on the board. The order the signals are changed and the delay between GPIO
 * signals is driven by USB/MST hub power sequencing requirements.
 */
const struct power_seq board_power_seq[] = {
	{GPIO_EN_AC_JACK,               1, 20},
	{GPIO_EN_PP5000_A,              1, 31},
	{GPIO_EN_PP3300_B,              1, 100},
	{GPIO_EN_BB,                    1, 30},
	{GPIO_EN_PP1100_A,              1, 30},
	{GPIO_EN_PP1050_A,              1, 30},
	{GPIO_EN_PP1200_A,              1, 20},
	{GPIO_EN_PP5000_C,              1, 20},
	{GPIO_EN_PP5000_HSPORT,         1, 31},
	{GPIO_EN_DP_SINK,               1, 80},
	{GPIO_MST_RST_L,                1, 20},
	{GPIO_MST_LP_CTL_L,             1, 41},
	{GPIO_EC_HUB2_RESET_L,          1, 41},
	{GPIO_EC_HUB3_RESET_L,          1, 33},
	{GPIO_DP_SINK_RESET,            1, 100},
	{GPIO_USBC_DP_PD_RST_L,         1, 100},
	{GPIO_USBC_UF_RESET_L,          1, 33},
	{GPIO_DEMUX_DUAL_DP_PD_N,       1, 100},
	{GPIO_DEMUX_DUAL_DP_RESET_N,    1, 100},
	{GPIO_DEMUX_DP_HDMI_PD_N,       1, 10},
	{GPIO_DEMUX_DUAL_DP_MODE,       1, 10},
	{GPIO_DEMUX_DP_HDMI_MODE,       1, 1},
};

const size_t board_power_seq_count = ARRAY_SIZE(board_power_seq);

/* TCPCs */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		.drv = &stm32gx_tcpm_drv,
	},
};

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_HOST] = {
		.usb_port = USB_PD_PORT_HOST,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
	},
};

/* USB-C PPC Configuration */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_HOST] = {
		.i2c_port = I2C_PORT_USBC,
		.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
		.drv = &sn5s330_drv
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* Power Delivery and charging functions */
void board_tcpc_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_HOST_USBC_PPC_INT_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

static void board_select_drp_mode(void)
{
	/*
	 * Host port should operate as a dual role port. If it attaches as a
	 * sink, then it will trigger a PRS to end up as a SRC UFP. The port's
	 * DRP state only needs to be set once, after it's initialized in TCPMv2
	 * as the default role of sink only.
	 */
	pd_set_dual_role(USB_PD_PORT_HOST, PD_DRP_TOGGLE_ON);
	CPRINTS("ucpd: set drp toggle on");
}
DECLARE_DEFERRED(board_select_drp_mode);

static void board_init(void)
{
	hook_call_deferred(&board_select_drp_mode_data, 50 * MSEC);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

int ppc_get_alert_status(int port)
{
	if (port == USB_PD_PORT_HOST)
		return gpio_get_level(GPIO_HOST_USBC_PPC_INT_ODL) == 0;

	return 0;
}

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* TODO(b/174825406): check correct operation for honeybuns */
}
