/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "driver/bc12/pi3usb9201_public.h"
#include "driver/ppc/syv682x_public.h"
#include "driver/retimer/bb_retimer_public.h"
#include "driver/retimer/ps8811.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/tcpm/tcpci.h"
#include "ec_commands.h"
#include "fw_config.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "ioexpander.h"
#include "system.h"
#include "task.h"
#include "task_id.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_config.h"
#include "usbc_ppc.h"

#include <stdbool.h>
#include <stdint.h>

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

/* USBC TCPC configuration */
const struct tcpc_config_t tcpc_config[] = {
	[USBC_PORT_C0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0_TCPC,
			.addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		},
		.drv = &nct38xx_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
	[USBC_PORT_C1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C1_TCPC,
			.addr_flags = NCT38XX_I2C_ADDR1_4_FLAGS,
		},
		.drv = &nct38xx_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == USBC_PORT_COUNT);
BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT == USBC_PORT_COUNT);

/* USBC PPC configuration */
struct ppc_config_t ppc_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0_PPC,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.frs_en = IOEX_USB_C0_FRS_EN,
		.drv = &syv682x_drv,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1_PPC,
		.i2c_addr_flags = SYV682X_ADDR2_FLAGS,
		.frs_en = IOEX_USB_C1_FRS_EN,
		.drv = &syv682x_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ppc_chips) == USBC_PORT_COUNT);

unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* USBC mux configuration - Alder Lake includes internal mux */
static const struct usb_mux_chain usbc0_tcss_usb_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C0,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
};
static const struct usb_mux_chain usbc1_tcss_usb_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C1,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
};

const struct usb_mux_chain usb_muxes[] = {
	[USBC_PORT_C0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C0,
			.driver = &bb_usb_retimer,
			.hpd_update = bb_retimer_hpd_update,
			.i2c_port = I2C_PORT_USB_C0_MUX,
			.i2c_addr_flags = USBC_PORT_C0_BB_RETIMER_I2C_ADDR,
		},
		.next = &usbc0_tcss_usb_mux,
	},
	[USBC_PORT_C1] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C1,
			.driver = &bb_usb_retimer,
			.hpd_update = bb_retimer_hpd_update,
			.i2c_port = I2C_PORT_USB_C1_MUX,
			.i2c_addr_flags = USBC_PORT_C1_BB_RETIMER_I2C_ADDR,
		},
		.next = &usbc1_tcss_usb_mux,
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

/* BC1.2 charger detect configuration */
const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_1_FLAGS,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9201_bc12_chips) == USBC_PORT_COUNT);

/*
 * USB C0 and C1 uses burnside bridge chips and have their reset
 * controlled by their respective TCPC chips acting as GPIO expanders.
 *
 * ioex_init() is normally called before we take the TCPCs out of
 * reset, so we need to start in disabled mode, then explicitly
 * call ioex_init().
 */

struct ioexpander_config_t ioex_config[] = {
	[IOEX_C0_NCT38XX] = {
		.i2c_host_port = I2C_PORT_USB_C0_TCPC,
		.i2c_addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
		.flags = IOEX_FLAGS_DEFAULT_INIT_DISABLED,
	},
	[IOEX_C1_NCT38XX] = {
		.i2c_host_port = I2C_PORT_USB_C1_TCPC,
		.i2c_addr_flags = NCT38XX_I2C_ADDR1_4_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
		.flags = IOEX_FLAGS_DEFAULT_INIT_DISABLED,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ioex_config) == CONFIG_IO_EXPANDER_PORT_COUNT);

__override int bb_retimer_power_enable(const struct usb_mux *me, bool enable)
{
	enum ioex_signal rst_signal;

	if (me->usb_port == USBC_PORT_C0) {
		rst_signal = IOEX_USB_C0_RT_RST_ODL;
	} else if (me->usb_port == USBC_PORT_C1) {
		rst_signal = IOEX_USB_C1_RT_RST_ODL;
	} else {
		return EC_ERROR_INVAL;
	}

	/*
	 * We do not have a load switch for the burnside bridge chips,
	 * so we only need to sequence reset.
	 */

	if (enable) {
		/*
		 * Tpw, minimum time from VCC to RESET_N de-assertion is 100us.
		 * For boards that don't provide a load switch control, the
		 * retimer_init() function ensures power is up before calling
		 * this function.
		 */
		ioex_set_level(rst_signal, 1);
		/*
		 * Allow 1ms time for the retimer to power up lc_domain
		 * which powers I2C controller within retimer
		 */
		crec_msleep(1);
	} else {
		ioex_set_level(rst_signal, 0);
		crec_msleep(1);
	}
	return EC_SUCCESS;
}

void board_reset_pd_mcu(void)
{
	gpio_set_level(GPIO_USB_C0_TCPC_RST_ODL, 0);
	gpio_set_level(GPIO_USB_C1_TCPC_RST_ODL, 0);

	/*
	 * delay for power-on to reset-off and min. assertion time
	 */
	crec_msleep(NCT38XX_RESET_HOLD_DELAY_MS);

	gpio_set_level(GPIO_USB_C0_TCPC_RST_ODL, 1);
	gpio_set_level(GPIO_USB_C1_TCPC_RST_ODL, 1);

	nct38xx_reset_notify(USBC_PORT_C0);
	nct38xx_reset_notify(USBC_PORT_C1);

	/* wait for chips to come up */
	if (NCT3807_RESET_POST_DELAY_MS != 0)
		crec_msleep(NCT3807_RESET_POST_DELAY_MS);
}

static void board_tcpc_init(void)
{
	int i;

	/* Don't reset TCPCs after initial reset */
	if (!system_jumped_late())
		board_reset_pd_mcu();

	/*
	 * These IO expander pins are implemented using the
	 * C0/C1 TCPCs, so they must be set up after the TCPCs has
	 * been taken out of reset.
	 */
	for (i = 0; i < CONFIG_IO_EXPANDER_PORT_COUNT; ++i)
		ioex_init(i);

	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PPC_INT_ODL);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_TCPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_TCPC_INT_ODL);

	/* Enable BC1.2 interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_CHIPSET);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (gpio_get_level(GPIO_USB_C0_TCPC_INT_ODL) == 0)
		status |= PD_STATUS_TCPC_ALERT_0;

	if (gpio_get_level(GPIO_USB_C1_TCPC_INT_ODL) == 0)
		status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

void tcpc_alert_event(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_TCPC_INT_ODL:
		schedule_deferred_pd_interrupt(USBC_PORT_C0);
		break;
	case GPIO_USB_C1_TCPC_INT_ODL:
		schedule_deferred_pd_interrupt(USBC_PORT_C1);
		break;
	default:
		break;
	}
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

void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		syv682x_interrupt(USBC_PORT_C0);
		break;
	case GPIO_USB_C1_PPC_INT_ODL:
		syv682x_interrupt(USBC_PORT_C1);
		break;
	default:
		break;
	}
}

void retimer_interrupt(enum gpio_signal signal)
{
	/*
	 * TODO(b/179513527): add USB-C support
	 */
}

__override bool board_is_dts_port(int port)
{
	return port == USBC_PORT_C0;
}

const struct usb_mux usba_ps8811[] = {
	[USBA_PORT_A0] = {
		.usb_port = USBA_PORT_A0,
		.i2c_port = I2C_PORT_USB_A0_RETIMER,
		.i2c_addr_flags = PS8811_I2C_ADDR_FLAGS0,
	},
	[USBA_PORT_A1] = {
		.usb_port = USBA_PORT_A1,
		.i2c_port = I2C_PORT_USB_A1_RETIMER,
		.i2c_addr_flags = PS8811_I2C_ADDR_FLAGS0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(usba_ps8811) == USBA_PORT_COUNT);

const static struct ps8811_reg_val equalizer_wwan_table[] = {
	{
		/* Set channel A EQ setting */
		.reg = PS8811_REG1_USB_AEQ_LEVEL,
		.val = (PS8811_AEQ_I2C_LEVEL_UP_13DB
			<< PS8811_AEQ_I2C_LEVEL_UP_SHIFT) |
		       (PS8811_AEQ_PIN_LEVEL_UP_18DB
			<< PS8811_AEQ_PIN_LEVEL_UP_SHIFT),
	},
	{
		/* Set ADE pin setting */
		.reg = PS8811_REG1_USB_ADE_CONFIG,
		.val = (PS8811_ADE_PIN_MID_LEVEL_3DB
			<< PS8811_ADE_PIN_MID_LEVEL_SHIFT) |
		       PS8811_AEQ_CONFIG_REG_ENABLE |
		       PS8811_AEQ_ADAPTIVE_REG_ENABLE,
	},
	{
		/* Set channel B EQ setting */
		.reg = PS8811_REG1_USB_BEQ_LEVEL,
		.val = (PS8811_BEQ_I2C_LEVEL_UP_10P5DB
			<< PS8811_BEQ_I2C_LEVEL_UP_SHIFT) |
		       (PS8811_BEQ_PIN_LEVEL_UP_18DB
			<< PS8811_BEQ_PIN_LEVEL_UP_SHIFT),
	},
	{
		/* Set BDE pin setting */
		.reg = PS8811_REG1_USB_BDE_CONFIG,
		.val = (PS8811_BDE_PIN_MID_LEVEL_3DB
			<< PS8811_BDE_PIN_MID_LEVEL_SHIFT) |
		       PS8811_BEQ_CONFIG_REG_ENABLE,
	},
};

#define NUM_EQ_WWAN_ARRAY ARRAY_SIZE(equalizer_wwan_table)

const static struct ps8811_reg_val equalizer_wlan_table[] = {
	{
		/* Set 50ohm adjust for B channel */
		.reg = PS8811_REG1_50OHM_ADJUST_CHAN_B,
		.val = (PS8811_50OHM_ADJUST_CHAN_B_MINUS_9PCT
			<< PS8811_50OHM_ADJUST_CHAN_B_SHIFT),
	},
};

#define NUM_EQ_WLAN_ARRAY ARRAY_SIZE(equalizer_wlan_table)

static int usba_retimer_init(int port)
{
	int rv;
	int val;
	int i;
	const struct usb_mux *me = &usba_ps8811[port];

	rv = ps8811_i2c_read(me, PS8811_REG_PAGE1, PS8811_REG1_USB_BEQ_LEVEL,
			     &val);

	switch (port) {
	case USBA_PORT_A0:
		/* Set channel A output swing */
		rv = ps8811_i2c_field_update(me, PS8811_REG_PAGE1,
					     PS8811_REG1_USB_CHAN_A_SWING,
					     PS8811_CHAN_A_SWING_MASK,
					     0x2 << PS8811_CHAN_A_SWING_SHIFT);
		break;
	case USBA_PORT_A1:
		if (ec_cfg_has_lte()) {
			/* Set channel A output swing */
			rv = ps8811_i2c_field_update(
				me, PS8811_REG_PAGE1,
				PS8811_REG1_USB_CHAN_A_SWING,
				PS8811_CHAN_A_SWING_MASK,
				0x2 << PS8811_CHAN_A_SWING_SHIFT);

			/* Set channel B output PS level */
			rv |= ps8811_i2c_field_update(
				me, PS8811_REG_PAGE1,
				PS8811_REG1_USB_CHAN_B_DE_PS_LSB,
				PS8811_CHAN_B_DE_PS_LSB_MASK, 0x06);

			/* Set channel B output DE level */
			rv |= ps8811_i2c_field_update(
				me, PS8811_REG_PAGE1,
				PS8811_REG1_USB_CHAN_B_DE_PS_MSB,
				PS8811_CHAN_B_DE_PS_MSB_MASK, 0x16);

			for (i = 0; i < NUM_EQ_WWAN_ARRAY; i++)
				rv |= ps8811_i2c_write(
					me, PS8811_REG_PAGE1,
					equalizer_wwan_table[i].reg,
					equalizer_wwan_table[i].val);
		} else {
			/* Set channel A output swing */
			rv = ps8811_i2c_field_update(
				me, PS8811_REG_PAGE1,
				PS8811_REG1_USB_CHAN_A_SWING,
				PS8811_CHAN_A_SWING_MASK,
				0x2 << PS8811_CHAN_A_SWING_SHIFT);

			for (i = 0; i < NUM_EQ_WLAN_ARRAY; i++)
				rv |= ps8811_i2c_write(
					me, PS8811_REG_PAGE1,
					equalizer_wlan_table[i].reg,
					equalizer_wlan_table[i].val);
		}
		break;
	}

	return rv;
}

void board_chipset_startup(void)
{
	int i;

	for (i = 0; i < USBA_PORT_COUNT; ++i)
		usba_retimer_init(i);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);
