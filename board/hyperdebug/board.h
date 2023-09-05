/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* HyperDebug configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define CONFIG_LTO

/* Disable deferred (async) flash protect*/
#undef CONFIG_FLASH_PROTECT_DEFERRED

/* Configure the flash */
#undef CONFIG_RO_SIZE
#undef CONFIG_FW_PSTATE_OFF
#undef CONFIG_FW_PSTATE_SIZE
#undef CONFIG_RW_MEM_OFF
#undef CONFIG_RW_SIZE

#define CONFIG_RO_SIZE (4 * 1024)
#define CONFIG_RW_MEM_OFF (CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE)
#define CONFIG_RW_SIZE (CONFIG_FLASH_SIZE_BYTES - CONFIG_RW_MEM_OFF)
#undef CONFIG_IMAGE_PADDING

#ifdef SECTION_IS_RO

/* Configure the Boot Manager. */
#define CONFIG_MALLOC
#define CONFIG_DFU_BOOTMANAGER_MAIN
#define CONFIG_DFU_BOOTMANAGER_SHARED
#undef CONFIG_COMMON_RUNTIME
#undef CONFIG_COMMON_PANIC_OUTPUT
#undef CONFIG_COMMON_GPIO
#undef CONFIG_COMMON_TIMER
#undef CONFIG_WATCHDOG

#else /* !SECTION_IS_RO */

/*
 * PLL configuration. Freq = STM32_HSE_CLOCK or HSI (16MHz) * N / M / R.
 *
 * In our case, 16MHz * 13 / 1 / 2 = 104MHz.
 */

#undef STM32_PLLM
#undef STM32_PLLN
#undef STM32_PLLR
#define STM32_PLLM 1
#define STM32_PLLN 13
#define STM32_PLLR 2

#define STM32_USE_PLL
#define CPU_CLOCK 104000000

#define CONFIG_ADC
#define CONFIG_ADC_SAMPLE_TIME STM32_ADC_SMPR_247_5_CY
#undef CONFIG_ADC_WATCHDOG
#define CONFIG_BOARD_PRE_INIT

#define CONFIG_ROM_BASE 0x0
#define CONFIG_ROM_SIZE (CONFIG_RAM_BASE - CONFIG_ROM_BASE)

/* DFU Firmware Update */
#define CONFIG_DFU_RUNTIME
#define CONFIG_DFU_BOOTMANAGER_SHARED

/* Enable USB forwarding on UART 2, 3, 4, and 5. */
#define CONFIG_STREAM_USART
#undef CONFIG_STREAM_USART1
#define CONFIG_STREAM_USART2
#define CONFIG_STREAM_USART3
#define CONFIG_STREAM_USART4
#define CONFIG_STREAM_USART5
#undef CONFIG_STREAM_USART9
#define CONFIG_STREAM_USB
#define CONFIG_CMD_USART_INFO

/* The UART console is on LPUART (UART9), connected to st-link debugger. */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 9
#undef CONFIG_UART_TX_DMA
#undef CONFIG_UART_RX_DMA

/* Optional features */
#define CONFIG_HW_CRC
#undef CONFIG_PVD

/*
 * See 'Programmable voltage detector characteristics' in the
 * STM32F072x8 Datasheet.  PVD Threshold 1 corresponds to a falling
 * voltage threshold of min:2.09V, max:2.27V.
 */
#define PVD_THRESHOLD (1)

/* USB Configuration */

#define CONFIG_USB
#define CONFIG_USB_PID 0x520e
#define CONFIG_USB_CONSOLE

/*
 * Enabling USB updating would exceed the number of USB endpoints
 * supported by the hardware.  We will have to rely on the built-in
 * DFU support of STM32 chips.
 */
#undef CONFIG_USB_UPDATE

#undef CONFIG_USB_MAXPOWER_MA
#define CONFIG_USB_MAXPOWER_MA 100

#define CONFIG_USB_SERIALNO
#define DEFAULT_SERIALNO "Uninitialized"

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_CONSOLE 0
#define USB_IFACE_SPI 1
#define USB_IFACE_CMSIS_DAP 2
#define USB_IFACE_USART2_STREAM 3
#define USB_IFACE_USART3_STREAM 4
#define USB_IFACE_USART4_STREAM 5
#define USB_IFACE_USART5_STREAM 6
#define USB_IFACE_DFU 7
#define USB_IFACE_COUNT 8

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL 0
#define USB_EP_CONSOLE 1
#define USB_EP_SPI 2
#define USB_EP_CMSIS_DAP 3
#define USB_EP_USART2_STREAM 4
#define USB_EP_USART3_STREAM 5
#define USB_EP_USART4_STREAM 6
#define USB_EP_USART5_STREAM 7
#define USB_EP_COUNT 8

/*
 * Do not enable the common EC command gpioset for recasting of GPIO
 * type. Instead, board specific commands are used for implementing
 * the OpenTitan tool requirements.
 */
#undef CONFIG_CMD_GPIO_EXTENDED
#define CONFIG_GPIO_GET_EXTENDED

/* Enable control of SPI over USB */
#define CONFIG_USB_SPI
#define CONFIG_USB_SPI_BUFFER_SIZE 2048
#define CONFIG_USB_SPI_FLASH_EXTENSIONS
#define CONFIG_SPI_CONTROLLER
#define CONFIG_STM32_SPI1_CONTROLLER
#define CONFIG_SPI_MUTABLE_DEVICE_LIST

/*
 * Control of I2C over USB happens via board-specific CMSIS-DAP protocol. Do
 * not enable common EC code for USB forwarding, but do enable low level I2C
 * support for use by board-specific forwarding code.
 */
#undef CONFIG_USB_I2C
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER

/* See i2c_ite_flash_support.c for more information about these values */
/*#define CONFIG_ITE_FLASH_SUPPORT */
/*#define CONFIG_I2C_XFER_LARGE_TRANSFER */
#undef CONFIG_USB_I2C_MAX_WRITE_COUNT
#undef CONFIG_USB_I2C_MAX_READ_COUNT
#define CONFIG_USB_I2C_MAX_WRITE_COUNT ((1 << 9) - 4)
#define CONFIG_USB_I2C_MAX_READ_COUNT ((1 << 9) - 6)

#endif /* SECTION_IS_RO */

/* This is not actually an EC so disable some features. */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_FLASH_PSTATE

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2

#include "gpio_signal.h"

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_SERIALNO,
	USB_STR_VERSION,
	USB_STR_CONSOLE_NAME,
	USB_STR_SPI_NAME,
	USB_STR_CMSIS_DAP_NAME,
	USB_STR_USART2_STREAM_NAME,
	USB_STR_USART3_STREAM_NAME,
	USB_STR_USART4_STREAM_NAME,
	USB_STR_USART5_STREAM_NAME,
	USB_STR_DFU_NAME,

	USB_STR_COUNT
};

/* ADC signal */
enum adc_channel {
	ADC_CN9_11, /* ADC12_IN1 */
	ADC_CN9_9, /* ADC12_IN2 */
	/* ADC_CN10_9, */ /* ADC12_IN3, Nucleo USB VBUS sense */
	ADC_CN9_5, /* ADC12_IN4 */
	ADC_CN10_29, /* ADC12_IN5 */
	ADC_CN10_11, /* ADC12_IN6 */
	ADC_CN9_3, /* ADC12_IN7 */
	ADC_CN9_1, /* ADC12_IN8 */
	ADC_CN7_9, /* ADC12_IN9 */
	ADC_CN7_10, /* ADC12_IN10 */
	ADC_CN7_12, /* ADC12_IN11 */
	ADC_CN7_14, /* ADC12_IN12 */
	/* PC4, not on connectors */ /* ADC12_IN13 */
	/* PC5, not on connectors */ /* ADC12_IN14 */
	ADC_CN9_7, /* ADC12_IN15 */
	ADC_CN10_7, /* ADC12_IN16 */
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* Timeout for initializing the OctoSPI controller. */
#define OCTOSPI_INIT_TIMEOUT_US (100 * MSEC)

/*
 * Timeout for a complete SPI transaction.  Users can potentially set the clock
 * down to 62.5 kHz and transfer up to 2048 bytes, which would take 262ms
 * assuming no FIFO stalls.
 */
#define OCTOSPI_TRANSACTION_TIMEOUT_US (500 * MSEC)

/*
 * Several modules want to be able to re-initialize to go back to power-on
 * default settings, as part of "opentitantool transport init".  It is
 * convenient for each module to be able to register a hook, rather than a
 * central location to have to know about each of them.  Since HyperDebug does
 * not control any ChromeOS AP, we can "retrofit" the HOOK_CHIPSET_RESET for
 * this purpose without ill effect.
 */
#define HOOK_REINIT HOOK_CHIPSET_RESET

/* Interrupt handler, called by common/gpio.c. */
void gpio_edge(enum gpio_signal signal);
void user_button_edge(enum gpio_signal signal);

/* Utility method */
enum gpio_signal gpio_find_by_name(const char *name);

extern int shield_reset_pin;

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
