/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "button.h"
#include "charge_ramp.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "driver/charger/bq25710.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "driver/accel_bma2x2_public.h"
#include "driver/accel_bma422.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "fw_config.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "lid_switch.h"
#include "power_button.h"
#include "power.h"
#include "ps8xxx.h"
#include "registers.h"
#include "switch.h"
#include "tablet_mode.h"
#include "throttle_ap.h"
#include "usbc_config.h"

#include "gpio_list.h" /* Must come after other header files. */

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

/******************************************************************************/
/* USB-A charging control */

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_PP5000_USBA_R,
};
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) == USB_PORT_COUNT);

/******************************************************************************/

__override void board_cbi_init(void)
{
	config_usb_db_type();

	/*
	 * If keyboard is US2(KB_LAYOUT_1), we need translate right ctrl
	 * to backslash(\|) key.
	 */
	if (ec_cfg_keyboard_layout() == KB_LAYOUT_1)
		set_scancode_set2(4, 0, get_scancode_set2(2, 7));
}

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	/* Allow keyboard backlight to be enabled */

	/* TODO(b/190783131)
	 * Need to implement specific keyboard backlight control method.
	 */
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	/* Turn off the keyboard backlight if it's on. */

	/* TODO(b/190783131)
	 * Need to implement specific keyboard backlight control method.
	 */
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_CHARGE_RAMP_SW

/*
 * TODO(b/181508008): tune this threshold
 */

#define BC12_MIN_VOLTAGE 4400

/**
 * Return true if VBUS is too low
 */
int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	int voltage;

	if (charger_get_vbus_voltage(port, &voltage))
		voltage = 0;

	if (voltage == 0) {
		CPRINTS("%s: must be disconnected", __func__);
		return 1;
	}

	if (voltage < BC12_MIN_VOLTAGE) {
		CPRINTS("%s: port %d: vbus %d lower than %d", __func__, port,
			voltage, BC12_MIN_VOLTAGE);
		return 1;
	}

	return 0;
}

#endif /* CONFIG_CHARGE_RAMP_SW */

enum battery_present battery_hw_present(void)
{
	enum gpio_signal batt_pres;

	batt_pres = GPIO_EC_BATT_PRES_ODL;

	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(batt_pres) ? BP_NO : BP_YES;
}

static void board_init(void)
{
	/* The PPVAR_SYS must same as battery voltage(3 cells * 4.4V) */
	if (extpower_is_present() && battery_hw_present()) {
		bq25710_set_min_system_voltage(CHARGER_SOLO, 9200);
	} else {
		bq25710_set_min_system_voltage(CHARGER_SOLO, 13200);
	}
}
DECLARE_HOOK(HOOK_SECOND, board_init, HOOK_PRIO_DEFAULT);

__overridable void board_ps8xxx_tcpc_init(int port)
{
	int val;

	if (i2c_read8(I2C_PORT_USB_C1_TCPC, PS8XXX_I2C_ADDR1_P1_FLAGS,
		      PS8815_REG_APTX_EQ_AT_10G, &val))
		CPRINTS("ps8815: fail to read reg 0x%02x",
			PS8815_REG_APTX_EQ_AT_10G);

	/* APTX2 EQ 23dB, APTX1 EQ 23dB */
	if (i2c_write8(I2C_PORT_USB_C1_TCPC, PS8XXX_I2C_ADDR1_P1_FLAGS,
		       PS8815_REG_APTX_EQ_AT_10G, 0x99))
		CPRINTS("ps8815: fail to write reg 0x%02x",
			PS8815_REG_APTX_EQ_AT_10G);

	if (i2c_read8(I2C_PORT_USB_C1_TCPC, PS8XXX_I2C_ADDR1_P1_FLAGS,
		      PS8815_REG_RX_EQ_AT_10G, &val))
		CPRINTS("ps8815: fail to read reg 0x%02x",
			PS8815_REG_RX_EQ_AT_10G);

	/* RX2 EQ 18dB, RX1 EQ 16dB */
	if (i2c_write8(I2C_PORT_USB_C1_TCPC, PS8XXX_I2C_ADDR1_P1_FLAGS,
		       PS8815_REG_RX_EQ_AT_10G, 0x64))
		CPRINTS("ps8815: fail to write reg 0x%02x",
			PS8815_REG_RX_EQ_AT_10G);

	if (i2c_read8(I2C_PORT_USB_C1_TCPC, PS8XXX_I2C_ADDR1_P1_FLAGS,
		      PS8815_REG_APTX_EQ_AT_5G, &val))
		CPRINTS("ps8815: fail to read reg 0x%02x",
			PS8815_REG_APTX_EQ_AT_5G);

	/* APTX2 EQ 16dB, APTX1 EQ 16dB */
	if (i2c_write8(I2C_PORT_USB_C1_TCPC, PS8XXX_I2C_ADDR1_P1_FLAGS,
		       PS8815_REG_APTX_EQ_AT_5G, 0x44))
		CPRINTS("ps8815: fail to write reg 0x%02x",
			PS8815_REG_APTX_EQ_AT_5G);

	if (i2c_read8(I2C_PORT_USB_C1_TCPC, PS8XXX_I2C_ADDR1_P1_FLAGS,
		      PS8815_REG_RX_EQ_AT_5G, &val))
		CPRINTS("ps8815: fail to read reg 0x%02x",
			PS8815_REG_RX_EQ_AT_5G);

	/* RX2 EQ 16dB, RX1 EQ 16dB */
	if (i2c_write8(I2C_PORT_USB_C1_TCPC, PS8XXX_I2C_ADDR1_P1_FLAGS,
		       PS8815_REG_RX_EQ_AT_5G, 0x44))
		CPRINTS("ps8815: fail to write reg 0x%02x",
			PS8815_REG_RX_EQ_AT_5G);
}

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	/*
	 * Follow OEM request to limit the input current to
	 * 90% negotiated limit.
	 */
	charge_ma = charge_ma * 90 / 100;

	charge_set_input_current_limit(
		MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}
