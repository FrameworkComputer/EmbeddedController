/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl9241.h"
#include "driver/ppc/aoz1380_public.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/retimer/ps8802.h"
#include "driver/retimer/ps8818_public.h"
#include "driver/retimer/tusb544.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/usb_mux/amd_fp5.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpander.h"
#include "task.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

const struct i2c_port_t i2c_ports[] = {
	{
		.name = "tcpc0",
		.port = I2C_PORT_TCPC0,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_A0_C0_SCL,
		.sda = GPIO_EC_I2C_USB_A0_C0_SDA,
	},
	{
		.name = "tcpc1",
		.port = I2C_PORT_TCPC1,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_A1_C1_SCL,
		.sda = GPIO_EC_I2C_USB_A1_C1_SDA,
	},
	{
		.name = "battery",
		.port = I2C_PORT_BATTERY,
		.kbps = 100,
		.scl = GPIO_EC_I2C_BATT_SCL,
		.sda = GPIO_EC_I2C_BATT_SDA,
	},
	{
		.name = "ap_mux",
		.port = I2C_PORT_USB_AP_MUX,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USBC_AP_MUX_SCL,
		.sda = GPIO_EC_I2C_USBC_AP_MUX_SDA,
	},
	{
		.name = "therm_chg",
		.port = I2C_PORT_THERMAL_AP,
		.kbps = 400,
		.scl = GPIO_FCH_SIC_POWER_SCL,
		.sda = GPIO_FCH_SID_POWER_SDA,
	},
	{
		.name = "sensor",
		.port = I2C_PORT_SENSOR,
		.kbps = 400,
		.scl = GPIO_EC_I2C_SENSOR_CBI_SCL,
		.sda = GPIO_EC_I2C_SENSOR_CBI_SDA,
	},
	{
		.name = "ap_audio",
		.port = I2C_PORT_AP_AUDIO,
		.kbps = 400,
		.scl = GPIO_FCH_I2C_AUDIO_SCL,
		.sda = GPIO_FCH_I2C_AUDIO_SDA,
	},
	{
		.name = "ap_hdmi",
		.port = I2C_PORT_AP_HDMI,
		.kbps = 400,
		.scl = GPIO_FCH_I2C_HDMI_HUB_3V3_SCL,
		.sda = GPIO_FCH_I2C_HDMI_HUB_3V3_SDA,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/*****************************************************************************
 * Charger
 */

struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER_V1,
		.i2c_addr_flags = ISL9241_ADDR_FLAGS,
		.drv = &isl9241_drv,
	},
};

/*****************************************************************************
 * TCPC
 */

void baseboard_tcpc_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PPC_FAULT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PPC_INT_ODL);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_TCPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_TCPC_INT_ODL);

	/* Enable BC 1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_ODL);

	/* Enable SBU fault interrupts */
	ioex_enable_interrupt(IOEX_USB_C0_SBU_FAULT_ODL);
	ioex_enable_interrupt(IOEX_USB_C1_SBU_FAULT_DB_ODL);
}
DECLARE_HOOK(HOOK_INIT, baseboard_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

struct ppc_config_t ppc_chips[] = {
	[USBC_PORT_C0] = {
		/* Device does not talk I2C */
		.drv = &aoz1380_drv
	},

	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr_flags = NX20P3483_ADDR1_FLAGS,
		.drv = &nx20p348x_drv
	},
};
BUILD_ASSERT(ARRAY_SIZE(ppc_chips) == USBC_PORT_COUNT);
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

__overridable void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_FAULT_ODL:
		aoz1380_interrupt(USBC_PORT_C0);
		break;

	case GPIO_USB_C1_PPC_INT_ODL:
		nx20p348x_interrupt(USBC_PORT_C1);
		break;

	default:
		break;
	}
}

int board_set_active_charge_port(int port)
{
	int is_valid_port = (port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;

	if (port == CHARGE_PORT_NONE) {
		CPRINTSUSB("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0; i < ppc_cnt; i++) {
			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (ppc_vbus_sink_enable(i, 0))
				CPRINTSUSB("Disabling C%d as sink failed.", i);
		}

		return EC_SUCCESS;
	} else if (!is_valid_port) {
		return EC_ERROR_INVAL;
	}

	/* Check if the port is sourcing VBUS. */
	if (ppc_is_sourcing_vbus(port)) {
		CPRINTFUSB("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTSUSB("New charge port: C%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < ppc_cnt; i++) {
		if (i == port)
			continue;

		if (ppc_vbus_sink_enable(i, 0))
			CPRINTSUSB("C%d: sink path disable failed.", i);
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTSUSB("C%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

void board_overcurrent_event(int port, int is_overcurrented)
{
	switch (port) {
	case USBC_PORT_C0:
		ioex_set_level(IOEX_USB_C0_FAULT_ODL, !is_overcurrented);
		break;

	case USBC_PORT_C1:
		ioex_set_level(IOEX_USB_C1_FAULT_ODL, !is_overcurrented);
		break;

	default:
		break;
	}
}

const struct tcpc_config_t tcpc_config[] = {
	[USBC_PORT_C0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		},
		.drv = &nct38xx_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
	[USBC_PORT_C1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC1,
			.addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		},
		.drv = &nct38xx_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == USBC_PORT_COUNT);
BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT == USBC_PORT_COUNT);

const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_TCPC0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},

	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9201_bc12_chips) == USBC_PORT_COUNT);

static void reset_nct38xx_port(int port)
{
	enum gpio_signal reset_gpio_l;

	if (port == USBC_PORT_C0)
		reset_gpio_l = GPIO_USB_C0_TCPC_RST_L;
	else if (port == USBC_PORT_C1)
		reset_gpio_l = GPIO_USB_C1_TCPC_RST_L;
	else
		/* Invalid port: do nothing */
		return;

	gpio_set_level(reset_gpio_l, 0);
	crec_msleep(NCT38XX_RESET_HOLD_DELAY_MS);
	gpio_set_level(reset_gpio_l, 1);
	nct38xx_reset_notify(port);
	if (NCT3807_RESET_POST_DELAY_MS != 0)
		crec_msleep(NCT3807_RESET_POST_DELAY_MS);
}

void board_reset_pd_mcu(void)
{
	/* Reset TCPC0 */
	reset_nct38xx_port(USBC_PORT_C0);

	/* Reset TCPC1 */
	reset_nct38xx_port(USBC_PORT_C1);
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	/*
	 * Check which port has the ALERT line set and ignore if that TCPC has
	 * its reset line active.
	 */
	if (!gpio_get_level(GPIO_USB_C0_TCPC_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C0_TCPC_RST_L) != 0)
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!gpio_get_level(GPIO_USB_C1_TCPC_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C1_TCPC_RST_L) != 0)
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

void tcpc_alert_event(enum gpio_signal signal)
{
	int port = -1;

	switch (signal) {
	case GPIO_USB_C0_TCPC_INT_ODL:
		port = 0;
		break;
	case GPIO_USB_C1_TCPC_INT_ODL:
		port = 1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

int board_pd_set_frs_enable(int port, int enable)
{
	int rv = EC_SUCCESS;

	/* Use the TCPC to enable fast switch when FRS included */
	if (port == USBC_PORT_C0) {
		rv = ioex_set_level(IOEX_USB_C0_TCPC_FASTSW_CTL_EN, !!enable);
	} else {
		rv = ioex_set_level(IOEX_USB_C1_TCPC_FASTSW_CTL_EN, !!enable);
	}

	return rv;
}

void bc12_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_BC12_INT_ODL:
		usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
		break;

	case GPIO_USB_C1_BC12_INT_ODL:
		usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
		break;

	default:
		break;
	}
}

/*****************************************************************************
 * IO expander
 */

struct ioexpander_config_t ioex_config[] = {
	[USBC_PORT_C0] = {
		.i2c_host_port = I2C_PORT_TCPC0,
		.i2c_addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
	},
	[USBC_PORT_C1] = {
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ioex_config) == USBC_PORT_COUNT);
BUILD_ASSERT(CONFIG_IO_EXPANDER_PORT_COUNT == USBC_PORT_COUNT);

/*****************************************************************************
 * Custom Zork USB-C1 Retimer/MUX driver
 */

/*
 * PS8802 set mux board tuning.
 * Adds in board specific gain and DP lane count configuration
 */
static int board_ps8802_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* Make sure the PS8802 is awake */
	rv = ps8802_i2c_wake(me);
	if (rv)
		return rv;

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		rv = ps8802_i2c_field_update16(me, PS8802_REG_PAGE2,
					       PS8802_REG2_USB_SSEQ_LEVEL,
					       PS8802_USBEQ_LEVEL_UP_MASK,
					       PS8802_USBEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		rv = ps8802_i2c_field_update8(me, PS8802_REG_PAGE2,
					      PS8802_REG2_DPEQ_LEVEL,
					      PS8802_DPEQ_LEVEL_UP_MASK,
					      PS8802_DPEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;
	}

	return rv;
}

/*
 * PS8818 set mux board tuning.
 * Adds in board specific gain and DP lane count configuration
 */
static int board_ps8818_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX1EQ_10G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX2EQ_10G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX1EQ_5G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX2EQ_5G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Set the RX input termination */
		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_RX_PHY,
					      PS8818_RX_INPUT_TERM_MASK,
					      ZORK_PS8818_RX_INPUT_TERM);
		if (rv)
			return rv;
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_DPEQ_LEVEL,
					      PS8818_DPEQ_LEVEL_UP_MASK,
					      PS8818_DPEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Enable IN_HPD on the DB */
		gpio_or_ioex_set_level(board_usbc1_retimer_inhpd, 1);
	} else {
		/* Disable IN_HPD on the DB */
		gpio_or_ioex_set_level(board_usbc1_retimer_inhpd, 0);
	}

	return rv;
}

struct usb_mux usbc1_ps8802 = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = PS8802_I2C_ADDR_FLAGS,
	.driver = &ps8802_usb_mux_driver,
	.board_set = &board_ps8802_mux_set,
};
const struct usb_mux usbc1_ps8818 = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = PS8818_I2C_ADDR0_FLAGS,
	.driver = &ps8818_usb_retimer_driver,
	.board_set = &board_ps8818_mux_set,
};
struct usb_mux usbc1_amd_fp5_usb_mux = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_USB_AP_MUX,
	.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
	.driver = &amd_fp5_usb_mux_driver,
};

/*
 * USB-C1 HPD may go through an IO expander, so we must use a custom HPD GPIO
 * control function with CONFIG_USB_PD_DP_HPD_GPIO_CUSTOM.
 *
 * TODO(b/165622386) revert to non-custom GPIO control when HPD is no longer on
 * the IO expander in any variants.
 */
void svdm_set_hpd_gpio(int port, int en)
{
	gpio_or_ioex_set_level(PORT_TO_HPD(port), en);
}

int svdm_get_hpd_gpio(int port)
{
	int out;

	if (gpio_or_ioex_get_level(PORT_TO_HPD(port), &out) != EC_SUCCESS) {
		ccprints("Failed to read current HPD for port C%d", port);
		return 0;
	}
	return out;
}
