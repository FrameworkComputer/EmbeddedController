/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Servo V4p1 configuration */

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
#include "system.h"
#include "task.h"
#include "timer.h"
#include "update_fw.h"
#include "usart-stm32f0.h"
#include "usart_tx_dma.h"
#include "usart_rx_dma.h"
#include "usb_gpio.h"
#include "usb_i2c.h"
#include "usb_spi.h"
#include "usb-stream.h"
#include "util.h"

#ifdef SECTION_IS_RO
#define CROS_EC_SECTION "RO"
#else
#define CROS_EC_SECTION "RW"
#endif

/******************************************************************************
 * GPIO interrupt handlers.
 */
#ifdef SECTION_IS_RO
static void vbus0_evt(enum gpio_signal signal)
{
}

static void vbus1_evt(enum gpio_signal signal)
{
}

static void tca_evt(enum gpio_signal signal)
{
}

static void dp_evt(enum gpio_signal signal)
{
}

static void tcpc_evt(enum gpio_signal signal)
{
}

static void hub_evt(enum gpio_signal signal)
{
}

static void bc12_evt(enum gpio_signal signal)
{
}
#endif /* SECTION_IS_RO */

#include "gpio_list.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/******************************************************************************
 * Board pre-init function.
 */

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= BIT(0);

	/*
	 * the DMA mapping is :
	 *  Chan 2 : TIM1_CH1  (CHG RX) - Default mapping
	 *  Chan 3 : SPI1_TX   (CHG TX) - Default mapping
	 *  Chan 4 : USART1 TX - Remapped from default Chan 2
	 *  Chan 5 : USART1 RX - Remapped from default Chan 3
	 *  Chan 6 : TIM3_CH1  (DUT RX) - Remapped from default Chan 4
	 *  Chan 7 : SPI2_TX   (DUT TX) - Remapped from default Chan 5
	 *
	 * As described in the comments above, both USART1 TX/RX and DUT Tx/RX
	 * channels must be remapped from the defulat locations. Remapping is
	 * acoomplished by setting the following bits in the STM32_SYSCFG_CFGR1
	 * register. Information about this register and its settings can be
	 * found in section 11.3.7 DMA Request Mapping of the STM RM0091
	 * Reference Manual
	 */
	/* Remap USART1 Tx from DMA channel 2 to channel 4 */
	STM32_SYSCFG_CFGR1 |= BIT(9);
	/* Remap USART1 Rx from DMA channel 3 to channel 5 */
	STM32_SYSCFG_CFGR1 |= BIT(10);
	/* Remap TIM3_CH1 from DMA channel 4 to channel 6 */
	STM32_SYSCFG_CFGR1 |= BIT(30);
	/* Remap SPI2 Tx from DMA channel 5 to channel 7 */
	STM32_SYSCFG_CFGR1 |= BIT(24);
}

/******************************************************************************
 * Set up USB PD
 */

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_CHG_CC1_PD] = {"CHG_CC1_PD", 3300, 4096, 0, STM32_AIN(2)},
	[ADC_CHG_CC2_PD] = {"CHG_CC2_PD", 3300, 4096, 0, STM32_AIN(4)},
	[ADC_DUT_CC1_PD] = {"DUT_CC1_PD", 3300, 4096, 0, STM32_AIN(0)},
	[ADC_DUT_CC2_PD] = {"DUT_CC2_PD", 3300, 4096, 0, STM32_AIN(5)},
	[ADC_SBU1_DET] = {"SBU1_DET", 3300, 4096, 0, STM32_AIN(3)},
	[ADC_SBU2_DET] = {"SBU2_DET", 3300, 4096, 0, STM32_AIN(7)},
	[ADC_SUB_C_REF] = {"SUB_C_REF", 3300, 4096, 0, STM32_AIN(1)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);


/******************************************************************************
 * Forward UARTs as a USB serial interface.
 */

#define USB_STREAM_RX_SIZE	16
#define USB_STREAM_TX_SIZE	16

/******************************************************************************
 * Forward USART3 as a simple USB serial interface.
 */

static struct usart_config const usart3;
struct usb_stream_config const usart3_usb;

static struct queue const usart3_to_usb = QUEUE_DIRECT(64, uint8_t,
	usart3.producer, usart3_usb.consumer);
static struct queue const usb_to_usart3 = QUEUE_DIRECT(64, uint8_t,
	usart3_usb.producer, usart3.consumer);

static struct usart_config const usart3 =
	USART_CONFIG(usart3_hw,
		usart_rx_interrupt,
		usart_tx_interrupt,
		115200,
		0,
		usart3_to_usb,
		usb_to_usart3);

USB_STREAM_CONFIG(usart3_usb,
	USB_IFACE_USART3_STREAM,
	USB_STR_USART3_STREAM_NAME,
	USB_EP_USART3_STREAM,
	USB_STREAM_RX_SIZE,
	USB_STREAM_TX_SIZE,
	usb_to_usart3,
	usart3_to_usb)


/******************************************************************************
 * Forward USART4 as a simple USB serial interface.
 */

static struct usart_config const usart4;
struct usb_stream_config const usart4_usb;

static struct queue const usart4_to_usb = QUEUE_DIRECT(64, uint8_t,
	usart4.producer, usart4_usb.consumer);
static struct queue const usb_to_usart4 = QUEUE_DIRECT(64, uint8_t,
	usart4_usb.producer, usart4.consumer);

static struct usart_config const usart4 =
	USART_CONFIG(usart4_hw,
		usart_rx_interrupt,
		usart_tx_interrupt,
		9600,
		0,
		usart4_to_usb,
		usb_to_usart4);

USB_STREAM_CONFIG(usart4_usb,
	USB_IFACE_USART4_STREAM,
	USB_STR_USART4_STREAM_NAME,
	USB_EP_USART4_STREAM,
	USB_STREAM_RX_SIZE,
	USB_STREAM_TX_SIZE,
	usb_to_usart4,
	usart4_to_usb)


/******************************************************************************
 * Define the strings used in our USB descriptors.
 */

const void *const usb_strings[] = {
	[USB_STR_DESC]         = usb_string_desc,
	[USB_STR_VENDOR]       = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT]      = USB_STRING_DESC("Servo V4p1"),
	[USB_STR_SERIALNO]     = USB_STRING_DESC("1234-a"),
	[USB_STR_VERSION]      = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_I2C_NAME]     = USB_STRING_DESC("I2C"),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Servo EC Shell"),
	[USB_STR_USART3_STREAM_NAME]  = USB_STRING_DESC("DUT UART"),
	[USB_STR_USART4_STREAM_NAME]  = USB_STRING_DESC("Atmega UART"),
	[USB_STR_UPDATE_NAME]  = USB_STRING_DESC("Firmware update"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);



/******************************************************************************
 * Support I2C bridging over USB.
 */

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

int usb_i2c_board_is_enabled(void) { return 1; }

/******************************************************************************
 * Initialize board.
 */

int board_get_version(void)
{
	return 0;
}

static void board_init(void)
{
	/* USB to serial queues */
	queue_init(&usart3_to_usb);
	queue_init(&usb_to_usart3);
	queue_init(&usart4_to_usb);
	queue_init(&usb_to_usart4);

	/* UART init */
	usart_init(&usart3);
	usart_init(&usart4);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
