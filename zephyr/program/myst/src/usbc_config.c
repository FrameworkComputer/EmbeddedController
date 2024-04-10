/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Myst family-specific USB-C configuration */

#include "battery_fuel_gauge.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/charger/isl9241.h"
#include "driver/ppc/ktu1125_public.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/tcpm/rt1718s.h"
#include "driver/usb_mux/amd_fp6.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "power.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc/usb_muxes.h"
#include "usbc_config.h"
#include "usbc_ppc.h"

#include <zephyr/drivers/gpio.h>

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#ifdef CONFIG_PLATFORM_EC_USB_PD_TCPM_RT1718S
#define GPIO_EN_USB_C1_SINK RT1718S_GPIO1
#define GPIO_EN_USB_C1_SOURCE RT1718S_GPIO2
#define GPIO_EN_USB_C1_FRS RT1718S_GPIO3
#endif

static uint32_t get_io_db_type_from_cached_cbi(void)
{
	uint32_t io_db_type;
	int ret = cros_cbi_get_fw_config(FW_IO_DB, &io_db_type);

	if (ret != 0) {
		io_db_type = FW_IO_DB_NONE;
	}

	return io_db_type;
}

static void usbc_interrupt_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_pd_soc));

	/* Reset TCPC if we only have a battery connected, or the SINK
	 * gpio to the PPC might be reset and cause brown-out.
	 */
	if (!system_jumped_late() && battery_is_present() == BP_YES) {
		/* TODO(crosbug.com/p/61098): How long do we need to wait? */
		board_reset_pd_mcu();
	}
}
DECLARE_HOOK(HOOK_INIT, usbc_interrupt_init, HOOK_PRIO_POST_I2C);

int ppc_get_alert_status(int port)
{
	if (port == USBC_PORT_C0)
		return gpio_pin_get_dt(
			GPIO_DT_FROM_NODELABEL(gpio_usb_c0_ppc_int_odl));
	else if (port == USBC_PORT_C1)
		return gpio_pin_get_dt(
			GPIO_DT_FROM_NODELABEL(gpio_usb_c1_ppc_int_odl));
	return 0;
}

int board_set_active_charge_port(int port)
{
	int is_valid_port = (port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;

	if (port == CHARGE_PORT_NONE) {
		CPRINTSUSB("Disabling all charger ports");
		/* Disable all ports. */
		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
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
	if (tcpm_get_src_ctrl(port)) {
		CPRINTSUSB("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTSUSB("New charge port: C%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
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

/* Round up 3250 max current to multiple of 128mA for ISL9241 AC prochot. */
static void charger_prochot_init_isl9241(void)
{
	isl9241_set_ac_prochot(CHARGER_SOLO, CONFIG_AC_PROCHOT_CURRENT_MA);
}
DECLARE_HOOK(HOOK_INIT, charger_prochot_init_isl9241, HOOK_PRIO_DEFAULT);

void board_reset_pd_mcu(void)
{
	uint32_t io_db_type = get_io_db_type_from_cached_cbi();
	/* reset C1 RT1718s */
	if (io_db_type == FW_IO_DB_SKU_A)
		rt1718s_sw_reset(USBC_PORT_C1);
}

__override int board_rt1718s_init(int port)
{
	static bool gpio_initialized;

	/* Reset TCPC sink/source control when it's a power-on reset or has a
	 * battery. Do not alter the carried GPIO status or this might stop PPC
	 * sinking and brown-out the system when battery disconnected.
	 */
	if (!system_jumped_late() && !gpio_initialized &&
	    (battery_is_present() == BP_YES ||
	     (system_get_reset_flags() & EC_RESET_FLAG_POWER_ON))) {
		/* set GPIO 1~3 as push pull, as output, output low. */
		rt1718s_gpio_set_flags(port, RT1718S_GPIO1, GPIO_OUT_LOW);
		rt1718s_gpio_set_flags(port, RT1718S_GPIO2, GPIO_OUT_LOW);
		rt1718s_gpio_set_flags(port, RT1718S_GPIO3, GPIO_OUT_LOW);
		gpio_initialized = true;
	}

	/* gpio1 low, gpio2 output high when receiving frs signal */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_GPIO1_VBUS_CTRL,
					  RT1718S_GPIO_VBUS_CTRL_FRS_RX_VBUS,
					  0));
	/* GPIO1 EN_SNK high when received TCPCI SNK enabled command */
	RETURN_ERROR(rt1718s_update_bits8(
		port, RT1718S_GPIO1_VBUS_CTRL,
		RT1718S_GPIO_VBUS_CTRL_ENA_SNK_VBUS_GPIO, 0xFF));
	/* GPIO2 EN_SRC high when received TCPCI SRC enabled command */
	RETURN_ERROR(rt1718s_update_bits8(
		port, RT1718S_GPIO2_VBUS_CTRL,
		RT1718S_GPIO_VBUS_CTRL_FRS_RX_VBUS |
			RT1718S_GPIO_VBUS_CTRL_ENA_SRC_VBUS_GPIO,
		0xFF));

	/* Trigger GPIO 1/2 change when FRS signal received */
	RETURN_ERROR(rt1718s_update_bits8(
		port, RT1718S_FRS_CTRL3,
		RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO2 |
			RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO1,
		RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO2 |
			RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO1));
	/* Set FRS signal detect time to 46.875us */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_FRS_CTRL1,
					  RT1718S_FRS_CTRL1_FRSWAPRX_MASK,
					  0xFF));

	/* Disable BC1.2 SRC mode */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_RT2_BC12_SRC_FUNC,
					  RT1718S_RT2_BC12_SRC_FUNC_BC12_SRC_EN,
					  0));
	/* Disable BC1.2 SNK mode */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_RT2_BC12_SNK_FUNC,
					  RT1718S_RT2_BC12_SNK_FUNC_BC12_SNK_EN,
					  0));

	return EC_SUCCESS;
}

__override int board_rt1718s_set_frs_enable(int port, int enable)
{
	if (port == USBC_PORT_C1)
		/*
		 * Use set_flags (implemented by a single i2c write) instead
		 * of set_level (= i2c_update) to save one read operation in
		 * FRS path.
		 */
		rt1718s_gpio_set_flags(port, GPIO_EN_USB_C1_FRS,
				       enable ? GPIO_OUT_HIGH : GPIO_OUT_LOW);
	else {
		CPRINTSUSB("Skip rt1718 FRS enable C%d", port);
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

__override int board_rt1718s_set_snk_enable(int port, int enable)
{
	rt1718s_gpio_set_level(port, GPIO_EN_USB_C1_SINK, enable);

	return EC_SUCCESS;
}
__override int board_rt1718s_set_src_enable(int port, int enable)
{
	rt1718s_gpio_set_level(port, GPIO_EN_USB_C1_SOURCE, enable);

	return EC_SUCCESS;
}

__override int board_get_vbus_voltage(int port)
{
	int voltage = 0;
	uint32_t io_db_type = get_io_db_type_from_cached_cbi();

	if (io_db_type == FW_IO_DB_SKU_A)
		rt1718s_get_adc(port, RT1718S_ADC_VBUS1, &voltage);
	else if (charger_get_vbus_voltage(port, &voltage))
		voltage = 0;

	return voltage;
}

#define SAFE_RESET_VBUS_DELAY_MS 900
#define SAFE_RESET_VBUS_MV 5000
void board_hibernate(void)
{
	int port;
	enum ec_error_list ret;

	/*
	 * If we are charging, then drop the Vbus level down to 5V to ensure
	 * that we don't get locked out of the 6.8V OVLO for our PPCs in
	 * dead-battery mode. This is needed when the TCPC/PPC rails go away.
	 * (b/79218851, b/143778351, b/147007265)
	 */
	port = charge_manager_get_active_charge_port();
	if (port != CHARGE_PORT_NONE) {
		pd_request_source_voltage(port, SAFE_RESET_VBUS_MV);

		/* Give PD task and PPC chip time to get to 5V */
		crec_msleep(SAFE_RESET_VBUS_DELAY_MS);
	}

	/* Try to put our battery fuel gauge into sleep mode */
	ret = battery_sleep_fuel_gauge();
	if ((ret != EC_SUCCESS) && (ret != EC_ERROR_UNIMPLEMENTED))
		cprints(CC_SYSTEM, "Failed to send battery sleep command");
}
