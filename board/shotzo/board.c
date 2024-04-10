/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Shotzo board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "cbi_fw_config.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "cros_board_info.h"
#include "driver/charger/sm5803.h"
#include "driver/led/oz554.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/temp_sensor/thermistor.h"
#include "driver/usb_mux/it5205.h"
#include "gpio.h"
#include "hooks.h"
#include "intc.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "temp_sensor.h"
#include "uart.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)

#define INT_RECHECK_US 5000

uint32_t board_version;

/* GPIO to enable/disable the USB Type-A port. */
const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_USB_A_5V,
};

/* C0 interrupt line triggered by charger */
static void check_c0_line(void);
DECLARE_DEFERRED(check_c0_line);

static void notify_c0_chips(void)
{
	sm5803_interrupt(0);
}

static void check_c0_line(void)
{
	/*
	 * If line is still being held low, see if there's more to process from
	 * one of the chips
	 */
	if (!gpio_get_level(GPIO_USB_C0_INT_ODL)) {
		notify_c0_chips();
		hook_call_deferred(&check_c0_line_data, INT_RECHECK_US);
	}
}

static void usb_c0_interrupt(enum gpio_signal s)
{
	/* Cancel any previous calls to check the interrupt line */
	hook_call_deferred(&check_c0_line_data, -1);

	/* Notify all chips using this line that an interrupt came in */
	notify_c0_chips();

	/* Check the line again in 5ms */
	hook_call_deferred(&check_c0_line_data, INT_RECHECK_US);
}

static void c0_ccsbu_ovp_interrupt(enum gpio_signal s)
{
	cprints(CC_USBPD, "C0: CC OVP, SBU OVP, or thermal event");
	pd_handle_cc_overvoltage(0);
}

/******************************************************************************/
/*
 * Barrel jack power supply handling
 *
 * EN_PPVAR_BJ_ADP_L must default active to ensure we can power on when the
 * barrel jack is connected, and the USB-C port can bring the EC up fine in
 * dead-battery mode. Both the USB-C and barrel jack switches do reverse
 * protection, so we're safe to turn one on then the other off- but we should
 * only do that if the system is off since it might still brown out.
 */

static int barrel_jack_adapter_is_present(void)
{
	/* Shotzo barrel jack adapter present pin is active low. */
	return !gpio_get_level(GPIO_BJ_ADP_PRESENT_L);
}

/*
 * Barrel-jack power adapter ratings.
 */

#define BJ_ADP_RATING_DEFAULT 0 /* BJ power ratings default */
static const struct {
	int voltage;
	int current;
} bj_power[] = {
	{ /* 0 - 90W (also default) */
	  .voltage = 19500,
	  .current = 4500 },
};

/* Debounced connection state of the barrel jack */
static int8_t adp_connected = -1;
static void adp_connect_deferred(void)
{
	struct charge_port_info pi = { 0 };
	int connected = barrel_jack_adapter_is_present();

	/* Debounce */
	if (connected == adp_connected)
		return;
	if (connected) {
		unsigned int bj = BJ_ADP_RATING_DEFAULT;

		pi.voltage = bj_power[bj].voltage;
		pi.current = bj_power[bj].current;
	}
	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
				     DEDICATED_CHARGE_PORT, &pi);
	adp_connected = connected;
}
DECLARE_DEFERRED(adp_connect_deferred);

#define ADP_DEBOUNCE_MS 1000 /* Debounce time for BJ plug/unplug */
/* IRQ for BJ plug/unplug. It shouldn't be called if BJ is the power source. */
static void adp_connect_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&adp_connect_deferred_data, ADP_DEBOUNCE_MS * MSEC);
}
static void adp_state_init(void)
{
	/*
	 * Initialize all charge suppliers to 0. The charge manager waits until
	 * all ports have reported in before doing anything.
	 */
	for (int i = 0; i < CHARGE_PORT_COUNT; i++) {
		for (int j = 0; j < CHARGE_SUPPLIER_COUNT; j++)
			charge_manager_update_charge(j, i, NULL);
	}

	/* Report charge state from the barrel jack. */
	adp_connect_deferred();
}
DECLARE_HOOK(HOOK_INIT, adp_state_init, HOOK_PRIO_INIT_CHARGE_MANAGER + 1);

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_VSNS_PP3300_A] = { .name = "PP3300_A_PGOOD",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH0 },
	[ADC_TEMP_SENSOR_1] = { .name = "TEMP_SENSOR1",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH2 },
	[ADC_TEMP_SENSOR_2] = { .name = "TEMP_SENSOR2",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH3 },
	[ADC_SUB_ANALOG] = { .name = "SUB_ANALOG",
			     .factor_mul = ADC_MAX_MVOLT,
			     .factor_div = ADC_READ_MAX + 1,
			     .shift = 0,
			     .channel = CHIP_ADC_CH13 },
	[ADC_TEMP_SENSOR_3] = { .name = "TEMP_SENSOR3",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH15 },
	[ADC_TEMP_SENSOR_4] = { .name = "TEMP_SENSOR4",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH16 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Charger chips */
const struct charger_config_t
	chg_chips[] = { [CHARGER_SOLO] = {
				.i2c_port = I2C_PORT_USB_C0,
				.i2c_addr_flags = SM5803_ADDR_CHARGER_FLAGS,
				.drv = &sm5803_drv,
			} };

/* TCPCs */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = { {
	.bus_type = EC_BUS_TYPE_EMBEDDED,
	.drv = &it83xx_tcpm_drv,
} };

/* USB Muxes */
const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = { {
	.mux =
		&(const struct usb_mux){
			.usb_port = 0,
			.i2c_port = I2C_PORT_USB_C0,
			.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
			.driver = &it5205_usb_mux_driver,
		},
} };

void oz554_board_init(void)
{
	int panel_id = 0;
	int oz554_id;

	oz554_id = gpio_get_level(GPIO_BL_OZ554_ID);
	panel_id |= gpio_get_level(GPIO_PANEL_ID0) << 0;
	panel_id |= gpio_get_level(GPIO_PANEL_ID1) << 1;
	panel_id |= gpio_get_level(GPIO_PANEL_ID2) << 2;
	panel_id |= gpio_get_level(GPIO_PANEL_ID3) << 3;

	if (oz554_id == 0)
		CPRINTUSB("OZ554ELN");
	else if (oz554_id == 1)
		CPRINTUSB("OZ554ALN");
	else
		CPRINTUSB("OZ554A UNKNOWN");

	switch (panel_id) {
	case 0x00:
		CPRINTUSB("PANEL M238HAN");
		oz554_set_config(0, 0xF1);
		oz554_set_config(1, 0x43);
		oz554_set_config(2, 0x44);
		oz554_set_config(5, 0xBF);
		break;
	case 0x08:
		CPRINTUSB("PANEL MV238FHM");
		oz554_set_config(0, 0xF1);
		oz554_set_config(1, 0x43);
		oz554_set_config(2, 0x3C);
		oz554_set_config(5, 0xD7);
		break;
	default:
		CPRINTUSB("PANEL UNKNOWN");
		break;
	}
}

void board_init(void)
{
	int on;

	gpio_enable_interrupt(GPIO_BJ_ADP_PRESENT_L);

	gpio_enable_interrupt(GPIO_USB_C0_INT_ODL);

	/* Store board version for use in determining charge limits */
	cbi_get_board_version(&board_version);

	/*
	 * If interrupt lines are already low, schedule them to be processed
	 * after inits are completed.
	 */
	if (!gpio_get_level(GPIO_USB_C0_INT_ODL))
		hook_call_deferred(&check_c0_line_data, 0);

	gpio_enable_interrupt(GPIO_USB_C0_CCSBU_OVP_ODL);

	oz554_board_init();
	gpio_enable_interrupt(GPIO_PANEL_BACKLIGHT_EN);

	/* Charger on the MB will be outputting PROCHOT_ODL and OD CHG_DET */
	sm5803_configure_gpio0(CHARGER_SOLO, GPIO0_MODE_PROCHOT, 1);
	sm5803_configure_chg_det_od(CHARGER_SOLO, 1);

	/* Turn on 5V if the system is on, otherwise turn it off */
	on = chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND |
			      CHIPSET_STATE_SOFT_OFF);
	board_power_5v_enable(on);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_resume(void)
{
	sm5803_disable_low_power_mode(CHARGER_SOLO);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_resume, HOOK_PRIO_DEFAULT);

static void board_suspend(void)
{
	sm5803_enable_low_power_mode(CHARGER_SOLO);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_suspend, HOOK_PRIO_DEFAULT);

static void board_shutdown(void)
{
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_shutdown, HOOK_PRIO_DEFAULT);

__override void board_pulse_entering_rw(void)
{
	/*
	 * On the ITE variants, the EC_ENTERING_RW signal was connected to a pin
	 * which is active high by default.  This causes Cr50 to think that the
	 * EC has jumped to its RW image even though this may not be the case.
	 * The pin is changed to GPIO_EC_ENTERING_RW2.
	 */
	gpio_set_level(GPIO_EC_ENTERING_RW, 1);
	gpio_set_level(GPIO_EC_ENTERING_RW2, 1);
	crec_usleep(MSEC);
	gpio_set_level(GPIO_EC_ENTERING_RW, 0);
	gpio_set_level(GPIO_EC_ENTERING_RW2, 0);
}

void board_reset_pd_mcu(void)
{
	/*
	 * Nothing to do.  TCPC C0 is internal.
	 */
}

__override void board_power_5v_enable(int enable)
{
	/*
	 * Motherboard has a GPIO to turn on the 5V regulator, but the sub-board
	 * sets it through the charger GPIO.
	 */
	gpio_set_level(GPIO_EN_PP5000, !!enable);
}

uint16_t tcpc_get_alert_status(void)
{
	/*
	 * TCPC 0 is embedded in the EC and processes interrupts in the chip
	 * code (it83xx/intc.c)
	 */
	return 0;
}

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	if (port == CHARGER_SOLO) {
		charger_set_input_current_limit(CHARGER_SOLO, max_ma);
	}
}

__override int extpower_is_present(void)
{
	int port;
	int rv;
	bool acok;

	for (port = 0; port < board_get_usb_pd_port_count(); port++) {
		rv = sm5803_is_acok(port, &acok);
		if ((rv == EC_SUCCESS) && acok)
			return 1;
	}

	if (!gpio_get_level(GPIO_EN_PPVAR_BJ_ADP_L))
		return 1;

	CPRINTUSB("No external power present.");

	return 0;
}

int board_set_active_charge_port(int port)
{
	CPRINTUSB("Requested charge port change to %d", port);

	/*
	 * The charge manager may ask us to switch to no charger if we're
	 * running off USB-C only but upstream doesn't support PD. It requires
	 * that we accept this switch otherwise it triggers an assert and EC
	 * reset; it's not possible to boot the AP anyway, but we want to avoid
	 * resetting the EC so we can continue to do the "low power" LED blink.
	 */
	if (port == CHARGE_PORT_NONE)
		return EC_SUCCESS;

	if (port < 0 || CHARGE_PORT_COUNT <= port)
		return EC_ERROR_INVAL;

	if (port == charge_manager_get_active_charge_port())
		return EC_SUCCESS;

	/* Don't charge from a source port */
	if (board_vbus_source_enabled(port))
		return EC_ERROR_INVAL;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		int bj_active, bj_requested;

		if (charge_manager_get_active_charge_port() != CHARGE_PORT_NONE)
			/* Change is only permitted while the system is off */
			return EC_ERROR_INVAL;

		/*
		 * Current setting is no charge port but the AP is on, so the
		 * charge manager is out of sync (probably because we're
		 * reinitializing after sysjump). Reject requests that aren't
		 * in sync with our outputs.
		 */
		bj_active = !gpio_get_level(GPIO_EN_PPVAR_BJ_ADP_L);
		bj_requested = port == CHARGE_PORT_BARRELJACK;
		if (bj_active != bj_requested)
			return EC_ERROR_INVAL;
	}

	CPRINTUSB("New charger p%d", port);

	switch (port) {
	case CHARGE_PORT_TYPEC0:
		sm5803_vbus_sink_enable(CHARGER_SOLO, 1);
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 1);
		break;
	case CHARGE_PORT_BARRELJACK:
		/* Make sure BJ adapter is sourcing power */
		if (!barrel_jack_adapter_is_present())
			return EC_ERROR_INVAL;
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 0);
		sm5803_vbus_sink_enable(CHARGER_SOLO, 0);
		break;
	default:
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

/* Vconn control for integrated ITE TCPC */
void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	/* Vconn control is only for port 0 */
	if (port)
		return;

	if (cc_pin == USBPD_CC_PIN_1)
		gpio_set_level(GPIO_EN_USB_C0_CC1_VCONN, !!enabled);
	else
		gpio_set_level(GPIO_EN_USB_C0_CC2_VCONN, !!enabled);
}

__override void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	int current;

	if (port < 0 || port > board_get_usb_pd_port_count())
		return;

	current = (rp == TYPEC_RP_3A0) ? 3000 : 1500;

	charger_set_otg_current_voltage(port, current, 5000);
}

/* Thermistors */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = { .name = "Memory",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "Ambient",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
	[TEMP_SENSOR_3] = { .name = "Charger",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_3 },
	[TEMP_SENSOR_4] = { .name = "5V regular",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_4 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);
