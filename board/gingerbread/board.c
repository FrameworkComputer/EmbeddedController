/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Gingerbread board-specific configuration */

#include "common.h"
#include "cros_board_info.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/stm32gx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/usb_mux/tusb1064.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "mp4245.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "usb_descriptor.h"
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
/*
 * C1 port on gingerbread does not have a PPC. However, C0 port does have a PPC
 * and therefore PPC related config options are defined. Defining a null driver
 * here so that functions from usbc_ppc.c will correctly dereference to a NULL
 * function pointer.
 */
const struct ppc_drv board_ppc_null_drv = {};

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

static void board_pwr_btn_interrupt(enum gpio_signal signal)
{
	baseboard_power_button_evt(gpio_get_level(signal));
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
	{ GPIO_EN_PP5000_A, 1, 31 },
	{ GPIO_EN_PP3300_A, 1, 135 },
	{ GPIO_EN_BB, 1, 30 },
	{ GPIO_EN_PP1100_A, 1, 30 },
	{ GPIO_EN_PP1000_A, 1, 20 },
	{ GPIO_EN_PP1050_A, 1, 30 },
	{ GPIO_EN_PP1200_A, 1, 20 },
	{ GPIO_EN_PP5000_HSPORT, 1, 31 },
	{ GPIO_EN_DP_SINK, 1, 80 },
	{ GPIO_MST_LP_CTL_L, 1, 80 },
	{ GPIO_MST_RST_L, 1, 41 },
	{ GPIO_EC_HUB1_RESET_L, 1, 41 },
	{ GPIO_EC_HUB2_RESET_L, 1, 33 },
	{ GPIO_USBC_DP_PD_RST_L, 1, 100 },
	{ GPIO_USBC_UF_RESET_L, 1, 33 },
	{ GPIO_DEMUX_DUAL_DP_PD_N, 1, 100 },
	{ GPIO_DEMUX_DUAL_DP_RESET_N, 1, 100 },
	{ GPIO_DEMUX_DP_HDMI_PD_N, 1, 10 },
	{ GPIO_DEMUX_DUAL_DP_MODE, 1, 10 },
	{ GPIO_DEMUX_DP_HDMI_MODE, 1, 1 },
};

const size_t board_power_seq_count = ARRAY_SIZE(board_power_seq);

/*
 * Define the strings used in our USB descriptors.
 */
const void *const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google LLC"),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Gingerbread"),
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
		.i2c_port = I2C_PORT_I2C3,
		.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
	},
};
#endif

#ifdef SECTION_IS_RW

/* TUSB1064 set mux board tuning for DP Rx path */
static int board_tusb1064_dp_rx_eq_set(const struct usb_mux *me,
				       mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		rv = tusb1064_set_dp_rx_eq(me, TUSB1064_DP_EQ_RX_8_9_DB);

	return rv;
}

/*
 * TCPCs: 2 USBC/PD ports
 *     port 0 -> host port              -> STM32G4 UCPD
 *     port 1 -> user data/display port -> PS8805
 */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		.drv = &stm32gx_tcpm_drv,
	},
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_I2C3,
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
			.i2c_addr_flags = TUSB1064_I2C_ADDR0_FLAGS,
			.driver = &tusb1064_usb_mux_driver,
			.board_set = &board_tusb1064_dp_rx_eq_set,
		},
	},
	[USB_PD_PORT_DP] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_DP,
			.i2c_port = I2C_PORT_I2C3,
			.i2c_addr_flags = PS8XXX_I2C_ADDR2_FLAGS,
			.driver = &tcpci_tcpm_usb_mux_driver,
			.hpd_update = &ps8xxx_tcpc_update_hpd_status,
		},
	},
};

/* USB-C PPC Configuration */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_HOST] = { .i2c_port = I2C_PORT_I2C3,
			       .i2c_addr_flags = SN5S330_ADDR0_FLAGS,
			       .drv = &sn5s330_drv },
	[USB_PD_PORT_DP] = { .drv = &board_ppc_null_drv },
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
	/*
	 * Reset all TCPCs.
	 *   C0 -> ucpd (on chip TCPC)
	 *   C1 -> PS8805 TCPC -> USBC_DP_PD_RST_L
	 *   C2 -> PS8803 TCPC -> USBC_UF_RESET_L
	 */
	gpio_set_level(GPIO_USBC_DP_PD_RST_L, 0);
	gpio_set_level(GPIO_USBC_UF_RESET_L, 0);
	crec_msleep(PS8805_FW_INIT_DELAY_MS);
	gpio_set_level(GPIO_USBC_DP_PD_RST_L, 1);
	gpio_set_level(GPIO_USBC_UF_RESET_L, 1);
	crec_msleep(PS8805_FW_INIT_DELAY_MS);
}

/* Power Delivery and charging functions */
void board_enable_usbc_interrupts(void)
{
	board_reset_pd_mcu();

	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_HOST_USBC_PPC_INT_ODL);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USBC_DP_MUX_ALERT_ODL);

	/* Enable HPD interrupt */
	gpio_enable_interrupt(GPIO_DDI_MST_IN_HPD);
}

/* Power Delivery and charging functions */
void board_disable_usbc_interrupts(void)
{
	/* Disable PPC interrupts. */
	gpio_disable_interrupt(GPIO_HOST_USBC_PPC_INT_ODL);

	/* Disable TCPC interrupts. */
	gpio_disable_interrupt(GPIO_USBC_DP_MUX_ALERT_ODL);

	/* Disable HPD interrupt */
	gpio_disable_interrupt(GPIO_DDI_MST_IN_HPD);
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

int ppc_get_alert_status(int port)
{
	if (port == USB_PD_PORT_HOST)
		return gpio_get_level(GPIO_HOST_USBC_PPC_INT_ODL) == 0;

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

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* TODO: b/ - check correct operation for honeybuns */
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
	 * The EC needs to keep the USB hubs in reset until the host port is
	 * attached so that the USB-EP can be properly enumerated.
	 */
	if (port == USB_PD_PORT_HOST) {
		gpio_set_level(GPIO_EC_HUB1_RESET_L, 1);
		gpio_set_level(GPIO_EC_HUB2_RESET_L, 1);
	}
}
DECLARE_HOOK(HOOK_USB_PD_CONNECT, board_usb_tc_connect, HOOK_PRIO_DEFAULT);

static void board_usb_tc_disconnect(void)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());

	/* Only the host port disconnect is relevant */
	if (port == USB_PD_PORT_HOST) {
		gpio_set_level(GPIO_EC_HUB1_RESET_L, 0);
		gpio_set_level(GPIO_EC_HUB2_RESET_L, 0);
	}
}
DECLARE_HOOK(HOOK_USB_PD_DISCONNECT, board_usb_tc_disconnect,
	     HOOK_PRIO_DEFAULT);

#endif /* SECTION_IS_RW */

static void board_init(void)
{
#ifdef SECTION_IS_RW
	/*
	 * Set current limit for USB 3.1 Gen 2 ports to 1.5 A. Note, this is
	 * also done in gpio.inc, but needs to be in RW for platforms which
	 * shipped with RO that set these 2 lines to the 900 mA level.
	 */
	gpio_set_level(GPIO_USB3_P3_CDP_EN, 1);
	gpio_set_level(GPIO_USB3_P4_CDP_EN, 1);
#endif
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
