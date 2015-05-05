/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* ryu board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "battery.h"
#include "case_closed_debug.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "inductive_charging.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "usb.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "usb_spi.h"
#include "usb-stm32f3.h"
#include "usb-stream.h"
#include "usart-stm32f3.h"
#include "util.h"
#include "pi3usb9281.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

/* Default input current limit when VBUS is present */
#define DEFAULT_CURR_LIMIT            500  /* mA */

/* VBUS too low threshold */
#define VBUS_LOW_THRESHOLD_MV 4600

/* Input current error margin */
#define IADP_ERROR_MARGIN_MA 100

static int charge_current_limit;

/*
 * Store the state of our USB data switches so that they can be restored
 * after pericom reset.
 */
static int usb_switch_state;

static void vbus_log(void)
{
	CPRINTS("VBUS %d", gpio_get_level(GPIO_CHGR_ACOK));
}
DECLARE_DEFERRED(vbus_log);

void vbus_evt(enum gpio_signal signal)
{
	struct charge_port_info charge;
	int vbus_level = gpio_get_level(signal);

	/*
	 * If VBUS is low, or VBUS is high and we are not outputting VBUS
	 * ourselves, then update the VBUS supplier.
	 */
	if (!vbus_level || !gpio_get_level(GPIO_USBC_5V_EN)) {
		charge.voltage = USB_BC12_CHARGE_VOLTAGE;
		charge.current = vbus_level ? DEFAULT_CURR_LIMIT : 0;
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, 0, &charge);
	}

	hook_call_deferred(vbus_log, 0);
	if (task_start_called())
		task_wake(TASK_ID_PD);
}

/* Wait 200ms after a charger is detected to debounce pin contact order */
#define USB_CHG_DEBOUNCE_DELAY_MS 200
/*
 * Wait 100ms after reset, before re-enabling attach interrupt, so that the
 * spurious attach interrupt from certain ports is ignored.
 */
#define USB_CHG_RESET_DELAY_MS 100

void usb_charger_task(void)
{
	int device_type, charger_status;
	struct charge_port_info charge;
	int type;
	charge.voltage = USB_BC12_CHARGE_VOLTAGE;

	while (1) {
		/* Read interrupt register to clear */
		pi3usb9281_get_interrupts(0);

		/* Set device type */
		device_type = pi3usb9281_get_device_type(0);
		charger_status = pi3usb9281_get_charger_status(0);

		/* Debounce pin plug order if we detect a charger */
		if (device_type || PI3USB9281_CHG_STATUS_ANY(charger_status)) {
			msleep(USB_CHG_DEBOUNCE_DELAY_MS);

			/* Trigger chip reset to refresh detection registers */
			pi3usb9281_reset(0);
			/*
			 * Restore data switch settings - switches return to
			 * closed on reset until restored.
			 */
			if (usb_switch_state)
				pi3usb9281_set_switches(0, 1);

			/* Clear possible disconnect interrupt */
			pi3usb9281_get_interrupts(0);
			/* Mask attach interrupt */
			pi3usb9281_set_interrupt_mask(0,
						      0xff &
						      ~PI3USB9281_INT_ATTACH);
			/* Re-enable interrupts */
			pi3usb9281_enable_interrupts(0);
			msleep(USB_CHG_RESET_DELAY_MS);

			/* Clear possible attach interrupt */
			pi3usb9281_get_interrupts(0);
			/* Re-enable attach interrupt */
			pi3usb9281_set_interrupt_mask(0, 0xff);

			/* Re-read ID registers */
			device_type = pi3usb9281_get_device_type(0);
			charger_status = pi3usb9281_get_charger_status(0);
		}

		if (PI3USB9281_CHG_STATUS_ANY(charger_status))
			type = CHARGE_SUPPLIER_PROPRIETARY;
		else if (device_type & PI3USB9281_TYPE_CDP)
			type = CHARGE_SUPPLIER_BC12_CDP;
		else if (device_type & PI3USB9281_TYPE_DCP)
			type = CHARGE_SUPPLIER_BC12_DCP;
		else if (device_type & PI3USB9281_TYPE_SDP)
			type = CHARGE_SUPPLIER_BC12_SDP;
		else
			type = CHARGE_SUPPLIER_OTHER;

		/* Attachment: decode + update available charge */
		if (device_type || PI3USB9281_CHG_STATUS_ANY(charger_status)) {
			charge.current = pi3usb9281_get_ilim(device_type,
							     charger_status);
			charge_manager_update_charge(type, 0, &charge);
		} else { /* Detachment: update available charge to 0 */
			charge.current = 0;
			charge_manager_update_charge(
						CHARGE_SUPPLIER_PROPRIETARY,
						0,
						&charge);
			charge_manager_update_charge(
						CHARGE_SUPPLIER_BC12_CDP,
						0,
						&charge);
			charge_manager_update_charge(
						CHARGE_SUPPLIER_BC12_DCP,
						0,
						&charge);
			charge_manager_update_charge(
						CHARGE_SUPPLIER_BC12_SDP,
						0,
						&charge);
			charge_manager_update_charge(
						CHARGE_SUPPLIER_OTHER,
						0,
						&charge);
		}

		/* notify host of power info change */
		/* pd_send_host_event(PD_EVENT_POWER_CHANGE); */

		/* Wait for interrupt */
		task_wait_event(-1);
	}
}

void usb_evt(enum gpio_signal signal)
{
	task_wake(TASK_ID_USB_CHG);
}

#include "gpio_list.h"

const void *const usb_strings[] = {
	[USB_STR_DESC]           = usb_string_desc,
	[USB_STR_VENDOR]         = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT]        = USB_STRING_DESC("Ryu debug"),
	[USB_STR_VERSION]        = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_CONSOLE_NAME]   = USB_STRING_DESC("EC_PD"),
	[USB_STR_AP_STREAM_NAME] = USB_STRING_DESC("AP"),
	[USB_STR_SH_STREAM_NAME] = USB_STRING_DESC("SH"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/*
 * Define AP and SH console forwarding queues and associated USART and USB
 * stream endpoints.
 */

QUEUE_CONFIG(ap_usart_to_usb, 64, uint8_t);
QUEUE_CONFIG(usb_to_ap_usart, 64, uint8_t);
QUEUE_CONFIG(sh_usart_to_usb, 64, uint8_t);
QUEUE_CONFIG(usb_to_sh_usart, 64, uint8_t);

struct usb_stream_config const usb_ap_stream;
struct usb_stream_config const usb_sh_stream;

USART_CONFIG(usart1,
	     usart1_hw,
	     115200,
	     ap_usart_to_usb,
	     usb_to_ap_usart,
	     usb_ap_stream.consumer,
	     usb_ap_stream.producer)

USART_CONFIG(usart3,
	     usart3_hw,
	     115200,
	     sh_usart_to_usb,
	     usb_to_sh_usart,
	     usb_sh_stream.consumer,
	     usb_sh_stream.producer)

#define AP_USB_STREAM_RX_SIZE	16
#define AP_USB_STREAM_TX_SIZE	16

USB_STREAM_CONFIG(usb_ap_stream,
		  USB_IFACE_AP_STREAM,
		  USB_STR_AP_STREAM_NAME,
		  USB_EP_AP_STREAM,
		  AP_USB_STREAM_RX_SIZE,
		  AP_USB_STREAM_TX_SIZE,
		  usb_to_ap_usart,
		  ap_usart_to_usb,
		  usart1.consumer,
		  usart1.producer)

#define SH_USB_STREAM_RX_SIZE	16
#define SH_USB_STREAM_TX_SIZE	16

USB_STREAM_CONFIG(usb_sh_stream,
		  USB_IFACE_SH_STREAM,
		  USB_STR_SH_STREAM_NAME,
		  USB_EP_SH_STREAM,
		  SH_USB_STREAM_RX_SIZE,
		  SH_USB_STREAM_TX_SIZE,
		  usb_to_sh_usart,
		  sh_usart_to_usb,
		  usart3.consumer,
		  usart3.producer)

/* Initialize board. */
static void board_init(void)
{
	struct charge_port_info charge_none, charge_vbus;

	/* Initialize all pericom charge suppliers to 0 */
	charge_none.voltage = USB_BC12_CHARGE_VOLTAGE;
	charge_none.current = 0;
	charge_manager_update_charge(CHARGE_SUPPLIER_PROPRIETARY,
				     0,
				     &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_CDP, 0, &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_DCP, 0, &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_SDP, 0, &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_OTHER, 0, &charge_none);

	/* Initialize VBUS supplier based on whether or not VBUS is present */
	charge_vbus.voltage = USB_BC12_CHARGE_VOLTAGE;
	charge_vbus.current = DEFAULT_CURR_LIMIT;
	if (gpio_get_level(GPIO_CHGR_ACOK))
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, 0,
					     &charge_vbus);
	else
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, 0,
					     &charge_none);

	/* Enable pericom BC1.2 interrupts. */
	gpio_enable_interrupt(GPIO_USBC_BC12_INT_L);
	pi3usb9281_set_interrupt_mask(0, 0xff);
	pi3usb9281_enable_interrupts(0);

	/*
	 * Determine recovery mode is requested by the power, volup, and
	 * voldown buttons being pressed.
	 */
	if (power_button_signal_asserted() &&
	    !gpio_get_level(GPIO_BTN_VOLD_L) &&
	    !gpio_get_level(GPIO_BTN_VOLU_L))
		host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY);

	/*
	 * Initialize AP and SH console forwarding USARTs and queues.
	 */
	queue_init(&ap_usart_to_usb);
	queue_init(&usb_to_ap_usart);
	queue_init(&sh_usart_to_usb);
	queue_init(&usb_to_sh_usart);
	usart_init(&usart1);
	usart_init(&usart3);

	/*
	 * Enable CC lines after all GPIO have been initialized. Note, it is
	 * important that this is enabled after the CC_DEVICE_ODL lines are
	 * set low to specify device mode.
	 */
	gpio_set_level(GPIO_USBC_CC_EN, 1);

	/* Enable interrupts on VBUS transitions. */
	gpio_enable_interrupt(GPIO_CHGR_ACOK);

	/*
	 * TODO(crosbug.com/p/38689) Workaround for PMIC issue on P5.
	 * remove when P5 are de-commissioned.
	 * We are re-using EXTINT1 for the new power sequencing workaround
	 * this is killing the base closing detection on P5
	 * we won't charge it.
	 */
	if (board_get_version() == 5)
		gpio_enable_interrupt(GPIO_HPD_IN);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_AP_HOLD, 1, "AP_HOLD"},
	{GPIO_AP_IN_SUSPEND,  1, "SUSPEND_ASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/*
 * TODO(crosbug.com/p/38689) Workaround for MAX77620 PMIC EN_PP3300 issue.
 * remove when P5 are de-commissioned.
 */
void pp1800_on_off_evt(enum gpio_signal signal)
{
	int level = gpio_get_level(signal);
	gpio_set_level(GPIO_EN_PP3300_RSVD, level);
}

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus sensing. Converted to mV, /10 voltage divider. */
	[ADC_VBUS] = {"VBUS",  30000, 4096, 0, STM32_AIN(0)},
	/* USB PD CC lines sensing. Converted to mV (3000mV/4096). */
	[ADC_CC1_PD] = {"CC1_PD", 3000, 4096, 0, STM32_AIN(1)},
	[ADC_CC2_PD] = {"CC2_PD", 3000, 4096, 0, STM32_AIN(3)},
	/* Charger current sensing. Converted to mA. */
	[ADC_IADP] = {"IADP",  7500, 4096, 0, STM32_AIN(8)},
	[ADC_IBAT] = {"IBAT", 37500, 4096, 0, STM32_AIN(13)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Charge supplier priority: lower number indicates higher priority. */
const int supplier_priority[] = {
	[CHARGE_SUPPLIER_PD] = 0,
	[CHARGE_SUPPLIER_TYPEC] = 1,
	[CHARGE_SUPPLIER_PROPRIETARY] = 1,
	[CHARGE_SUPPLIER_BC12_DCP] = 1,
	[CHARGE_SUPPLIER_BC12_CDP] = 2,
	[CHARGE_SUPPLIER_BC12_SDP] = 3,
	[CHARGE_SUPPLIER_OTHER] = 3,
	[CHARGE_SUPPLIER_VBUS] = 4
};
BUILD_ASSERT(ARRAY_SIZE(supplier_priority) == CHARGE_SUPPLIER_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
	{"slave",  I2C_PORT_SLAVE, 100,
		GPIO_SLAVE_I2C_SCL, GPIO_SLAVE_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

static void board_set_usb_switches(int port, int open)
{
	/* If switch is not changing, then return */
	if (open == usb_switch_state)
		return;

	usb_switch_state = open;
	pi3usb9281_set_switches(port, open);
}

/* TODO(crosbug.com/p/38333) remove me */
#define GPIO_USBC_SS1_USB_MODE_L GPIO_USBC_MUX_CONF0
#define GPIO_USBC_SS2_USB_MODE_L GPIO_USBC_MUX_CONF1
#define GPIO_USBC_SS_EN_L GPIO_USBC_MUX_CONF2

void p4_board_set_usb_mux(int port, enum typec_mux mux,
			  enum usb_switch usb, int polarity)
{
	/* reset everything */
	gpio_set_level(GPIO_USBC_SS_EN_L, 1);
	gpio_set_level(GPIO_USBC_DP_MODE_L, 1);
	gpio_set_level(GPIO_USBC_DP_POLARITY, 1);
	gpio_set_level(GPIO_USBC_SS1_USB_MODE_L, 1);
	gpio_set_level(GPIO_USBC_SS2_USB_MODE_L, 1);

	/* Set D+/D- switch to appropriate level */
	board_set_usb_switches(port, usb);

	if (mux == TYPEC_MUX_NONE)
		/* everything is already disabled, we can return */
		return;

	if (mux == TYPEC_MUX_USB || mux == TYPEC_MUX_DOCK) {
		/* USB 3.0 uses 2 superspeed lanes */
		gpio_set_level(polarity ? GPIO_USBC_SS2_USB_MODE_L :
					  GPIO_USBC_SS1_USB_MODE_L, 0);
	}

	if (mux == TYPEC_MUX_DP || mux == TYPEC_MUX_DOCK) {
		/* DP uses available superspeed lanes (x2 or x4) */
		gpio_set_level(GPIO_USBC_DP_POLARITY, polarity);
		gpio_set_level(GPIO_USBC_DP_MODE_L, 0);
	}
	/* switch on superspeed lanes */
	gpio_set_level(GPIO_USBC_SS_EN_L, 0);
}

void board_set_usb_mux(int port, enum typec_mux mux,
		       enum usb_switch usb, int polarity)
{
	if (board_get_version() < 5) {
		/* P4/EVT or older boards */
		/* TODO(crosbug.com/p/38333) remove this */
		p4_board_set_usb_mux(port, mux, usb, polarity);
		return;
	}

	/* reset everything */
	gpio_set_level(GPIO_USBC_MUX_CONF0, 0);
	gpio_set_level(GPIO_USBC_MUX_CONF1, 0);
	gpio_set_level(GPIO_USBC_MUX_CONF2, 0);

	/* Set D+/D- switch to appropriate level */
	board_set_usb_switches(port, usb);

	if (mux == TYPEC_MUX_NONE)
		/* everything is already disabled, we can return */
		return;

	gpio_set_level(GPIO_USBC_MUX_CONF0, polarity);

	if (mux == TYPEC_MUX_USB || mux == TYPEC_MUX_DOCK)
		/* USB 3.0 uses 2 superspeed lanes */
		gpio_set_level(GPIO_USBC_MUX_CONF2, 1);

	if (mux == TYPEC_MUX_DP || mux == TYPEC_MUX_DOCK)
		/* DP uses available superspeed lanes (x2 or x4) */
		gpio_set_level(GPIO_USBC_MUX_CONF1, 1);
}

int p4_board_get_usb_mux(int port, const char **dp_str, const char **usb_str)
{
	int has_ss = !gpio_get_level(GPIO_USBC_SS_EN_L);
	int has_usb = !gpio_get_level(GPIO_USBC_SS1_USB_MODE_L) ||
		      !gpio_get_level(GPIO_USBC_SS2_USB_MODE_L);
	int has_dp = !gpio_get_level(GPIO_USBC_DP_MODE_L);

	if (has_dp)
		*dp_str = gpio_get_level(GPIO_USBC_DP_POLARITY) ? "DP2" : "DP1";
	else
		*dp_str = NULL;

	if (has_usb)
		*usb_str = gpio_get_level(GPIO_USBC_SS1_USB_MODE_L) ?
				"USB2" : "USB1";
	else
		*usb_str = NULL;

	return has_ss;
}

int board_get_usb_mux(int port, const char **dp_str, const char **usb_str)
{
	int has_usb, has_dp, polarity;

	if (board_get_version() < 5) {
		/* P4/EVT or older boards */
		/* TODO(crosbug.com/p/38333) remove this */
		return p4_board_get_usb_mux(port, dp_str, usb_str);
	}

	has_usb = gpio_get_level(GPIO_USBC_MUX_CONF2);
	has_dp = gpio_get_level(GPIO_USBC_MUX_CONF1);
	polarity = gpio_get_level(GPIO_USBC_MUX_CONF0);

	if (has_dp)
		*dp_str = polarity ? "DP2" : "DP1";
	else
		*dp_str = NULL;

	if (has_usb)
		*usb_str = polarity ? "USB2" : "USB1";
	else
		*usb_str = NULL;

	return has_dp || has_usb;
}

/**
 * Discharge battery when on AC power for factory test.
 */
int board_discharge_on_ac(int enable)
{
	return charger_discharge_on_ac(enable);
}

int extpower_is_present(void)
{
	return gpio_get_level(GPIO_CHGR_ACOK);
}

void usb_board_connect(void)
{
	gpio_set_level(GPIO_USB_PU_EN_L, 0);
}

void usb_board_disconnect(void)
{
	gpio_set_level(GPIO_USB_PU_EN_L, 1);
}

/* Charge manager callback function, called on delayed override timeout */
void board_charge_manager_override_timeout(void)
{
	/* TODO: Implement me! */
}
DECLARE_DEFERRED(board_charge_manager_override_timeout);

/**
 * Set active charge port -- only one port can be active at a time.
 *
 * @param charge_port   Charge port to enable.
 *
 * Returns EC_SUCCESS if charge port is accepted and made active,
 * EC_ERROR_* otherwise.
 */
int board_set_active_charge_port(int charge_port)
{
	int ret = EC_SUCCESS;
	/* check if we are source vbus on that port */
	int source = gpio_get_level(GPIO_USBC_5V_EN);

	if (charge_port >= 0 && charge_port < PD_PORT_COUNT && source) {
		CPRINTS("Port %d is not a sink, skipping enable", charge_port);
		charge_port = CHARGE_PORT_NONE;
		ret = EC_ERROR_INVAL;
	}
	if (charge_port == CHARGE_PORT_NONE) {
		/* Disable charging */
		charge_set_input_current_limit(0);
	}

	return ret;
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param charge_ma     Desired charge limit (mA).
 */
void board_set_charge_limit(int charge_ma)
{
	int rv;

	charge_current_limit = MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT);
	rv = charge_set_input_current_limit(charge_current_limit);
	if (rv < 0)
		CPRINTS("Failed to set input current limit for PD");
}

/**
 * Return whether ramping is allowed for given supplier
 */
int board_is_ramp_allowed(int supplier)
{
	return supplier == CHARGE_SUPPLIER_BC12_DCP ||
	       supplier == CHARGE_SUPPLIER_BC12_SDP ||
	       supplier == CHARGE_SUPPLIER_BC12_CDP ||
	       supplier == CHARGE_SUPPLIER_PROPRIETARY;
}

/**
 * Return the maximum allowed input current
 */
int board_get_ramp_current_limit(int supplier, int sup_curr)
{
	switch (supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
		return 2000;
	case CHARGE_SUPPLIER_BC12_SDP:
		return 1000;
	case CHARGE_SUPPLIER_BC12_CDP:
	case CHARGE_SUPPLIER_PROPRIETARY:
		return sup_curr;
	default:
		return 500;
	}
}

/**
 * Return if board is consuming full amount of input current
 */
int board_is_consuming_full_charge(void)
{
	return adc_read_channel(ADC_IADP) >= charge_current_limit -
					     IADP_ERROR_MARGIN_MA;
}

/**
 * Return if VBUS is sagging low enough that we should stop ramping
 */
int board_is_vbus_too_low(enum chg_ramp_vbus_state ramp_state)
{
	return adc_read_channel(ADC_VBUS) < VBUS_LOW_THRESHOLD_MV;
}

/*
 * Enable and disable SPI for case closed debugging.  This forces the AP into
 * reset while SPI is enabled, thus preventing contention on the SPI interface.
 */
void usb_spi_board_enable(struct usb_spi_config const *config)
{
	/* Place AP into reset */
	gpio_set_level(GPIO_PMIC_WARM_RESET_L, 0);

	/* Configure SPI GPIOs */
	gpio_config_module(MODULE_SPI_MASTER, 1);
	gpio_set_flags(GPIO_SPI_FLASH_NSS, GPIO_OUT_HIGH);

	/* Set all four SPI pins to high speed */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xf03c0000;

	/* Enable clocks to SPI2 module */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;

	/* Reset SPI2 */
	STM32_RCC_APB1RSTR |= STM32_RCC_PB1_SPI2;
	STM32_RCC_APB1RSTR &= ~STM32_RCC_PB1_SPI2;

	/* Enable SPI LDO to power the flash chip */
	gpio_set_level(GPIO_VDDSPI_EN, 1);

	spi_enable(1);
}

void usb_spi_board_disable(struct usb_spi_config const *config)
{
	spi_enable(0);

	/* Disable SPI LDO */
	gpio_set_level(GPIO_VDDSPI_EN, 0);

	/* Disable clocks to SPI2 module */
	STM32_RCC_APB1ENR &= ~STM32_RCC_PB1_SPI2;

	/* Release SPI GPIOs */
	gpio_config_module(MODULE_SPI_MASTER, 0);
	gpio_set_flags(GPIO_SPI_FLASH_NSS, GPIO_INPUT);

	/* Release AP from reset */
	gpio_set_level(GPIO_PMIC_WARM_RESET_L, 1);
}

int board_get_version(void)
{
	static int ver;

	if (!ver) {
		/*
		 * read the board EC ID on the tristate strappings
		 * using ternary encoding: 0 = 0, 1 = 1, Hi-Z = 2
		 */
		uint8_t id0 = 0, id1 = 0;
		gpio_set_flags(GPIO_BOARD_ID0, GPIO_PULL_DOWN | GPIO_INPUT);
		gpio_set_flags(GPIO_BOARD_ID1, GPIO_PULL_DOWN | GPIO_INPUT);
		usleep(100);
		id0 = gpio_get_level(GPIO_BOARD_ID0);
		id1 = gpio_get_level(GPIO_BOARD_ID1);
		gpio_set_flags(GPIO_BOARD_ID0, GPIO_PULL_UP | GPIO_INPUT);
		gpio_set_flags(GPIO_BOARD_ID1, GPIO_PULL_UP | GPIO_INPUT);
		usleep(100);
		id0 = gpio_get_level(GPIO_BOARD_ID0) && !id0 ? 2 : id0;
		id1 = gpio_get_level(GPIO_BOARD_ID1) && !id1 ? 2 : id1;
		gpio_set_flags(GPIO_BOARD_ID0, GPIO_INPUT);
		gpio_set_flags(GPIO_BOARD_ID1, GPIO_INPUT);
		ver = id1 * 3 + id0;
		CPRINTS("Board ID = %d\n", ver);
	}

	return ver;
}
