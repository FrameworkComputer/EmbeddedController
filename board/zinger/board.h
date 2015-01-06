/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Tiny charger configuration. This config is used for multiple boards
 * including zinger and minimuffin.
 */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART1 (PA9/PA10) */
#define CONFIG_UART_CONSOLE 1

#ifdef BOARD_ZINGER
#define CONFIG_USB_PD_HW_DEV_ID_BOARD_MAJOR USB_PD_HW_DEV_ID_ZINGER
#define CONFIG_USB_PD_HW_DEV_ID_BOARD_MINOR 1
#elif defined(BOARD_MINIMUFFIN)
#define CONFIG_USB_PD_HW_DEV_ID_BOARD_MAJOR USB_PD_HW_DEV_ID_MINIMUFFIN
#define CONFIG_USB_PD_HW_DEV_ID_BOARD_MINOR 0
#else
#error "Board does not have a USB-PD HW Device ID"
#endif

/* Optional features */
#undef CONFIG_COMMON_GPIO
#undef CONFIG_COMMON_PANIC_OUTPUT
#undef CONFIG_COMMON_RUNTIME
#undef CONFIG_COMMON_TIMER
#define CONFIG_LOW_POWER_IDLE
#undef CONFIG_CONSOLE_CMDHELP
#undef CONFIG_DEBUG_ASSERT
#undef CONFIG_DEBUG_EXCEPTIONS
#undef CONFIG_DEBUG_STACK_OVERFLOW
#undef CONFIG_FLASH
#undef CONFIG_FMAP
#define CONFIG_HIBERNATE
#define CONFIG_HIBERNATE_WAKEUP_PINS STM32_PWR_CSR_EWUP1
#define CONFIG_HW_CRC
#undef CONFIG_LID_SWITCH
#define CONFIG_RSA
#define CONFIG_SHA256
#undef CONFIG_TASK_PROFILING
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_CUSTOM_VDM
#undef CONFIG_USB_PD_DUAL_ROLE
#undef CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_LOG_SIZE 256
#undef CONFIG_USB_PD_RX_COMP_IRQ
#define CONFIG_USB_PD_SIMPLE_DFP
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_WATCHDOG_PERIOD_MS
#define CONFIG_WATCHDOG_PERIOD_MS 2300

/* debug printf flash footprinf is about 1400 bytes */
#define CONFIG_DEBUG_PRINTF
#define UARTN CONFIG_UART_CONSOLE
#define UARTN_BASE STM32_USART_BASE(CONFIG_UART_CONSOLE)

/* USB configuration */
#if defined(BOARD_ZINGER)
#define CONFIG_USB_PID 0x5012
#define CONFIG_USB_BCD_DEV 0x0001 /* v 0.01 */
#elif defined(BOARD_MINIMUFFIN)
#define CONFIG_USB_PID 0x5013
#define CONFIG_USB_BCD_DEV 0x0001 /* v 0.01 */
#endif

#ifndef __ASSEMBLER__

#include "common.h"

/* No GPIO abstraction layer */
enum gpio_signal;

enum adc_channel {
	ADC_CH_CC1_PD = 1,
	ADC_CH_A_SENSE = 2,
	ADC_CH_V_SENSE = 3,
	/* Number of ADC channels */
	ADC_CH_COUNT
};
/* captive cable : no CC2 */
#define ADC_CH_CC2_PD ADC_CH_CC1_PD

/* Initialize all useful registers */
void hardware_init(void);

/* last interrupt event */
extern volatile uint32_t last_event;

/* RW section flashing */
int flash_erase_rw(void);
int flash_write_rw(int offset, int size, const char *data);
void flash_physical_permanent_protect(void);
int flash_physical_is_permanently_protected(void);
uint8_t *flash_hash_rw(void);
int is_ro_mode(void);

/* RTC functions */
void rtc_init(void);
void set_rtc_alarm(uint32_t delay_s, uint32_t delay_us,
		   uint32_t *rtc, uint32_t *rtcss);
void reset_rtc_alarm(uint32_t *rtc, uint32_t *rtcss);
int32_t get_rtc_diff(uint32_t rtc0, uint32_t rtc0ss,
		     uint32_t rtc1, uint32_t rtc1ss);
void __enter_hibernate(uint32_t seconds, uint32_t microseconds);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
