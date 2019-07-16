/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Servo micro board configuration */

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
#include "usb_hw.h"
#include "usb_i2c.h"
#include "usb_spi.h"
#include "usb-stream.h"
#include "util.h"

#include "gpio_list.h"

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= STM32_RCC_SYSCFGEN;

	/*
	 * the DMA mapping is :
	 *  Chan 3 : USART3_RX
	 *  Chan 5 : USART2_RX
	 *  Chan 6 : USART4_RX (Disable)
	 *  Chan 6 : SPI2_RX
	 *  Chan 7 : SPI2_TX
	 *
	 *  i2c : no dma
	 *  tim16/17: no dma
	 */
	STM32_SYSCFG_CFGR1 |= BIT(26);  /* Remap USART3 RX/TX DMA */

	/* Remap SPI2 to DMA channels 6 and 7 */
	/* STM32F072 SPI2 defaults to using DMA channels 4 and 5 */
	/* but cros_ec hardcodes a 6/7 assumption in registers.h */
	STM32_SYSCFG_CFGR1 |= BIT(24);

}

/******************************************************************************
 * Forward UARTs as a USB serial interface.
 */

#define USB_STREAM_RX_SIZE	32
#define USB_STREAM_TX_SIZE	64

/******************************************************************************
 * Forward USART2 (EC) as a simple USB serial interface.
 */

static struct usart_config const usart2;
struct usb_stream_config const usart2_usb;

static struct queue const usart2_to_usb = QUEUE_DIRECT(128, uint8_t,
	usart2.producer, usart2_usb.consumer);
static struct queue const usb_to_usart2 = QUEUE_DIRECT(64, uint8_t,
	usart2_usb.producer, usart2.consumer);

static struct usart_rx_dma const usart2_rx_dma =
	USART_RX_DMA(STM32_DMAC_CH5, 32);

static struct usart_config const usart2 =
	USART_CONFIG(usart2_hw,
		usart2_rx_dma.usart_rx,
		usart_tx_interrupt,
		115200,
		0,
		usart2_to_usb,
		usb_to_usart2);

USB_STREAM_CONFIG_USART_IFACE(usart2_usb,
	USB_IFACE_USART2_STREAM,
	USB_STR_USART2_STREAM_NAME,
	USB_EP_USART2_STREAM,
	USB_STREAM_RX_SIZE,
	USB_STREAM_TX_SIZE,
	usb_to_usart2,
	usart2_to_usb,
	usart2)


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
 *  We cannot enable DMA due to lack of DMA channels.
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
 * Check parity setting on usarts.
 */
static int command_uart_parity(int argc, char **argv)
{
	int parity = 0, newparity;
	struct usart_config const *usart;
	char *e;

	if ((argc < 2) || (argc > 3))
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "usart2"))
		usart = &usart2;
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

	if (!strcasecmp(argv[1], "usart2"))
		usart = &usart2;
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
 * Commands for sending the magic non-I2C handshake over I2C bus wires to an
 * ITE IT8320 EC chip to enable direct firmware update (DFU) over I2C mode.
 */

#define KHz 1000
#define MHz (1000 * KHz)

/*
 * These constants are values that one might want to try changing if
 * enable_ite_dfu stops working, or does not work on a new ITE EC chip revision.
 */

#define ITE_DFU_I2C_CMD_ADDR_FLAGS 0x5A
#define ITE_DFU_I2C_DATA_ADDR_FLAGS 0x35

#define SMCLK_WAVEFORM_PERIOD_HZ (100 * KHz)
#define SMDAT_WAVEFORM_PERIOD_HZ (200 * KHz)

#define START_DELAY_MS 5
#define SPECIAL_WAVEFORM_MS 50
#define PLL_STABLE_MS 10

/*
 * Digital line levels to hold before (PRE_) or after (POST_) sending the
 * special waveforms.  0 for low, 1 for high.
 */
#define SMCLK_PRE_LEVEL 0
#define SMDAT_PRE_LEVEL 0
#define SMCLK_POST_LEVEL 0
#define SMDAT_POST_LEVEL 0

/* The caller should hold the i2c_lock() for I2C_PORT_MASTER. */
static int ite_i2c_read_register(uint8_t register_offset, uint8_t *output)
{
	/*
	 * Ideally the write and read would be done in one I2C transaction, as
	 * is normally done when reading from the same I2C address that the
	 * write was sent to.  The ITE EC is abnormal in that regard, with its
	 * different addresses for writes vs reads.
	 *
	 * i2c_xfer() does not support that.  Its I2C_XFER_START and
	 * I2C_XFER_STOP flag bits do not cleanly support that scenario, they
	 * are for continuing transfers without either of STOP or START
	 * in-between.
	 *
	 * For what it's worth, the iteflash.c FTDI-based implementation of this
	 * does the same thing, issuing a STOP between the write and read.  This
	 * works, even if perhaps it should not.
	 */
	int ret;
	/* Tell the ITE EC which register we want to read. */
	ret = i2c_xfer_unlocked(I2C_PORT_MASTER,
				ITE_DFU_I2C_CMD_ADDR_FLAGS,
				&register_offset, sizeof(register_offset),
				NULL, 0, I2C_XFER_SINGLE);
	if (ret != EC_SUCCESS)
		return ret;
	/* Read in the 1 byte register value. */
	ret = i2c_xfer_unlocked(I2C_PORT_MASTER,
				ITE_DFU_I2C_DATA_ADDR_FLAGS,
				NULL, 0,
				output, sizeof(*output), I2C_XFER_SINGLE);
	return ret;
}

/* Helper function to read ITE chip ID, for verifying ITE DFU mode. */
static int cprint_ite_chip_id(void)
{
	/*
	 * Per i2c_read8() implementation, use an array even for single byte
	 * reads to ensure alignment for DMA on STM32.
	 */
	uint8_t chipid1[1];
	uint8_t chipid2[1];
	uint8_t chipver[1];

	int ret;
	int chip_version;
	int flash_kb;

	i2c_lock(I2C_PORT_MASTER, 1);

	/* Read the CHIPID1 register. */
	ret = ite_i2c_read_register(0x00, chipid1);
	if (ret != EC_SUCCESS)
		goto unlock;

	/* Read the CHIPID2 register. */
	ret = ite_i2c_read_register(0x01, chipid2);
	if (ret != EC_SUCCESS)
		goto unlock;

	/* Read the CHIPVER register. */
	ret = ite_i2c_read_register(0x02, chipver);

unlock:
	i2c_lock(I2C_PORT_MASTER, 0);
	if (ret != EC_SUCCESS)
		return ret;

	/*
	 * Compute chip version and embedded flash size from the CHIPVER value.
	 *
	 * Chip version is mapping from bit 3-0
	 * Flash size is mapping from bit 7-4
	 *
	 * Chip Version (bits 3-0)
	 * 0: AX
	 * 1: BX
	 * 2: CX
	 * 3: DX
	 *
	 * CX or prior flash size (bits 7-4)
	 * 0:128KB
	 * 4:192KB
	 * 8:256KB
	 *
	 * DX flash size (bits 7-4)
	 * 0:128KB
	 * 2:192KB
	 * 4:256KB
	 * 6:384KB
	 * 8:512KB
	 */
	chip_version = chipver[0] & 0x07;
	if (chip_version < 0x3) {
		/* Chip version is CX or earlier. */
		switch (chipver[0] >> 4) {
		case 0:
			flash_kb = 128;
			break;
		case 4:
			flash_kb = 192;
			break;
		case 8:
			flash_kb = 256;
			break;
		default:
			flash_kb = -2;
		}
	} else if (chip_version == 0x3) {
		/* Chip version is DX. */
		switch (chipver[0] >> 4) {
		case 0:
			flash_kb = 128;
			break;
		case 2:
			flash_kb = 192;
			break;
		case 4:
			flash_kb = 256;
			break;
		case 6:
			flash_kb = 384;
			break;
		case 8:
			flash_kb = 512;
			break;
		default:
			flash_kb = -3;
		}
	} else {
		/* Unrecognized chip version. */
		flash_kb = -1;
	}

	ccprintf("ITE EC info: CHIPID1=0x%02X CHIPID2=0x%02X CHIPVER=0x%02X ",
		chipid1[0], chipid2[0], chipver[0]);
	ccprintf("version=%d flash_bytes=%d\n", chip_version, flash_kb << 10);

	/*
	 * IT8320_eflash_SMBus_Programming_Guide.pdf says it is an error if
	 * CHIPID1 != 0x83.
	 */
	if (chipid1[0] != 0x83)
		ret = EC_ERROR_HW_INTERNAL;

	return ret;
}

/* Enable ITE direct firmware update (DFU) mode. */
static int command_enable_ite_dfu(int argc, char **argv)
{
	if (argc > 1)
		return EC_ERROR_PARAM_COUNT;

	/* Enable peripheral clocks. */
	STM32_RCC_APB2ENR |=
		STM32_RCC_APB2ENR_TIM16EN | STM32_RCC_APB2ENR_TIM17EN;

	/* Reset timer registers which are not otherwise set below. */
	STM32_TIM_CR2(16) = 0x0000;
	STM32_TIM_CR2(17) = 0x0000;
	STM32_TIM_DIER(16) = 0x0000;
	STM32_TIM_DIER(17) = 0x0000;
	STM32_TIM_SR(16) = 0x0000;
	STM32_TIM_SR(17) = 0x0000;
	STM32_TIM_CNT(16) = 0x0000;
	STM32_TIM_CNT(17) = 0x0000;
	STM32_TIM_RCR(16) = 0x0000;
	STM32_TIM_RCR(17) = 0x0000;
	STM32_TIM_DCR(16) = 0x0000;
	STM32_TIM_DCR(17) = 0x0000;
	STM32_TIM_DMAR(16) = 0x0000;
	STM32_TIM_DMAR(17) = 0x0000;

	/* Prescale to 1 MHz and use ARR to achieve NNN KHz periods. */
	/* This approach is seen in STM's documentation. */
	STM32_TIM_PSC(16) = (CPU_CLOCK / MHz) - 1;
	STM32_TIM_PSC(17) = (CPU_CLOCK / MHz) - 1;

	/* Set the waveform periods based on 1 MHz prescale. */
	STM32_TIM_ARR(16) = (MHz / SMCLK_WAVEFORM_PERIOD_HZ) - 1;
	STM32_TIM_ARR(17) = (MHz / SMDAT_WAVEFORM_PERIOD_HZ) - 1;

	/* Set output compare 1 mode to PWM mode 1 and enable preload. */
	STM32_TIM_CCMR1(16) =
		STM32_TIM_CCMR1_OC1M_PWM_MODE_1 | STM32_TIM_CCMR1_OC1PE;
	STM32_TIM_CCMR1(17) =
		STM32_TIM_CCMR1_OC1M_PWM_MODE_1 | STM32_TIM_CCMR1_OC1PE;

	/* Enable output compare 1. */
	STM32_TIM_CCER(16) = STM32_TIM_CCER_CC1E;
	STM32_TIM_CCER(17) = STM32_TIM_CCER_CC1E;

	/* Enable main output. */
	STM32_TIM_BDTR(16) = STM32_TIM_BDTR_MOE;
	STM32_TIM_BDTR(17) = STM32_TIM_BDTR_MOE;

	/* Update generation (reinitialize counters). */
	STM32_TIM_EGR(16) = STM32_TIM_EGR_UG;
	STM32_TIM_EGR(17) = STM32_TIM_EGR_UG;

	/* Set duty cycle to 0% or 100%, pinning each channel low or high. */
	STM32_TIM_CCR1(16) = SMCLK_PRE_LEVEL ? 0xFFFF : 0x0000;
	STM32_TIM_CCR1(17) = SMDAT_PRE_LEVEL ? 0xFFFF : 0x0000;

	/* Enable timer counters. */
	STM32_TIM_CR1(16) = STM32_TIM_CR1_CEN;
	STM32_TIM_CR1(17) = STM32_TIM_CR1_CEN;

	/* Set PB8 GPIO to alternate mode TIM16_CH1. */
	/* Set PB9 GPIO to alternate mode TIM17_CH1. */
	gpio_config_module(MODULE_I2C_TIMERS, 1);

	msleep(START_DELAY_MS);

	/* Set pulse width to half of waveform period. */
	STM32_TIM_CCR1(16) = (MHz / SMCLK_WAVEFORM_PERIOD_HZ) / 2;
	STM32_TIM_CCR1(17) = (MHz / SMDAT_WAVEFORM_PERIOD_HZ) / 2;

	msleep(SPECIAL_WAVEFORM_MS);

	/* Set duty cycle to 0% or 100%, pinning each channel low or high. */
	STM32_TIM_CCR1(16) = SMCLK_POST_LEVEL ? 0xFFFF : 0x0000;
	STM32_TIM_CCR1(17) = SMDAT_POST_LEVEL ? 0xFFFF : 0x0000;

	msleep(PLL_STABLE_MS);

	/* Set PB8 GPIO to alternate mode I2C1_SCL. */
	/* Set PB9 GPIO to alternate mode I2C1_DAT. */
	gpio_config_module(MODULE_I2C, 1);

	/* Disable timer counters. */
	STM32_TIM_CR1(16) = 0x0000;
	STM32_TIM_CR1(17) = 0x0000;

	/* Disable peripheral clocks. */
	STM32_RCC_APB2ENR &=
		~(STM32_RCC_APB2ENR_TIM16EN | STM32_RCC_APB2ENR_TIM17EN);

	return cprint_ite_chip_id();
}
DECLARE_CONSOLE_COMMAND(
	enable_ite_dfu, command_enable_ite_dfu, "",
	"Enable ITE Direct Firmware Update (DFU) mode");

/* Read ITE chip ID.  Can be used to verify ITE DFU mode. */
/*
 * TODO(b/79684405): There is nothing specific about Servo Micro in the
 * implementation of the "get_ite_chipid" command.  Move the implementation to a
 * common place so that it need not be reimplemented for every Servo version
 * that "enable_ite_dfu" is implemented for.
 */
static int command_get_ite_chipid(int argc, char **argv)
{
	if (argc > 1)
		return EC_ERROR_PARAM_COUNT;

	return cprint_ite_chip_id();
}
DECLARE_CONSOLE_COMMAND(
	get_ite_chipid, command_get_ite_chipid, "",
	"Read ITE EC chip ID, version, flash size (must be in DFU mode)");

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */
const void *const usb_strings[] = {
	[USB_STR_DESC]         = usb_string_desc,
	[USB_STR_VENDOR]       = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT]      = USB_STRING_DESC("Servo Micro"),
	[USB_STR_SERIALNO]     = 0,
	[USB_STR_VERSION]      = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_I2C_NAME]     = USB_STRING_DESC("I2C"),
	[USB_STR_USART4_STREAM_NAME]  = USB_STRING_DESC("UART3"),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Servo Shell"),
	[USB_STR_USART3_STREAM_NAME]  = USB_STRING_DESC("CPU"),
	[USB_STR_USART2_STREAM_NAME]  = USB_STRING_DESC("EC"),
	[USB_STR_UPDATE_NAME]  = USB_STRING_DESC("Firmware update"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/******************************************************************************
 * Support SPI bridging over USB, this requires usb_spi_board_enable and
 * usb_spi_board_disable to be defined to enable and disable the SPI bridge.
 */

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_FLASH_PORT, 1, GPIO_SPI_CS},
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
}

USB_SPI_CONFIG(usb_spi, USB_IFACE_SPI, USB_EP_SPI);

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
static void board_init(void)
{
	/* USB to serial queues */
	queue_init(&usart2_to_usb);
	queue_init(&usb_to_usart2);
	queue_init(&usart3_to_usb);
	queue_init(&usb_to_usart3);
	queue_init(&usart4_to_usb);
	queue_init(&usb_to_usart4);

	/* UART init */
	usart_init(&usart2);
	usart_init(&usart3);
	usart_init(&usart4);

	/* Enable GPIO expander. */
	gpio_set_level(GPIO_TCA6416_RESET_L, 1);

	/* Structured enpoints */
	usb_spi_enable(&usb_spi, 1);

	/* Enable UARTs by default. */
	gpio_set_level(GPIO_UART1_EN_L, 0);
	gpio_set_level(GPIO_UART2_EN_L, 0);
	/* Disable power output. */
	gpio_set_level(GPIO_SPI1_VREF_18, 0);
	gpio_set_level(GPIO_SPI1_VREF_33, 0);
	gpio_set_level(GPIO_SPI2_VREF_18, 0);
	gpio_set_level(GPIO_SPI2_VREF_33, 0);
	/* Enable UART3 routing. */
	gpio_set_level(GPIO_SPI1_MUX_SEL, 1);
	gpio_set_level(GPIO_SPI1_BUF_EN_L, 1);
	gpio_set_level(GPIO_JTAG_BUFIN_EN_L, 0);
	gpio_set_level(GPIO_SERVO_JTAG_TDO_BUFFER_EN, 1);
	gpio_set_level(GPIO_SERVO_JTAG_TDO_SEL, 1);
	gpio_set_flags(GPIO_UART3_RX_JTAG_BUFFER_TO_SERVO_TDO, GPIO_ALTERNATE);
	gpio_set_flags(GPIO_UART3_TX_SERVO_JTAG_TCK, GPIO_ALTERNATE);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
