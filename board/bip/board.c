/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Bip board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "driver/bc12/bq24392.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/usb_mux_it5205.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "tcpci.h"
#include "uart.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

static void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_USB_C0_PD_INT_ODL)
		sn5s330_interrupt(0);
	else if (signal == GPIO_USB_C1_PD_INT_ODL)
		sn5s330_interrupt(1);
}

#include "gpio_list.h" /* Must come after other header files. */
/******************************************************************************/
/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus C0 sensing (10x voltage divider). PPVAR_USB_C0_VBUS */
	[ADC_VBUS_C0] = {
		"VBUS_C0", 10*ADC_MAX_MVOLT, ADC_READ_MAX+1, 0, CHIP_ADC_CH13},
	/* Vbus C1 sensing (10x voltage divider). PPVAR_USB_C1_VBUS */
	[ADC_VBUS_C1] = {
		"VBUS_C1", 10*ADC_MAX_MVOLT, ADC_READ_MAX+1, 0, CHIP_ADC_CH14},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
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

/******************************************************************************/
/* SPI devices */
/* TODO(b/75972988): Fill out correctly (SPI FLASH) */
const struct spi_device_t spi_devices[] = {
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/******************************************************************************/
/* I2C port map. */
const struct i2c_port_t i2c_ports[] = {
/* TODO(b/77139726): increase I2C bus speeds after bringup. */
	{"power",  IT83XX_I2C_CH_A, 100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"sensor", IT83XX_I2C_CH_B, 100, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"usbc0",  IT83XX_I2C_CH_C, 100, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"usbc1",  IT83XX_I2C_CH_E, 100, GPIO_I2C4_SCL, GPIO_I2C4_SDA},
	{"eeprom", IT83XX_I2C_CH_F, 100, GPIO_I2C5_SCL, GPIO_I2C5_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#define USB_PD_PORT_0 0
#define USB_PD_PORT_1 1

/******************************************************************************/
/* USB-C TCPC config. */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_0] = {
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it83xx_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
	[USB_PD_PORT_1] = {
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it83xx_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
};

/******************************************************************************/
/* USB-C MUX config */
static void board_it83xx_hpd_status(int port, int hpd_lvl, int hpd_irq);

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_0] = {
		/* Driver uses I2C_PORT_USB_MUX as I2C port */
		.port_addr = IT5205_I2C_ADDR1,
		.driver = &it5205_usb_mux_driver,
		.hpd_update = &board_it83xx_hpd_status,
	},
	[USB_PD_PORT_1] = {
		/* Use PS8751 as mux only */
		.port_addr = MUX_PORT_AND_ADDR(
			I2C_PORT_USBC1, PS8751_I2C_ADDR1),
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &board_it83xx_hpd_status,
	}
};

/******************************************************************************/
/* USB-C PPC config */
const struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_0] = {
		.i2c_port = I2C_PORT_USBC0,
		.i2c_addr = SN5S330_ADDR0,
		.drv = &sn5s330_drv
	},
	[USB_PD_PORT_1] = {
		.i2c_port = I2C_PORT_USBC1,
		.i2c_addr = SN5S330_ADDR0,
		.drv = &sn5s330_drv
	},
};
const unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/******************************************************************************/
/* USB-C BC 1.2 chip Configuration */
const struct bq24392_config_t bq24392_config[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_0] = {
		.chip_enable_pin = GPIO_USB_C0_BC12_VBUS_ON,
		.chg_det_pin = GPIO_USB_C0_BC12_CHG_DET_L,
		.flags = BQ24392_FLAGS_CHG_DET_ACTIVE_LOW,
	},
	[USB_PD_PORT_1] = {
		.chip_enable_pin = GPIO_USB_C1_BC12_VBUS_ON,
		.chg_det_pin = GPIO_USB_C1_BC12_CHG_DET_L,
		.flags = BQ24392_FLAGS_CHG_DET_ACTIVE_LOW,
	},
};

/******************************************************************************/
/* USB-A config */
const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_USB_A0_5V,
	GPIO_EN_USB_A1_5V,
};

/******************************************************************************/
/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	/*
	 * F3 key scan cycle completed but scan input is not
	 * charging to logic high when EC start scan next
	 * column for "T" key, so we set .output_settle_us
	 * to 80us from 50us.
	 */
	.output_settle_us = 80,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};


/* TODO(crbug.com/826441): Consolidate this logic with other impls */
static void board_it83xx_hpd_status(int port, int hpd_lvl, int hpd_irq)
{
	enum gpio_signal gpio = port ?
		GPIO_USB_C1_HPD_1V8_ODL : GPIO_USB_C0_HPD_1V8_ODL;

	/* Invert HPD level since GPIOs are active low. */
	hpd_lvl = !hpd_lvl;

	gpio_set_level(gpio, hpd_lvl);
	if (hpd_irq) {
		gpio_set_level(gpio, 1);
		msleep(1);
		gpio_set_level(gpio, hpd_lvl);
	}
}

void board_pd_vconn_ctrl(int port, int cc_pin, int enabled)
{
	/*
	 * We ignore the cc_pin because the polarity should already be set
	 * correctly in the PPC driver via the pd state machine.
	 */
	if (ppc_set_vconn(port, enabled) != EC_SUCCESS)
		cprints(CC_USBPD, "C%d: Failed %sabling vconn",
			port, enabled ? "en" : "dis");
}

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
	/*
	 * C0: The internal TCPC on ITE EC does not have a reset signal, but
	 * it will get reset when the EC gets reset.
	 */

	/* C1: Assert reset to TCPC (PS8751) for required delay (1ms) */
	gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 0);
	msleep(PS8XXX_RESET_DELAY_MS);
	gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 1);
}

void board_overcurrent_event(int port)
{
	/* TODO(b/78344554): pass this signal upstream once hardware reworked */
	cprints(CC_USBPD, "p%d: overcurrent!", port);
}


uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	/*
	 * Since C0 TCPC is embedded within EC, we don't need the PDCMD tasks
	 * to query the (embedded) TCPC status since chip driver code will
	 * handles its own interrupts and forward the correct events to
	 * the PD_C0 task. See it83xx/intc.c
	 */

	/* Check C1 interrupt pin to let PDCMD task know to query TCPC */
	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C1_PD_RST_ODL))
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

void chipset_pre_init_callback(void)
{
	/* Dummy until chipset support is added. */
}
