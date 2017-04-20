/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charge_state_v2.h"
#include "console.h"
#include "hooks.h"
#include "task.h"
#include "tcpci.h"
#include "system.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PTN5110_EXT_GPIO_CONFIG		0x92
#define PTN5110_EXT_GPIO_CONTROL	0x93

#define PTN5110_EXT_GPIO_FRS_EN			(1 << 6)
#define PTN5110_EXT_GPIO_EN_SRC			(1 << 5)
#define PTN5110_EXT_GPIO_EN_SNK1		(1 << 4)
#define PTN5110_EXT_GPIO_IILIM_5V_VBUS_L	(1 << 3)

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{NPCX_I2C_PORT0_1, 0xA0, &tcpci_tcpm_drv, TCPC_ALERT_ACTIVE_LOW},
	{NPCX_I2C_PORT0_1, 0xA4, &tcpci_tcpm_drv, TCPC_ALERT_ACTIVE_LOW},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == CONFIG_USB_PD_PORT_COUNT);

static int board_charger_port_is_sourcing_vbus(int port)
{
	int reg;

	if (tcpc_read(port, PTN5110_EXT_GPIO_CONTROL, &reg))
		return 0;

	return !!(reg & PTN5110_EXT_GPIO_EN_SRC);
}

static int ptn5110_ext_gpio_enable(int port, int enable, int gpio)
{
	int reg;
	int rv;

	rv = tcpc_read(port, PTN5110_EXT_GPIO_CONTROL, &reg);
	if (rv)
		return rv;

	if (enable)
		reg |= gpio;
	else
		reg &= ~gpio;

	return tcpc_write(port, PTN5110_EXT_GPIO_CONTROL, reg);
}

void board_charging_enable(int port, int enable)
{
	ptn5110_ext_gpio_enable(port, enable, PTN5110_EXT_GPIO_EN_SNK1);
}

void board_vbus_enable(int port, int enable)
{
	ptn5110_ext_gpio_enable(port, enable, PTN5110_EXT_GPIO_EN_SRC);
}

void tcpc_alert_event(enum gpio_signal signal)
{
#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

void board_tcpc_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_to_this_image())
		board_reset_pd_mcu();

	/* Enable TCPC0/1 interrupt */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

int board_tcpc_post_init(int port)
{
	int reg;
	int rv;

	rv = tcpc_read(port, PTN5110_EXT_GPIO_CONFIG, &reg);
	if (rv)
		return rv;

	/* Configure PTN5110 External GPIOs as output */
	reg |=  PTN5110_EXT_GPIO_EN_SRC | PTN5110_EXT_GPIO_EN_SNK1 |
		PTN5110_EXT_GPIO_IILIM_5V_VBUS_L;
	rv = tcpc_write(port, PTN5110_EXT_GPIO_CONFIG, reg);
	if (rv)
		return rv;

	return ptn5110_ext_gpio_enable(port, 1,
					PTN5110_EXT_GPIO_IILIM_5V_VBUS_L);
}

/* Reset PD MCU */
void board_reset_pd_mcu(void)
{
	/* TODO: Add reset logic */
}

int board_set_active_charge_port(int port)
{
	/* charge port is a realy physical port */
	int is_real_port = (port >= 0 &&
			port < CONFIG_USB_PD_PORT_COUNT);
	/* check if we are source vbus on that port */
	int source = board_charger_port_is_sourcing_vbus(port);

	if (is_real_port && source) {
		CPRINTS("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("New chg p%d", port);

	if (port != CHARGE_PORT_NONE) {
		/* Make sure non-charging port is disabled */
		board_charging_enable(port, 1);
		board_charging_enable(!port, 0);
	} else {
		/* Disable both ports */
		board_charging_enable(0, 0);
		board_charging_enable(1, 0);
	}

	return EC_SUCCESS;
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_0;

	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charge_set_input_current_limit(MAX(charge_ma,
				CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}

int adc_read_channel(enum adc_channel ch)
{
	return 0;
}
