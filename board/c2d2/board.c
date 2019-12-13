/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* C2D2 debug device board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "queue_policies.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "update_fw.h"
#include "usart_rx_dma.h"
#include "usart_tx_dma.h"
#include "usart-stm32f0.h"
#include "usb_hw.h"
#include "usb_i2c.h"
#include "usb_spi.h"
#include "usb-stream.h"
#include "util.h"

#include "gpio_list.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/* Forward declarations */
static void update_vrefs_and_shifters(void);
DECLARE_DEFERRED(update_vrefs_and_shifters);

/* Global state tracking current pin configuration and operations */
static struct mutex vref_uart_state_mutex;
static int vref_monitor_disable;
#define VREF_MON_DIS_H1_RST_HELD	BIT(0)
#define VREF_MON_DIS_EC_PWR_HELD	BIT(1)
#define VREF_MON_DIS_SPI_MODE		BIT(2)

static int uart_state;
#define UART_STATE_HELD			BIT(0)
#define UART_STATE_SPI_MODE		BIT(1)

void board_config_pre_init(void)
{
	/* enable SYSCFG & COMP clock */
	STM32_RCC_APB2ENR |= STM32_RCC_SYSCFGEN;

	/* enable DAC for comparator input */
	STM32_RCC_APB1ENR |= STM32_RCC_DACEN;

	/*
	 * the DMA mapping is :
	 *  Chan 3 : USART3_RX
	 *  Chan 5 : USART1_RX
	 *  Chan 6 : SPI2_RX
	 *  Chan 7 : SPI2_TX
	 *
	 *  i2c : no dma
	 *  tim16/17: no dma
	 */
	STM32_SYSCFG_CFGR1 |= BIT(24);	/* Remap SPI2_RX to channel 6 */
	STM32_SYSCFG_CFGR1 |= BIT(26);  /* Remap USART3 RX/TX DMA */
	STM32_SYSCFG_CFGR1 |= BIT(10);  /* Remap USART1 RX/TX DMA */
}

/******************************************************************************
 ** ADC channels
 */
const struct adc_t adc_channels[] = {
	/* Sensing the H1's voltage at the DUT side.  Converted to mV. */
	[ADC_H1_SPI_VREF] = {
		.name = "H1_VREF",
		.factor_mul = 3300,
		.factor_div = 4096,
		.shift = 0,
		.channel = STM32_AIN(3),
	},
	/* Sensing the EC's voltage at the DUT side.  Converted to mV. */
	[ADC_EC_SPI_VREF] = {
		.name = "EC_VREF",
		.factor_mul = 3300,
		.factor_div = 4096,
		.shift = 0,
		.channel = STM32_AIN(4),
	}
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */
const void *const usb_strings[] = {
	[USB_STR_DESC]			= usb_string_desc,
	[USB_STR_VENDOR]		= USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT]		= USB_STRING_DESC("C2D2"),
	[USB_STR_SERIALNO]		= 0,
	[USB_STR_VERSION]		= USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_USART4_STREAM_NAME]	= USB_STRING_DESC("CR50"),
	[USB_STR_UPDATE_NAME]		= USB_STRING_DESC("Firmware update"),
	[USB_STR_CONSOLE_NAME]		= USB_STRING_DESC("C2D2 Shell"),
	[USB_STR_I2C_NAME]		= USB_STRING_DESC("I2C"),
	[USB_STR_USART3_STREAM_NAME]	= USB_STRING_DESC("CPU"),
	[USB_STR_USART1_STREAM_NAME]	= USB_STRING_DESC("EC"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);


/******************************************************************************
 * Forward UARTs as a USB serial interface.
 */

#define USB_STREAM_RX_SIZE	32
#define USB_STREAM_TX_SIZE	64

/******************************************************************************
 * Forward USART1 (EC) as a simple USB serial interface.
 */

static struct usart_config const usart1;
struct usb_stream_config const usart1_usb;

static struct queue const usart1_to_usb = QUEUE_DIRECT(128, uint8_t,
	usart1.producer, usart1_usb.consumer);
static struct queue const usb_to_usart1 = QUEUE_DIRECT(64, uint8_t,
	usart1_usb.producer, usart1.consumer);

static struct usart_rx_dma const usart1_rx_dma =
	USART_RX_DMA(STM32_DMAC_CH5, 32);

static struct usart_config const usart1 =
	USART_CONFIG(usart1_hw,
		usart1_rx_dma.usart_rx,
		usart_tx_interrupt,
		115200,
		0,
		usart1_to_usb,
		usb_to_usart1);

USB_STREAM_CONFIG_USART_IFACE(usart1_usb,
	USB_IFACE_USART1_STREAM,
	USB_STR_USART1_STREAM_NAME,
	USB_EP_USART1_STREAM,
	USB_STREAM_RX_SIZE,
	USB_STREAM_TX_SIZE,
	usb_to_usart1,
	usart1_to_usb,
	usart1)


/******************************************************************************
 * Forward USART3 (CPU) as a simple USB serial interface.
 */

static struct usart_config const usart3;
struct usb_stream_config const usart3_usb;

static struct queue const usart3_to_usb = QUEUE_DIRECT(1024, uint8_t,
	usart3.producer, usart3_usb.consumer);
static struct queue const usb_to_usart3 = QUEUE_DIRECT(64, uint8_t,
	usart3_usb.producer, usart3.consumer);

static struct usart_rx_dma const usart3_rx_dma =
	USART_RX_DMA(STM32_DMAC_CH3, 32);

static struct usart_config const usart3 =
	USART_CONFIG(usart3_hw,
		usart3_rx_dma.usart_rx,
		usart_tx_interrupt,
		115200,
		0,
		usart3_to_usb,
		usb_to_usart3);

USB_STREAM_CONFIG_USART_IFACE(usart3_usb,
	USB_IFACE_USART3_STREAM,
	USB_STR_USART3_STREAM_NAME,
	USB_EP_USART3_STREAM,
	USB_STREAM_RX_SIZE,
	USB_STREAM_TX_SIZE,
	usb_to_usart3,
	usart3_to_usb,
	usart3)


/******************************************************************************
 * Forward USART4 (cr50) as a simple USB serial interface.
 *
 * We do not try to share DMA channel 6 with SPI2, so just use interrupts
 */

static struct usart_config const usart4;
struct usb_stream_config const usart4_usb;

static struct queue const usart4_to_usb = QUEUE_DIRECT(1024, uint8_t,
	usart4.producer, usart4_usb.consumer);
static struct queue const usb_to_usart4 = QUEUE_DIRECT(64, uint8_t,
	usart4_usb.producer, usart4.consumer);

static struct usart_config const usart4 =
	USART_CONFIG(usart4_hw,
		usart_rx_interrupt,
		usart_tx_interrupt,
		115200,
		0,
		usart4_to_usb,
		usb_to_usart4);

USB_STREAM_CONFIG_USART_IFACE(usart4_usb,
	USB_IFACE_USART4_STREAM,
	USB_STR_USART4_STREAM_NAME,
	USB_EP_USART4_STREAM,
	USB_STREAM_RX_SIZE,
	USB_STREAM_TX_SIZE,
	usb_to_usart4,
	usart4_to_usb,
	usart4)

/******************************************************************************
 * Set up SPI over USB
 * Notes DMA Channel 6 is shared and mutually exclusive with USART4 RX
 */

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_FLASH_PORT, 1, GPIO_SPI_CSN},
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

void usb_spi_board_enable(struct usb_spi_config const *config)
{
	/* Configure SPI GPIOs */
	gpio_config_module(MODULE_SPI_FLASH, 1);

	/* Set all four SPI pins to high speed */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xff000000;

	/* Enable clocks to SPI2 module */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;

	/* Reset SPI2 */
	STM32_RCC_APB1RSTR |= STM32_RCC_PB1_SPI2;
	STM32_RCC_APB1RSTR &= ~STM32_RCC_PB1_SPI2;

	spi_enable(CONFIG_SPI_FLASH_PORT, 1);
}

void usb_spi_board_disable(struct usb_spi_config const *config)
{
	spi_enable(CONFIG_SPI_FLASH_PORT, 0);

	/* Disable clocks to SPI2 module */
	STM32_RCC_APB1ENR &= ~STM32_RCC_PB1_SPI2;

	/* Release SPI GPIOs */
	gpio_config_module(MODULE_SPI_FLASH, 0);

	/* Reset all four SPI pins to low speed */
	STM32_GPIO_OSPEEDR(GPIO_B) &= ~0xff000000;
}

USB_SPI_CONFIG(usb_spi, USB_IFACE_SPI, USB_EP_SPI,
	       USB_SPI_CONFIG_FLAGS_IGNORE_HOST_SIDE_ENABLE);

/******************************************************************************
 * Check parity setting on usarts.
 */
static int command_uart_parity(int argc, char **argv)
{
	int parity = 0, newparity;
	struct usart_config const *usart;
	char *e;

	if ((argc < 2) || (argc > 3))
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "usart1"))
		usart = &usart1;
	else if (!strcasecmp(argv[1], "usart3"))
		usart = &usart3;
	else if (!strcasecmp(argv[1], "usart4"))
		usart = &usart4;
	else
		return EC_ERROR_PARAM1;

	if (argc == 3) {
		parity = strtoi(argv[2], &e, 0);
		if (*e || (parity < 0) || (parity > 2))
			return EC_ERROR_PARAM2;

		usart_set_parity(usart, parity);
	}

	newparity = usart_get_parity(usart);
	ccprintf("Parity on %s is %d.\n", argv[1], newparity);

	if ((argc == 3) && (newparity != parity))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(parity, command_uart_parity,
			"usart[2|3|4] [0|1|2]",
			"Set parity on uart");

/******************************************************************************
 * Set baud rate setting on usarts.
 */
static int command_uart_baud(int argc, char **argv)
{
	int baud = 0;
	struct usart_config const *usart;
	char *e;

	if ((argc < 2) || (argc > 3))
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "usart1"))
		usart = &usart1;
	else if (!strcasecmp(argv[1], "usart3"))
		usart = &usart3;
	else if (!strcasecmp(argv[1], "usart4"))
		usart = &usart4;
	else
		return EC_ERROR_PARAM1;

	baud = strtoi(argv[2], &e, 0);
	if (*e || baud < 0)
		return EC_ERROR_PARAM2;

	usart_set_baud(usart, baud);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(baud, command_uart_baud,
			"usart[2|3|4] rate",
			"Set baud rate on uart");

/******************************************************************************
 * Hold the usart pins low while disabling it, or return it to normal.
 */
static int command_hold_usart_low(int argc, char **argv)
{
	/* Each bit represents if that port is being held low */
	static int usart_status;

	int usart_mask;
	enum gpio_signal rx;

	if (argc > 3 || argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "usart1")) {
		usart_mask = 1 << 1;
		rx = GPIO_UART_EC_TX_DBG_RX_SDA;
	} else if (!strcasecmp(argv[1], "usart3")) {
		usart_mask = 1 << 3;
		rx = GPIO_UART_AP_TX_DBG_RX_INA_SDA;
	} else if (!strcasecmp(argv[1], "usart4")) {
		usart_mask = 1 << 4;
		rx = GPIO_UART_H1_TX_DBG_RX;
	} else {
		return EC_ERROR_PARAM1;
	}

	/* Updating the status of this port */
	if (argc == 3) {
		char *e;
		const int hold_low = strtoi(argv[2], &e, 0);

		if (*e || (hold_low < 0) || (hold_low > 1))
			return EC_ERROR_PARAM2;

		mutex_lock(&vref_uart_state_mutex);

		if (uart_state & UART_STATE_SPI_MODE) {
			ccprintf("Cannot hold USART while in SPI mode\n");
			goto busy_error_unlock;
		}

		if (!!(usart_status & usart_mask) == hold_low) {
			/* Do nothing since there is no change */
		} else if (hold_low) {
			/*
			 * No need to shutdown UART, just de-mux the RX pin from
			 * UART and change it to a GPIO temporarily
			 */
			gpio_config_pin(MODULE_USART, rx, 0);
			gpio_set_flags(rx, GPIO_OUT_LOW);

			/* Update global uart state */
			usart_status |= usart_mask;
			uart_state |= UART_STATE_HELD;
		} else {
			/*
			 * Mux the RX pin back to GPIO mode
			 */
			gpio_config_pin(MODULE_USART, rx, 1);

			/* Update global uart state */
			usart_status &= ~usart_mask;
			uart_state &= ~UART_STATE_HELD;
		}

		mutex_unlock(&vref_uart_state_mutex);
	}

	/* Print status for get and set case. */
	ccprintf("USART status: %s\n",
			usart_status & usart_mask ? "held low" : "normal");

	return EC_SUCCESS;

busy_error_unlock:
	mutex_unlock(&vref_uart_state_mutex);
	return EC_ERROR_BUSY;
}
DECLARE_CONSOLE_COMMAND(hold_usart_low, command_hold_usart_low,
			"usart[1|3|4] [0|1]?",
			"Get/set the hold-low state for usart port");


/******************************************************************************
 * Console commands SPI programming
 */



enum vref {
	OFF = 0,
	PP1800 = 1800,
	PP3300 = 3300,
};

static int command_enable_spi(int argc, char **argv)
{
	static enum vref current_spi_vref_state;

	if (argc > 2)
		return EC_ERROR_PARAM_COUNT;

	/* Updating the state */
	if (argc == 2) {
		char *e;
		const enum vref spi_vref = strtoi(argv[1], &e, 0);

		if (*e)
			return EC_ERROR_PARAM1;
		if (spi_vref != OFF && spi_vref != PP1800 && spi_vref != PP3300)
			return EC_ERROR_PARAM1;

		mutex_lock(&vref_uart_state_mutex);

		if (uart_state & UART_STATE_HELD) {
			ccprintf("Cannot update SPI with UART held.\n");
			goto busy_error_unlock;
		}

		if (vref_monitor_disable & ~VREF_MON_DIS_SPI_MODE) {
			ccprintf("Cannot update SPI with reset held.\n");
			goto busy_error_unlock;
		}

		if (current_spi_vref_state == spi_vref) {
			/* No change, do nothing */
		} else if (spi_vref == OFF) {
			/* We are transitioning from SPI to UART mode: */
			/* Disable level shifter pass through */
			gpio_set_level(GPIO_EN_MISO_MOSI_H1_UART, 0);
			gpio_set_level(GPIO_EN_CLK_CSN_EC_UART, 0);

			/* Disable SPI. Sets SPI pins to inputs. */
			usb_spi_enable(&usb_spi, 0);

			/* Set default state for chip select */
			gpio_set_flags(GPIO_SPI_CSN, GPIO_INPUT);

			/* Re-enable all UARTs pins as UART alternate mode. */
			gpio_config_module(MODULE_USART, 1);

			/* Ensure DUT's muxes are switched to UART mode */
			gpio_set_level(GPIO_C2D2_MUX_UART_ODL, 0);

			/* Update state and defer Vrefs update  */
			vref_monitor_disable &= ~VREF_MON_DIS_SPI_MODE;
			uart_state &= ~UART_STATE_SPI_MODE;
			hook_call_deferred(&update_vrefs_and_shifters_data, 0);
		} else if (vref_monitor_disable & VREF_MON_DIS_SPI_MODE) {
			/* We are just changing voltages */
			gpio_set_level(GPIO_SEL_SPIVREF_H1VREF_3V3,
				       spi_vref == PP3300);
			gpio_set_level(GPIO_SEL_SPIVREF_ECVREF_3V3,
				       spi_vref == PP3300);
		} else {
			/* We are transitioning from UART to SPI mode: */
			/* Turn off comparator interrupt for Vref detection */
			STM32_EXTI_IMR &= ~EXTI_COMP2_EVENT;

			/* Disable level shifters to avoid glitching output */
			gpio_set_level(GPIO_EN_MISO_MOSI_H1_UART, 0);
			gpio_set_level(GPIO_EN_CLK_CSN_EC_UART, 0);

			/*
			 * De-select UART on all UARTs pins to avoid drive
			 * fights with SPI pins.
			 */
			gpio_config_module(MODULE_USART, 0);

			/* Set default state for chip select */
			gpio_set_flags(GPIO_SPI_CSN, GPIO_OUT_HIGH);

			/* Enable SPI. Sets SPI pins to SPI alternate mode. */
			usb_spi_enable(&usb_spi, 1);

			/* Set requested Vref voltage */
			gpio_set_level(GPIO_SEL_SPIVREF_H1VREF_3V3,
				       spi_vref == PP3300);
			gpio_set_level(GPIO_SEL_SPIVREF_ECVREF_3V3,
				       spi_vref == PP3300);

			/* Ensure DUT's muxes are switched to SPI mode */
			gpio_set_level(GPIO_C2D2_MUX_UART_ODL, 1);

			/* Enable level shifters passthrough */
			gpio_set_level(GPIO_EN_MISO_MOSI_H1_UART, 1);
			gpio_set_level(GPIO_EN_CLK_CSN_EC_UART, 1);

			vref_monitor_disable |= VREF_MON_DIS_SPI_MODE;
			uart_state |= UART_STATE_SPI_MODE;
		}

		current_spi_vref_state = spi_vref;

		mutex_unlock(&vref_uart_state_mutex);
	}

	/* Print status for get and set case. */
	ccprintf("SPI Vref: %d\n", current_spi_vref_state);

	return EC_SUCCESS;

busy_error_unlock:
	mutex_unlock(&vref_uart_state_mutex);
	return EC_ERROR_BUSY;
}
DECLARE_CONSOLE_COMMAND(enable_spi, command_enable_spi,
			"[0|1800|3300]?",
			"Get/set the SPI Vref");

/******************************************************************************
 * Console commands for asserting H1 reset and EC Power button
 */

static int command_vref_alternate(int argc, char **argv,
				  const enum gpio_signal vref_signal,
				  const enum gpio_signal en_signal,
				  const int state_flag,
				  const char *const print_name)
{
	if (argc > 2)
		return EC_ERROR_PARAM_COUNT;

	/* Updating the state */
	if (argc == 2) {
		char *e;
		const int hold_low = strtoi(argv[1], &e, 0);

		if (*e || (hold_low < 0) || (hold_low > 1))
			return EC_ERROR_PARAM1;

		mutex_lock(&vref_uart_state_mutex);

		if (vref_monitor_disable & VREF_MON_DIS_SPI_MODE) {
			ccprintf("Cannot hold pin while in SPI mode.\n");
			goto busy_error_unlock;
		}

		if (!!(vref_monitor_disable & state_flag) == hold_low) {
			/* No change, do nothing */
		} else if (hold_low) {
			/* Turn off comparator interrupt for vref detection */
			STM32_EXTI_IMR &= ~EXTI_COMP2_EVENT;
			/* Start holding the ODL signal line low */
			gpio_set_flags(vref_signal, GPIO_OUT_LOW);
			/* Ensure the switch is connecting STM to DUT */
			gpio_set_level(en_signal, 1);
			vref_monitor_disable |= state_flag;
		} else {
			/* Return GPIO back to input for vref detection */
			gpio_set_flags(vref_signal, GPIO_INPUT);
			/* Transitioning out of hold, correct vrefs */
			hook_call_deferred(&update_vrefs_and_shifters_data, 0);
			vref_monitor_disable &= ~state_flag;
		}

		mutex_unlock(&vref_uart_state_mutex);
	}

	/* Print status for both get and set case */
	ccprintf("%s held: %s\n", print_name,
		 vref_monitor_disable & state_flag ? "yes" : "no");


	return EC_SUCCESS;

busy_error_unlock:
	mutex_unlock(&vref_uart_state_mutex);
	return EC_ERROR_BUSY;
}

static int command_pwr_button(int argc, char **argv)
{
	return command_vref_alternate(argc, argv,
				      GPIO_SPIVREF_HOLDN_ECVREF_H1_PWRBTN_ODL,
				      GPIO_EN_SPIVREF_HOLDN_ECVREF_H1_PWRBTN,
				      VREF_MON_DIS_EC_PWR_HELD, "Power button");
}
DECLARE_CONSOLE_COMMAND(pwr_button, command_pwr_button,
			"[0|1]?",
			"Get/set the power button state");

static int command_h1_reset(int argc, char **argv)
{
	return command_vref_alternate(argc, argv,
				      GPIO_SPIVREF_RSVD_H1VREF_H1_RST_ODL,
				      GPIO_EN_SPIVREF_RSVD_H1VREF_H1_RST,
				      VREF_MON_DIS_H1_RST_HELD, "H1 reset");
}
DECLARE_CONSOLE_COMMAND(h1_reset, command_h1_reset,
			"[0|1]?",
			"Get/set the h1 reset state");

/******************************************************************************
 * Vref detection logic
 */

/* Voltage thresholds for rail detection */
#define VREF_3300_MIN_MV 2300
#define VREF_1800_MIN_MV 1500

static enum vref get_vref(enum adc_channel chan)
{
	const int adc = adc_read_channel(chan);

	if (adc == ADC_READ_ERROR)
		return OFF;
	else if (adc > VREF_3300_MIN_MV)
		return PP3300;
	else if (adc > VREF_1800_MIN_MV)
		return PP1800;
	else
		return OFF;
}

static inline void drain_vref_lines(void)
{
	mutex_lock(&vref_uart_state_mutex);
	if (vref_monitor_disable) {
		mutex_unlock(&vref_uart_state_mutex);
		return;
	}

	/*
	 * Disconnect level shifters to prevent any leakage on DUT side while we
	 * are draining Vref lines for a proper read.
	 */
	gpio_set_level(GPIO_EN_MISO_MOSI_H1_UART, 0);
	gpio_set_level(GPIO_EN_CLK_CSN_EC_UART, 0);

	/* Disconnect Vref switches */
	gpio_set_level(GPIO_EN_SPIVREF_RSVD_H1VREF_H1_RST, 0);
	gpio_set_level(GPIO_EN_SPIVREF_HOLDN_ECVREF_H1_PWRBTN, 0);

	/* Actively pull down floating voltage */
	gpio_set_flags(GPIO_SPIVREF_RSVD_H1VREF_H1_RST_ODL, GPIO_OUT_LOW);
	gpio_set_flags(GPIO_SPIVREF_HOLDN_ECVREF_H1_PWRBTN_ODL, GPIO_OUT_LOW);

	/* Ensure we have enough time to drain line. Not in mutex */
	mutex_unlock(&vref_uart_state_mutex);
	msleep(5);
	mutex_lock(&vref_uart_state_mutex);
	if (vref_monitor_disable) {
		mutex_unlock(&vref_uart_state_mutex);
		/*
		 * One or both of the Vref signals will still be low. This is
		 * okay since anyone that just took over these signal will
		 * also take over the enabled switch signals appropriately.
		 *
		 * If no one takes over the Vref signal, then the switch will
		 * remain off and we won't pull down the DUT side.
		 */
		return;
	}

	/* Reset Vref GPIOs back to input for Vref detection */
	gpio_set_flags(GPIO_SPIVREF_RSVD_H1VREF_H1_RST_ODL, GPIO_INPUT);
	gpio_set_flags(GPIO_SPIVREF_HOLDN_ECVREF_H1_PWRBTN_ODL, GPIO_INPUT);

	/* Reconnect Vref switches */
	gpio_set_level(GPIO_EN_SPIVREF_RSVD_H1VREF_H1_RST, 1);
	gpio_set_level(GPIO_EN_SPIVREF_HOLDN_ECVREF_H1_PWRBTN, 1);

	mutex_unlock(&vref_uart_state_mutex);
	/* Ensure we have enough time to charge line up to real voltage */
	msleep(10);
}

/* This if forward declared as a deferred function above */
static void update_vrefs_and_shifters(void)
{
	static enum vref prev_h1_vref, prev_ec_vref;

	enum vref h1_vref, ec_vref;
	int adc_mv;

	/* Disable Vref comparator interrupt before draining and measuring */
	STM32_EXTI_IMR &= ~EXTI_COMP2_EVENT;

	drain_vref_lines();

	/* Ensure we aren't actively using Vref lines for other purposes */
	mutex_lock(&vref_uart_state_mutex);
	if (vref_monitor_disable) {
		mutex_unlock(&vref_uart_state_mutex);
		return;
	}

	/* Only get the EC Vref if H1 Vref is on */
	h1_vref = get_vref(ADC_H1_SPI_VREF);
	ec_vref = (h1_vref == OFF) ? OFF : get_vref(ADC_EC_SPI_VREF);

	/*
	 * It is possible that the user is physically holding the power button
	 * while inserting the c2d2 connector on the DUT. In that case the
	 * EC Vref (shared with power button ODL) will be OFF while H1 Vref is
	 * on. We won't get a valid read on the EC Vref, so we just keep trying
	 * to read in the background until we get out of that state.
	 */
	if (h1_vref != OFF && ec_vref == OFF) {
		CPRINTS("Looks like DUT power button is held. Will try again.");
		hook_call_deferred(&update_vrefs_and_shifters_data, 100 * MSEC);
	}

	/* Update C2D2 Vref and level shifters based on ADC Vref values */
	gpio_set_level(GPIO_SEL_SPIVREF_H1VREF_3V3, h1_vref == PP3300);
	gpio_set_level(GPIO_EN_MISO_MOSI_H1_UART, h1_vref != OFF);
	gpio_set_level(GPIO_SEL_SPIVREF_ECVREF_3V3, ec_vref == PP3300);
	gpio_set_level(GPIO_EN_CLK_CSN_EC_UART, ec_vref != OFF);

	/* Set up DAC2 for comparison on H1 Vref */
	adc_mv = (h1_vref == PP3300) ? VREF_3300_MIN_MV : VREF_1800_MIN_MV;
	/* 8-bit DAC based off of 3.3V rail */
	STM32_DAC_DHR8R2 = 256 * adc_mv / 3300;

	/* Clear any pending interrupts and enabled H1 Vref comparator */
	STM32_EXTI_PR = EXTI_COMP2_EVENT;
	STM32_EXTI_IMR |= EXTI_COMP2_EVENT;

	mutex_unlock(&vref_uart_state_mutex);

	if (prev_h1_vref != h1_vref || prev_ec_vref != ec_vref)
		CPRINTS("Vref updated. H1: %d -> %d; EC: %d -> %d",
			prev_h1_vref, h1_vref, prev_ec_vref, ec_vref);

	/*
	 * Transitioning from 3.3V to 1.8V should not happen and most likely
	 * indicates a leakage path on the DUT being backpowered from C2D2 or
	 * something else.
	 */
	if (prev_h1_vref == PP3300 && h1_vref == PP1800)
		CPRINTS("Check for H1 Leakage!!!");
	if (prev_ec_vref == PP3300 && ec_vref == PP1800)
		CPRINTS("Check for EC Leakage!!!");
	prev_h1_vref = h1_vref;
	prev_ec_vref = ec_vref;
}

void set_up_comparator(void)
{
	/* Overwrite any previous values. This is the only comparator usage */
	STM32_COMP_CSR = STM32_COMP_CMP2HYST_HI |
			 STM32_COMP_CMP2OUTSEL_NONE |
			 STM32_COMP_CMP2INSEL_INM5 | /* Watch DAC_OUT2 (PA5) */
			 STM32_COMP_CMP2MODE_LSPEED |
			 STM32_COMP_CMP2EN;

	/* Set Falling and Rising interrupts for COMP2 */
	STM32_EXTI_FTSR |= EXTI_COMP2_EVENT;
	STM32_EXTI_RTSR |= EXTI_COMP2_EVENT;

	/* Interrupt for COMP2 enabled when setting Vrefs */

	/* Ensure IRQ will get called when comp module enables interrupt */
	task_enable_irq(STM32_IRQ_COMP);
}

static void h1_vref_change(void)
{
	/* Ack the interrupt */
	STM32_EXTI_PR = EXTI_COMP2_EVENT;

	/* Disable interrupt, setting Vref will enable again */
	STM32_EXTI_IMR &= ~EXTI_COMP2_EVENT;

	hook_call_deferred(&update_vrefs_and_shifters_data, 0);
}
DECLARE_IRQ(STM32_IRQ_COMP, h1_vref_change, 1);

/******************************************************************************
 * Initialize board.
 */
static void board_init(void)
{
	/* USB to serial queues */
	queue_init(&usart1_to_usb);
	queue_init(&usb_to_usart1);
	queue_init(&usart3_to_usb);
	queue_init(&usb_to_usart3);
	queue_init(&usart4_to_usb);
	queue_init(&usb_to_usart4);

	/* UART init */
	usart_init(&usart1);
	usart_init(&usart3);
	usart_init(&usart4);

	/* Enabled DAC, when setting Vref, this voltage is adjusted */
	STM32_DAC_CR = STM32_DAC_CR_EN2;

	/* Set Vrefs and enabled level shifters */
	set_up_comparator();

	/*
	 * Ensure we set up vrefs at least once. Don't call here because
	 * there are delays in the reads
	 */
	hook_call_deferred(&update_vrefs_and_shifters_data, 0);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/******************************************************************************
 * Turn down USART before jumping to RW.
 */
static void board_jump(void)
{
	/* Put the board into safer state while jumping */
	gpio_set_level(GPIO_EN_SPIVREF_RSVD_H1VREF_H1_RST, 0);
	gpio_set_level(GPIO_EN_SPIVREF_HOLDN_ECVREF_H1_PWRBTN, 0);
	gpio_set_level(GPIO_EN_CLK_CSN_EC_UART, 0);
	gpio_set_level(GPIO_EN_MISO_MOSI_H1_UART, 0);

	/*
	 * Shutdown all UARTS before jumping to RW. They will be reinitialized
	 * after the jump is successful.
	 */
	usart_shutdown(&usart1);
	usart_shutdown(&usart3);
	usart_shutdown(&usart4);

	/* Ensure SPI2 is disabled as well */
	usb_spi_enable(&usb_spi, 0);
}
DECLARE_HOOK(HOOK_SYSJUMP, board_jump, HOOK_PRIO_DEFAULT);
