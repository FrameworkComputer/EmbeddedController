/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Servo V4 configuration */

#include "adc.h"
#include "adc_chip.h"
#include "case_closed_debug.h"
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
#include "usart-stm32f0.h"
#include "usart_tx_dma.h"
#include "usart_rx_dma.h"
#include "usb_gpio.h"
#include "usb_i2c.h"
#include "usb_spi.h"
#include "usb-stream.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/******************************************************************************
 * Board pre-init function.
 */

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= 1 << 0;

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
	STM32_SYSCFG_CFGR1 |= (1 << 9);
	/* Remap USART1 Rx from DMA channel 3 to channel 5 */
	STM32_SYSCFG_CFGR1 |= (1 << 10);
	/* Remap TIM3_CH1 from DMA channel 4 to channel 6 */
	STM32_SYSCFG_CFGR1 |= (1 << 30);
	/* Remap SPI2 Tx from DMA channel 5 to channel 7 */
	STM32_SYSCFG_CFGR1 |= (1 << 24);
}

/******************************************************************************
 * Build GPIO tables and expose a subset of the GPIOs over USB.
 */

#include "gpio_list.h"

static enum gpio_signal const usb_gpio_list[] = {
/* Outputs */
GPIO_DUT_CHG_EN,		/* 0 */
GPIO_HOST_OR_CHG_CTL,
GPIO_DP_HPD,
GPIO_SBU_UART_SEL,
GPIO_HOST_USB_HUB_RESET_L,
GPIO_FASTBOOT_DUTHUB_MUX_SEL,	/* 5 */
GPIO_SBU_MUX_EN,
GPIO_FASTBOOT_DUTHUB_MUX_EN_L,
GPIO_DUT_HUB_USB_RESET_L,
GPIO_ATMEL_HWB_L,
GPIO_CMUX_EN,			/* 10 */
GPIO_EMMC_MUX_EN_L,
GPIO_EMMC_PWR_EN,


/* Inputs */
GPIO_USERVO_FAULT_L,
GPIO_USB_FAULT_L,
GPIO_DONGLE_DET,		/* 15 */

GPIO_USB_DET_PP_DUT,
GPIO_USB_DET_PP_CHG,

GPIO_USB_DUT_CC2_RPUSB,
GPIO_USB_DUT_CC2_RD,
GPIO_USB_DUT_CC2_RA,		/* 20 */
GPIO_USB_DUT_CC1_RP3A0,
GPIO_USB_DUT_CC1_RP1A5,
GPIO_USB_DUT_CC1_RPUSB,
GPIO_USB_DUT_CC1_RD,
GPIO_USB_DUT_CC1_RA,		/* 25 */
GPIO_USB_DUT_CC2_RP3A0,
GPIO_USB_DUT_CC2_RP1A5,

};

/*
 * This instantiates struct usb_gpio_config const usb_gpio, plus several other
 * variables, all named something beginning with usb_gpio_
 */
USB_GPIO_CONFIG(usb_gpio,
		usb_gpio_list,
		USB_IFACE_GPIO,
		USB_EP_GPIO);


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
	[USB_STR_PRODUCT]      = USB_STRING_DESC("Servo V4"),
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
 * Support I2C bridging over USB, this requires usb_i2c_board_enable and
 * usb_i2c_board_disable to be defined to enable and disable the SPI bridge.
 */

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

int usb_i2c_board_enable(void) {return EC_SUCCESS; }
void usb_i2c_board_disable(int debounce) {}


/******************************************************************************
 * Support firmware upgrade over USB. We can update whichever section is not
 * the current section.
 */

/*
 * This array defines possible sections available for the firmware update.
 * The section which does not map the current executing code is picked as the
 * valid update area. The values are offsets into the flash space.
 */
const struct section_descriptor board_rw_sections[] = {
	{CONFIG_RO_MEM_OFF,
	 CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE},
	{CONFIG_RW_MEM_OFF,
	 CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE},
};
const struct section_descriptor * const rw_sections = board_rw_sections;
const int num_rw_sections = ARRAY_SIZE(board_rw_sections);


/******************************************************************************
 * Initialize board.
 */

/* Write a GPIO output on the tca6416 I2C ioexpander. */
static void write_ioexpander(int bank, int gpio, int val)
{
	int tmp;

	/* Read output port register */
	i2c_read8(1, 0x40, 0x2 + bank, &tmp);
	if (val)
		tmp |= (1 << gpio);
	else
		tmp &= ~(1 << gpio);
	/* Write back modified output port register */
	i2c_write8(1, 0x40, 0x2 + bank, tmp);

	/* Set Configuration port to output/0 */
	i2c_read8(1, 0x40, 0x6 + bank, &tmp);
	i2c_write8(1, 0x40, 0x6 + bank, tmp & ~(1 << gpio));
}

/* Enable uservo USB. */
static void init_uservo_port(void)
{
	/* Write USERVO_POWER_EN */
	write_ioexpander(0, 7, 1);
	/* Write USERVO_FASTBOOT_MUX_SEL */
	write_ioexpander(1, 0, 0);
}

/* Enable all ioexpander outputs. */
static void init_ioexpander(void)
{
	/* Write all GPIO to output 0 */
	i2c_write8(1, 0x40, 0x2, 0x0);
	i2c_write8(1, 0x40, 0x3, 0x0);
	/* Write all GPIO to output direction */
	i2c_write8(1, 0x40, 0x6, 0x0);
	i2c_write8(1, 0x40, 0x7, 0x0);
}

/* Define voltage thresholds for SBU USB detection */
#define GND_MAX_MV	350
#define USB_HIGH_MV	1500
/* Tracks current state of ccd */
static int ccd_mode;

static void ccd_measure_sbu(void);
DECLARE_DEFERRED(ccd_measure_sbu);

static void ccd_measure_sbu(void)
{
	int sbu1;
	int sbu2;

	/* Read sbu voltage levels */
	sbu1 = adc_read_channel(ADC_SBU1_DET);
	sbu2 = adc_read_channel(ADC_SBU2_DET);

	/* USB FS pulls one line high for connect request */
	if ((sbu1 > USB_HIGH_MV) && (sbu2 < GND_MAX_MV)) {
		/* SBU flip = 1 */
		write_ioexpander(0, 2, 1);
		msleep(10);
		gpio_set_level(GPIO_SBU_MUX_EN, 1);
		ccd_mode = CCD_MODE_ENABLED;
		CPRINTS("CCD: connected flip");
	} else if ((sbu2 > USB_HIGH_MV) &&
		   (sbu1 < GND_MAX_MV)) {
		/* SBU flip = 0 */
		write_ioexpander(0, 2, 0);
		msleep(10);
		gpio_set_level(GPIO_SBU_MUX_EN, 1);
		ccd_mode = CCD_MODE_ENABLED;
		CPRINTS("CCD: connected noflip");
	} else {
		/* Measure again after 100 msec */
		hook_call_deferred(&ccd_measure_sbu_data, 100 * MSEC);
	}
}

void ccd_set_mode(enum ccd_mode new_mode)
{
	if (new_mode == CCD_MODE_ENABLED) {
		/* Allow some time following turning on of VBUS */
		hook_call_deferred(&ccd_measure_sbu_data,
				   PD_POWER_SUPPLY_TURN_ON_DELAY);
	} else if (new_mode == CCD_MODE_DISABLED) {
		/* We are not connected to anything */

		/* Disable ccd_measure_sbu deferred call always */
		hook_call_deferred(&ccd_measure_sbu_data, -1);
		/* Turn off CCD */
		gpio_set_level(GPIO_SBU_MUX_EN, 0);
		CPRINTS("CCD: disconnect");
		ccd_mode = CCD_MODE_DISABLED;
	}
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

	/* Delay DUT hub to avoid brownout. */
	usleep(1000);
	gpio_set_flags(GPIO_DUT_HUB_USB_RESET_L, GPIO_OUT_HIGH);

	/* Write USB3 Mode Enable to PS8742 USB/DP Mux. */
	i2c_write8(1, 0x20, 0x0, 0x20);

	/* Enable uservo USB by default. */
	init_ioexpander();
	init_uservo_port();

	/* Initialize CCD mode and disable sbu mux. */
	ccd_set_mode(CCD_MODE_DISABLED);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
