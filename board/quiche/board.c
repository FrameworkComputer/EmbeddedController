/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Quiche board-specific configuration */

#include "common.h"
#include "cros_board_info.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/stm32gx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/usb_mux/ps8822.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "usb_descriptor.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dp_ufp.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_tc_sm.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

#define QUICHE_PD_DEBUG_LVL 1

#ifdef SECTION_IS_RW
#define CROS_EC_SECTION "RW"
#else
#define CROS_EC_SECTION "RO"
#endif

#ifdef SECTION_IS_RW
static int pd_dual_role_init[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	PD_DRP_TOGGLE_ON,
	PD_DRP_FORCE_SOURCE,
};

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_HOST_USBC_PPC_INT_ODL:
		sn5s330_interrupt(USB_PD_PORT_HOST);
		break;
	case GPIO_USBC_DP_PPC_INT_ODL:
		sn5s330_interrupt(USB_PD_PORT_DP);
		break;

	default:
		break;
	}
}

static void tcpc_alert_event(enum gpio_signal s)
{
	int port = -1;

	switch (s) {
	case GPIO_USBC_DP_MUX_ALERT_ODL:
		port = USB_PD_PORT_DP;
		break;
	default:
		return;
	}
	schedule_deferred_pd_interrupt(port);
}

void hpd_interrupt(enum gpio_signal signal)
{
	usb_pd_hpd_edge_event(signal);
}

static void board_uf_manage_vbus_interrupt(enum gpio_signal signal)
{
	baseboard_usb3_check_state();
}

static void board_pwr_btn_interrupt(enum gpio_signal signal)
{
	baseboard_power_button_evt(gpio_get_level(signal));
}

static void board_usbc_usb3_interrupt(enum gpio_signal signal)
{
	baseboard_usbc_usb3_irq();
}
#endif /* SECTION_IS_RW */

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/*
 * Table GPIO signals control both power rails and reset lines to various chips
 * on the board. The order the signals are changed and the delay between GPIO
 * signals is driven by USB/MST hub power sequencing requirements.
 */
const struct power_seq board_power_seq[] = {
	{ GPIO_EN_AC_JACK, 1, 20 },
	{ GPIO_EC_DFU_MUX_CTRL, 0, 0 },
	{ GPIO_EN_PP5000_A, 1, 31 },
	{ GPIO_MST_LP_CTL_L, 1, 0 },
	{ GPIO_EN_PP3300_B, 1, 1 },
	{ GPIO_EN_PP1100_A, 1, 100 + 30 },
	{ GPIO_EN_BB, 1, 30 },
	{ GPIO_EN_PP1050_A, 1, 30 },
	{ GPIO_EN_PP1200_A, 1, 20 },
	{ GPIO_EN_PP5000_C, 1, 20 },
	{ GPIO_EN_PP5000_HSPORT, 1, 31 },
	{ GPIO_EN_DP_SINK, 1, 80 },
	{ GPIO_MST_RST_L, 1, 61 },
	{ GPIO_EC_HUB2_RESET_L, 1, 41 },
	{ GPIO_EC_HUB3_RESET_L, 1, 33 },
	{ GPIO_DP_SINK_RESET, 1, 100 },
	{ GPIO_USBC_DP_PD_RST_L, 1, 100 },
	{ GPIO_USBC_UF_RESET_L, 1, 33 },
	{ GPIO_DEMUX_DUAL_DP_PD_N, 1, 100 },
	{ GPIO_DEMUX_DUAL_DP_RESET_N, 1, 100 },
	{ GPIO_DEMUX_DP_HDMI_PD_N, 1, 10 },
	{ GPIO_DEMUX_DUAL_DP_MODE, 1, 10 },
	{ GPIO_DEMUX_DP_HDMI_MODE, 1, 5 },
};
const size_t board_power_seq_count = ARRAY_SIZE(board_power_seq);

/*
 * Define the strings used in our USB descriptors.
 */
const void *const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google LLC"),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Quiche"),
	[USB_STR_SERIALNO] = 0,
	[USB_STR_VERSION] =
		USB_STRING_DESC(CROS_EC_SECTION ":" CROS_EC_VERSION32),
	[USB_STR_UPDATE_NAME] = USB_STRING_DESC("Firmware update"),
};
BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

#ifndef SECTION_IS_RW
/* USB-C PPC Configuration */
struct ppc_config_t ppc_chips[] = {
	[USB_PD_PORT_HOST] = {
		.i2c_port = I2C_PORT_I2C1,
		.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
	},
};
#endif

#ifdef SECTION_IS_RW
/*
 * PS8802 set mux board tuning.
 * Adds in board specific gain and DP lane count configuration
 */
static int board_ps8822_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		rv = ps8822_set_dp_rx_eq(me, PS8822_DPEQ_LEVEL_UP_20DB);

	return rv;
}

/* TCPCs */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_HOST] = {
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		.drv = &stm32gx_tcpm_drv,
	},
	[USB_PD_PORT_DP] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_I2C1,
			.addr_flags = PS8XXX_I2C_ADDR2_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
};

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_HOST] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_HOST,
			.i2c_port = I2C_PORT_I2C1,
			.i2c_addr_flags = PS8822_I2C_ADDR3_FLAG,
			.driver = &ps8822_usb_mux_driver,
			.board_set = &board_ps8822_mux_set,
		},
	},
	[USB_PD_PORT_DP] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_DP,
			.i2c_port = I2C_PORT_I2C1,
			.i2c_addr_flags = PS8XXX_I2C_ADDR2_FLAGS,
			.driver = &tcpci_tcpm_usb_mux_driver,
			.hpd_update = &ps8xxx_tcpc_update_hpd_status,
		},
	},
};

/* USB-C PPC Configuration */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_HOST] = { .i2c_port = I2C_PORT_I2C1,
			       .i2c_addr_flags = SN5S330_ADDR0_FLAGS,
			       .drv = &sn5s330_drv },
	[USB_PD_PORT_DP] = { .i2c_port = I2C_PORT_I2C1,
			     .i2c_addr_flags = SN5S330_ADDR2_FLAGS,
			     .drv = &sn5s330_drv },
	[USB_PD_PORT_USB3] = { .i2c_port = I2C_PORT_I2C3,
			       .i2c_addr_flags = SN5S330_ADDR1_FLAGS,
			       .drv = &sn5s330_drv },
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

const struct hpd_to_pd_config_t hpd_config = {
	.port = USB_PD_PORT_HOST,
	.signal = GPIO_DDI_MST_IN_HPD,
};

void board_reset_pd_mcu(void)
{
	cprints(CC_SYSTEM, "Resetting TCPCs...");
	cflush();
	gpio_set_level(GPIO_USBC_DP_PD_RST_L, 0);
	gpio_set_level(GPIO_USBC_UF_RESET_L, 0);
	crec_msleep(PS8805_FW_INIT_DELAY_MS);
	gpio_set_level(GPIO_USBC_DP_PD_RST_L, 1);
	gpio_set_level(GPIO_USBC_UF_RESET_L, 1);
	crec_msleep(PS8805_FW_INIT_DELAY_MS);
}

void board_enable_usbc_interrupts(void)
{
	/* Enable C0 PPC interrupt */
	gpio_enable_interrupt(GPIO_HOST_USBC_PPC_INT_ODL);
	/* Enable C1 PPC interrupt */
	gpio_enable_interrupt(GPIO_USBC_DP_PPC_INT_ODL);
	/* Enable C0 HPD interrupt */
	gpio_enable_interrupt(GPIO_DDI_MST_IN_HPD);
	/* Enable C1 TCPC interrupt */
	gpio_enable_interrupt(GPIO_USBC_DP_MUX_ALERT_ODL);
}

void board_disable_usbc_interrupts(void)
{
	/* Disable C0 PPC interrupt */
	gpio_disable_interrupt(GPIO_HOST_USBC_PPC_INT_ODL);
	/* Disable C1 PPC interrupt */
	gpio_disable_interrupt(GPIO_USBC_DP_PPC_INT_ODL);
	/* Disable C0 HPD interrupt */
	gpio_disable_interrupt(GPIO_DDI_MST_IN_HPD);
	/* Disable C1 TCPC interrupt */
	gpio_disable_interrupt(GPIO_USBC_DP_MUX_ALERT_ODL);
	/* Disable VBUS control interrupt for C2 */
	gpio_disable_interrupt(GPIO_USBC_UF_MUX_VBUS_EN);
}

void board_tcpc_init(void)
{
	board_reset_pd_mcu();

	/* Enable board usbc interrupts */
	board_enable_usbc_interrupts();
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 2);

enum pd_dual_role_states board_tc_get_initial_drp_mode(int port)
{
	return pd_dual_role_init[port];
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	/*
	 * CONFIG_USB_PD_PORT_MAX_COUNT must be defined to account for C0, C1,
	 * and C2, but TCPMv2 only knows about C0 and C1, as C2 is a type-c only
	 * port that is managed directly by the PS8803 TCPC.
	 */
	return CONFIG_USB_PD_PORT_MAX_COUNT - 1;
}

int ppc_get_alert_status(int port)
{
	if (port == USB_PD_PORT_HOST)
		return gpio_get_level(GPIO_HOST_USBC_PPC_INT_ODL) == 0;
	else if (port == USB_PD_PORT_DP)
		return gpio_get_level(GPIO_USBC_DP_PPC_INT_ODL) == 0;

	return 0;
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USBC_DP_MUX_ALERT_ODL) &&
	    gpio_get_level(GPIO_USBC_DP_PD_RST_L))
		status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

static void board_usb_pd_dp_ocp_reset(void)
{
	gpio_set_level(GPIO_USBC_ALTMODE_OCP_NOTIFY, 1);
}
DECLARE_DEFERRED(board_usb_pd_dp_ocp_reset);

void board_overcurrent_event(int port, int is_overcurrented)
{
	if (port == USB_PD_PORT_DP) {
		gpio_set_level(GPIO_USBC_ALTMODE_OCP_NOTIFY, !is_overcurrented);
		hook_call_deferred(&board_usb_pd_dp_ocp_reset_data,
				   USB_HUB_OCP_RESET_MSEC);
	}
}

int dock_get_mf_preference(void)
{
	int rv;
	uint32_t fw_config;
	int mf = MF_OFF;

	/*
	 * MF (multi function) preferece is indicated by bit 0 of the fw_config
	 * data field. If this data field does not exist, then default to 4 lane
	 * mode.
	 */
	rv = cbi_get_fw_config(&fw_config);
	if (!rv)
		mf = CBI_FW_MF_PREFERENCE(fw_config);

	return mf;
}

static void board_usb_tc_connect(void)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());

	/*
	 * The EC needs to indicate to the MST hub when the host port is
	 * attached. GPIO_UFP_PLUG_DET is used for this purpose.
	 */
	if (port == USB_PD_PORT_HOST)
		gpio_set_level(GPIO_UFP_PLUG_DET, 0);
}
DECLARE_HOOK(HOOK_USB_PD_CONNECT, board_usb_tc_connect, HOOK_PRIO_DEFAULT);

static void board_usb_tc_disconnect(void)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());

	/* Only the host port disconnect is relevant */
	if (port == USB_PD_PORT_HOST)
		gpio_set_level(GPIO_UFP_PLUG_DET, 1);
}
DECLARE_HOOK(HOOK_USB_PD_DISCONNECT, board_usb_tc_disconnect,
	     HOOK_PRIO_DEFAULT);

#endif /* SECTION_IS_RW */

static void board_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_debug_gpio_1_pulse(void)
{
	gpio_set_level(GPIO_TRIGGER_1, 0);
}
DECLARE_DEFERRED(board_debug_gpio_1_pulse);

static void board_debug_gpio_2_pulse(void)
{
	gpio_set_level(GPIO_TRIGGER_2, 0);
}
DECLARE_DEFERRED(board_debug_gpio_2_pulse);

void board_debug_gpio(enum debug_gpio trigger, int level, int pulse_usec)
{
	switch (trigger) {
	case TRIGGER_1:
		gpio_set_level(GPIO_TRIGGER_1, level);
		if (pulse_usec)
			hook_call_deferred(&board_debug_gpio_1_pulse_data,
					   pulse_usec);
		break;
	case TRIGGER_2:
		gpio_set_level(GPIO_TRIGGER_2, level);
		if (pulse_usec)
			hook_call_deferred(&board_debug_gpio_2_pulse_data,
					   pulse_usec);
		break;
	default:
		CPRINTS("bad debug gpio selection");
		break;
	}
}

static int command_dplane(int argc, const char **argv)
{
	char *e;
	int lane;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	lane = strtoi(argv[1], &e, 10);

	if ((lane != 2) && (lane != 4))
		return EC_ERROR_PARAM1;

	/* put MST into reset */
	gpio_set_level(GPIO_MST_RST_L, 0);
	crec_msleep(1);
	/* Set lane control to requested level */
	gpio_set_level(GPIO_MST_HUB_LANE_SWITCH, lane == 2 ? 1 : 0);
	crec_msleep(1);
	/* Take MST out of reset */
	gpio_set_level(GPIO_MST_RST_L, 1);

	ccprintf("MST lane set:  %s, lane_ctrl = %d\n",
		 lane == 2 ? "2 lane" : "4 lane",
		 gpio_get_level(GPIO_MST_HUB_LANE_SWITCH));

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dplane, command_dplane, "<2 | 4>", "MST lane control.");
