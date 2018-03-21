/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Yorp board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "driver/bc12/bq24392.h"
#include "driver/charger/bd9995x.h"
#include "driver/ppc/nx20p3483.h"
#include "driver/tcpm/anx74xx.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "tcpci.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define USB_PD_PORT_ANX74XX	0
#define USB_PD_PORT_PS8751	1

static void tcpc_alert_event(enum gpio_signal signal)
{
	/* TODO(b/74127309): Flesh out USB code */
}

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_PD_C0_INT_L:
		nx20p3483_interrupt(0);
		break;

	case GPIO_USB_PD_C1_INT_L:
		nx20p3483_interrupt(1);
		break;

	default:
		break;
	}
}

/* Must come after other header files and GPIO interrupts*/
#include "gpio_list.h"

/* Wake pins */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus C0 sensing (10x voltage divider). PPVAR_USB_C0_VBUS */
	[ADC_VBUS_C0] = {
		"VBUS_C0", NPCX_ADC_CH4, ADC_MAX_VOLT*10, ADC_READ_MAX+1, 0},
	/* Vbus C1 sensing (10x voltage divider). PPVAR_USB_C1_VBUS */
	[ADC_VBUS_C1] = {
		"VBUS_C1", NPCX_ADC_CH9, ADC_MAX_VOLT*10, ADC_READ_MAX+1, 0},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);


/* Power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
#ifdef CONFIG_POWER_S0IX
	{GPIO_PCH_SLP_S0_L,
		POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_DISABLE_AT_BOOT,
		"SLP_S0_DEASSERTED"},
#endif
	{GPIO_PCH_SLP_S3_L,   POWER_SIGNAL_ACTIVE_HIGH, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,   POWER_SIGNAL_ACTIVE_HIGH, "SLP_S4_DEASSERTED"},
	{GPIO_SUSPWRDNACK,    POWER_SIGNAL_ACTIVE_HIGH,
	 "SUSPWRDNACK_DEASSERTED"},

	{GPIO_ALL_SYS_PGOOD,  POWER_SIGNAL_ACTIVE_HIGH, "ALL_SYS_PGOOD"},
	{GPIO_RSMRST_L_PGOOD, POWER_SIGNAL_ACTIVE_HIGH, "RSMRST_L"},
	{GPIO_PP3300_PG,      POWER_SIGNAL_ACTIVE_HIGH, "PP3300_PG"},
	{GPIO_PP5000_PG,      POWER_SIGNAL_ACTIVE_HIGH, "PP5000_PG"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* I2C port map. */
const struct i2c_port_t i2c_ports[] = {
/* TODO(b/74387239): increase I2C bus speeds after bringup. */
	{"battery", I2C_PORT_BATTERY, 100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"tcpc0",   I2C_PORT_TCPC0,   100, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"tcpc1",   I2C_PORT_TCPC1,   100, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"eeprom",  I2C_PORT_EEPROM,  100, GPIO_I2C3_SCL, GPIO_I2C3_SDA},
	{"charger", I2C_PORT_CHARGER, 100, GPIO_I2C4_SCL, GPIO_I2C4_SDA},
	{"sensor",  I2C_PORT_SENSOR,  100, GPIO_I2C7_SCL, GPIO_I2C7_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* USB-A port configuration */
const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_USB_A_5V,
	/* TODO(b/74388692): Add second port control after hardware fix. */
};

/* Called by APL power state machine when transitioning from G3 to S5 */
static void chipset_pre_init(void)
{
	/* Enable 5.0V and 3.3V rails, and wait for Power Good */
	gpio_set_level(GPIO_EN_PP5000, 1);
	gpio_set_level(GPIO_EN_PP3300, 1);
	while (!gpio_get_level(GPIO_PP5000_PG) ||
	       !gpio_get_level(GPIO_PP3300_PG))
		;

	/* Enable PMIC */
	gpio_set_level(GPIO_PMIC_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, chipset_pre_init, HOOK_PRIO_DEFAULT);

/* Called by APL power state machine when transitioning to G3. */
void chipset_do_shutdown(void)
{
	/* Disable PMIC */
	gpio_set_level(GPIO_PMIC_EN, 0);

	/* Disable 5.0V and 3.3V rails, and wait until they power down. */
	gpio_set_level(GPIO_EN_PP5000, 0);
	gpio_set_level(GPIO_EN_PP3300, 0);
	while (gpio_get_level(GPIO_PP5000_PG) ||
	       gpio_get_level(GPIO_PP3300_PG))
		;
}

static void board_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_PD_C0_INT_L);
	gpio_enable_interrupt(GPIO_USB_PD_C1_INT_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

enum adc_channel board_get_vbus_adc(int port)
{
	return port ? ADC_VBUS_C1 : ADC_VBUS_C0;
}

/**
 * Reset all system PD/TCPC MCUs -- currently only called from
 * handle_pending_reboot() in common/power.c just before hard
 * resetting the system. This logic is likely not needed as the
 * PP3300_A rail should be dropped on EC reset.
 */
void board_reset_pd_mcu(void)
{
	/* TODO(b/74127309): Flesh out USB code */
}

int board_set_active_charge_port(int port)
{
	/* TODO(b/74127309): Flesh out USB code */
	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	/* TODO(b/74127309): Flesh out USB code */
}

uint16_t tcpc_get_alert_status(void)
{
	/* TODO(b/74127309): Flesh out USB code */
	return 0;
}


/* Drivers */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_ANX74XX] = {
		.i2c_host_port = I2C_PORT_TCPC0,
		.i2c_slave_addr = 0x50,
		.drv = &anx74xx_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
	[USB_PD_PORT_PS8751] = {
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_slave_addr = 0x16,
		.drv = &ps8xxx_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_ANX74XX] = {
		.port_addr = USB_PD_PORT_ANX74XX,
		.driver = &anx74xx_tcpm_usb_mux_driver,
		.hpd_update = &anx74xx_tcpc_update_hpd_status,
	},
	[USB_PD_PORT_PS8751] = {
		.port_addr = USB_PD_PORT_PS8751,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	}
};

const struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_ANX74XX] = {
		.i2c_port = I2C_PORT_TCPC0,
		.i2c_addr = NX20P3483_ADDR0,
		.drv = &nx20p3483_drv,
	},
	[USB_PD_PORT_PS8751] = {
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr = NX20P3483_ADDR0,
		.drv = &nx20p3483_drv,
		.flags = PPC_CFG_FLAGS_GPIO_CONTROL,
		.snk_gpio = GPIO_USB_C1_CHARGE_ON,
		.src_gpio = GPIO_EN_USB_C1_5V_OUT,
	},
};
const unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* BC 1.2 chip Configuration */
const struct bq24392_config_t bq24392_config[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_ANX74XX] = {
		.chip_enable_pin = GPIO_USB_C0_BC12_VBUS_ON,
		.chg_det_pin = GPIO_USB_C0_BC12_CHG_DET_L,
		.flags = BQ24392_FLAGS_CHG_DET_ACTIVE_LOW,
	},
	[USB_PD_PORT_PS8751] = {
		.chip_enable_pin = GPIO_USB_C1_BC12_VBUS_ON,
		.chg_det_pin = GPIO_USB_C1_BC12_CHG_DET_L,
		.flags = BQ24392_FLAGS_CHG_DET_ACTIVE_LOW,
	},
};
