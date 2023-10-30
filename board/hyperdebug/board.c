/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* HyperDebug board configuration */

#include "adc.h"
#include "common.h"
#include "ec_version.h"
#include "queue_policies.h"
#include "registers.h"
#include "spi.h"
#include "stm32-dma.h"
#include "timer.h"
#include "usart-stm32l5.h"
#include "usb-stream.h"
#include "usb_spi.h"

#include <stdio.h>

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= STM32_RCC_SYSCFGEN;

	/* We know VDDIO2 is present, enable the GPIO circuit. */
	STM32_PWR_CR2 |= STM32_PWR_CR2_IOSV;
}

/******************************************************************************
 * Forward UARTs as a USB serial interface.
 */

#define USB_STREAM_RX_SIZE 16
#define USB_STREAM_TX_SIZE 16

/******************************************************************************
 * Forward USART2 as a simple USB serial interface.
 */

static struct usart_config const usart2;
struct usb_stream_config const usart2_usb;

static struct queue const usart2_to_usb =
	QUEUE_DIRECT(64, uint8_t, usart2.producer, usart2_usb.consumer);
static struct queue const usb_to_usart2 =
	QUEUE_DIRECT(64, uint8_t, usart2_usb.producer, usart2.consumer);

static struct usart_config const usart2 =
	USART_CONFIG(usart2_hw, usart_rx_interrupt, usart_tx_interrupt, 115200,
		     0, usart2_to_usb, usb_to_usart2);

USB_STREAM_CONFIG(usart2_usb, USB_IFACE_USART2_STREAM,
		  USB_STR_USART2_STREAM_NAME, USB_EP_USART2_STREAM,
		  USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE, usb_to_usart2,
		  usart2_to_usb)

/******************************************************************************
 * Forward USART3 as a simple USB serial interface.
 */

static struct usart_config const usart3;
struct usb_stream_config const usart3_usb;

static struct queue const usart3_to_usb =
	QUEUE_DIRECT(64, uint8_t, usart3.producer, usart3_usb.consumer);
static struct queue const usb_to_usart3 =
	QUEUE_DIRECT(64, uint8_t, usart3_usb.producer, usart3.consumer);

static struct usart_config const usart3 =
	USART_CONFIG(usart3_hw, usart_rx_interrupt, usart_tx_interrupt, 115200,
		     0, usart3_to_usb, usb_to_usart3);

USB_STREAM_CONFIG(usart3_usb, USB_IFACE_USART3_STREAM,
		  USB_STR_USART3_STREAM_NAME, USB_EP_USART3_STREAM,
		  USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE, usb_to_usart3,
		  usart3_to_usb)

/******************************************************************************
 * Forward USART4 as a simple USB serial interface.
 */

static struct usart_config const usart4;
struct usb_stream_config const usart4_usb;

static struct queue const usart4_to_usb =
	QUEUE_DIRECT(64, uint8_t, usart4.producer, usart4_usb.consumer);
static struct queue const usb_to_usart4 =
	QUEUE_DIRECT(64, uint8_t, usart4_usb.producer, usart4.consumer);

static struct usart_config const usart4 =
	USART_CONFIG(usart4_hw, usart_rx_interrupt, usart_tx_interrupt, 115200,
		     0, usart4_to_usb, usb_to_usart4);

USB_STREAM_CONFIG(usart4_usb, USB_IFACE_USART4_STREAM,
		  USB_STR_USART4_STREAM_NAME, USB_EP_USART4_STREAM,
		  USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE, usb_to_usart4,
		  usart4_to_usb)

/******************************************************************************
 * Forward USART5 as a simple USB serial interface.
 */

static struct usart_config const usart5;
struct usb_stream_config const usart5_usb;

static struct queue const usart5_to_usb =
	QUEUE_DIRECT(64, uint8_t, usart5.producer, usart5_usb.consumer);
static struct queue const usb_to_usart5 =
	QUEUE_DIRECT(64, uint8_t, usart5_usb.producer, usart5.consumer);

static struct usart_config const usart5 =
	USART_CONFIG(usart5_hw, usart_rx_interrupt, usart_tx_interrupt, 115200,
		     0, usart5_to_usb, usb_to_usart5);

USB_STREAM_CONFIG(usart5_usb, USB_IFACE_USART5_STREAM,
		  USB_STR_USART5_STREAM_NAME, USB_EP_USART5_STREAM,
		  USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE, usb_to_usart5,
		  usart5_to_usb)

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */

const void *const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google LLC"),
	[USB_STR_PRODUCT] = USB_STRING_DESC("HyperDebug CMSIS-DAP"),
	[USB_STR_SERIALNO] = 0,
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("HyperDebug Shell"),
	[USB_STR_SPI_NAME] = USB_STRING_DESC("SPI"),
	[USB_STR_CMSIS_DAP_NAME] = USB_STRING_DESC("I2C CMSIS-DAP"),
	[USB_STR_USART2_STREAM_NAME] = USB_STRING_DESC("UART2"),
	[USB_STR_USART3_STREAM_NAME] = USB_STRING_DESC("UART3"),
	[USB_STR_USART4_STREAM_NAME] = USB_STRING_DESC("UART4"),
	[USB_STR_USART5_STREAM_NAME] = USB_STRING_DESC("UART5"),
	[USB_STR_DFU_NAME] = USB_STRING_DESC("DFU"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/******************************************************************************
 * Set up USB PD
 */

/* ADC channels */
const struct adc_t adc_channels[] = {
	/*
	 * All available ADC signals, converted to mV (3300mV/4096).  Every one
	 * is declared with same name as the GPIO signal on the same pin, that
	 * is how opentitantool identifies the signal.
	 *
	 * Technically, the Nucleo-L552ZE-Q board can run at either 1v8 or 3v3
	 * supply, but we use HyperDebug only on 3v3 setting.  If in the future
	 * we want to detect actual voltage, Vrefint could be used.  This would
	 * also serve as calibration as the supply voltage may not be 3300mV
	 * exactly.
	 */
	[ADC_CN9_11] = { "CN9_11", 3300, 4096, 0, STM32_AIN(1) },
	[ADC_CN9_9] = { "CN9_9", 3300, 4096, 0, STM32_AIN(2) },
	/*[ADC_CN10_9] = { "CN10_9", 3300, 4096, 0, STM32_AIN(3) },*/
	[ADC_CN9_5] = { "CN9_5", 3300, 4096, 0, STM32_AIN(4) },
	[ADC_CN10_29] = { "CN10_29", 3300, 4096, 0, STM32_AIN(5) },
	[ADC_CN10_11] = { "CN10_11", 3300, 4096, 0, STM32_AIN(6) },
	[ADC_CN9_3] = { "CN9_3", 3300, 4096, 0, STM32_AIN(7) },
	[ADC_CN9_1] = { "CN9_1", 3300, 4096, 0, STM32_AIN(8) },
	[ADC_CN7_9] = { "CN7_9", 3300, 4096, 0, STM32_AIN(9) },
	[ADC_CN7_10] = { "CN7_10", 3300, 4096, 0, STM32_AIN(10) },
	[ADC_CN7_12] = { "CN7_12", 3300, 4096, 0, STM32_AIN(11) },
	[ADC_CN7_14] = { "CN7_14", 3300, 4096, 0, STM32_AIN(12) },
	[ADC_CN9_7] = { "CN9_7", 3300, 4096, 0, STM32_AIN(15) },
	[ADC_CN10_7] = { "CN10_7", 3300, 4096, 0, STM32_AIN(16) },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************
 * Initialize board.
 */

static void board_init(void)
{
	timestamp_t deadline;

	/* USB to serial queues */
	queue_init(&usart2_to_usb);
	queue_init(&usb_to_usart2);
	queue_init(&usart3_to_usb);
	queue_init(&usb_to_usart3);
	queue_init(&usart4_to_usb);
	queue_init(&usb_to_usart4);
	queue_init(&usart5_to_usb);
	queue_init(&usb_to_usart5);

	/* UART init */
	usart_init(&usart2);
	usart_init(&usart3);
	usart_init(&usart4);
	usart_init(&usart5);

	/* Structured endpoints */
	usb_spi_enable(1);

	/* Configure SPI GPIOs */
	gpio_config_module(MODULE_SPI, 1);

	/*
	 * Unlike most SPI, I2C and UARTs, which are configured in their
	 * alternate mode by default, SPI1 pins are in GPIO input mode on
	 * HyperDebug power-on, for compatibility with previous firmwares.  In
	 * the future we may decide to leave even more functions off by default,
	 * in order for HyperDebug to actively drive as little at possible on
	 * boot.  It is relatively straightforward to declare pins as "Alternate
	 * mode" in opentitantool json configuration file, to have them enabled
	 * by "transport init".
	 *
	 * The code below sets up the alternate function "number" for the
	 * relevant pins, such that when alternate mode is enabled on the pins,
	 * the result is the particular alternate function that HyperDebug
	 * firmware has chosen for the pin.
	 */
	STM32_GPIO_AFRL(STM32_GPIOA_BASE) |= 0x55000000; /* SPI1: PA6/PA7
							    HIDO/HODI */
	STM32_GPIO_AFRL(STM32_GPIOB_BASE) |= 0x00005000; /* SPI1: PB3 SCK */

	/*
	 * Enable TIMER3 in downward mode for precise JTAG bit-banging.
	 */
	STM32_RCC_APB1ENR1 |= STM32_RCC_APB1ENR1_TIM3EN;
	STM32_TIM_CR1(3) = STM32_TIM_CR1_DIR_DOWN | STM32_TIM_CR1_CEN;

	/* Enable ADC */
	STM32_RCC_AHB2ENR |= STM32_RCC_AHB2ENR_ADCEN;
	/* Initialize the ADC by performing a fake reading */
	adc_read_channel(ADC_CN9_11);

	/* Enable DAC */
	STM32_RCC_APB1ENR |= STM32_RCC_APB1ENR1_DAC1EN;

	/*
	 * Enable SPI1.
	 */

	/* Enable clocks to SPI1 module */
	STM32_RCC_APB2ENR |= STM32_RCC_APB2ENR_SPI1EN;

	/* Reset SPI1 */
	STM32_RCC_APB2RSTR |= STM32_RCC_APB2RSTR_SPI1RST;
	STM32_RCC_APB2RSTR &= ~STM32_RCC_APB2RSTR_SPI1RST;

	spi_enable(&spi_devices[2], 1);

	/*
	 * Enable SPI2.
	 */

	/* Enable clocks to SPI2 module */
	STM32_RCC_APB1ENR1 |= STM32_RCC_APB1ENR1_SPI2EN;

	/* Reset SPI2 */
	STM32_RCC_APB1RSTR1 |= STM32_RCC_APB1RSTR1_SPI2RST;
	STM32_RCC_APB1RSTR1 &= ~STM32_RCC_APB1RSTR1_SPI2RST;

	spi_enable(&spi_devices[0], 1);

	/*
	 * Enable OCTOSPI, no driver for this in chip/stm32.
	 */
	deadline.val = get_time().val + OCTOSPI_INIT_TIMEOUT_US;

	STM32_RCC_AHB3ENR |= STM32_RCC_AHB3ENR_QSPIEN;
	while (STM32_OCTOSPI_SR & STM32_OCTOSPI_SR_BUSY) {
		timestamp_t now = get_time();
		if (timestamp_expired(deadline, &now)) {
			/*
			 * Ideally, the USB host would have a way of
			 * discovering our failure to initialize OctoSPI.  But
			 * for now, log and move on, this would happen only on
			 * code bug or hardware failure.
			 */
			cprints(CC_SPI, "Initialization of OctoSPI failed");
			break;
		}
	}

	/*
	 * Declare that a "Standard" SPI flash device, maximum size is connected
	 * to OCTOSPI.  This allows the controller to send arbitrary 32-bit
	 * addresses, which is needed as we use the instruction and address
	 * bytes as arbitrary data to send via SPI.
	 */
	STM32_OCTOSPI_DCR1 = STM32_OCTOSPI_DCR1_MTYP_STANDARD |
			     STM32_OCTOSPI_DCR1_DEVSIZE_MSK;
	/* Clock prescaler (max value 255) */
	STM32_OCTOSPI_DCR2 = spi_devices[1].div;

	/* Select DMA channel */
	dma_select_channel(STM32_DMAC_CH13, DMAMUX_REQ_OCTOSPI1);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static int command_reinit(int argc, const char **argv)
{
	/* Let every module know to re-initialize to power-on state. */
	hook_notify(HOOK_REINIT);
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND_FLAGS(
	reinit, command_reinit, "",
	"Stop any ongoing operation, revert to power-on state.",
	CMD_FLAG_RESTRICTED);

const char *board_read_serial(void)
{
	const uint32_t *stm32_unique_id =
		(const uint32_t *)STM32_UNIQUE_ID_BASE;
	static char serial[13];

	// Compute 12 hex digits from three factory programmed 32-bit "Unique
	// ID" words in a manner that has been observed to be consistent with
	// how the STM DFU ROM bootloader presents its serial number.  This
	// means that the serial number of any particular HyperDebug board will
	// remain the same as it enters and leaves DFU mode for software
	// upgrade.
	int rc = snprintf(serial, sizeof(serial), "%08X%04X",
			  stm32_unique_id[0] + stm32_unique_id[2],
			  stm32_unique_id[1] >> 16);
	if (12 != rc)
		return NULL;
	return serial;
}
