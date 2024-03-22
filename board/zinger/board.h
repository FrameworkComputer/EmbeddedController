/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Tiny charger configuration. This config is used for multiple boards
 * including zinger and minimuffin.
 */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

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
#undef CONFIG_FLASH_CROS
#undef CONFIG_FLASH_PHYSICAL
#undef CONFIG_FMAP
/* Not using pstate but keep some space for the public key */
#undef CONFIG_FW_PSTATE_SIZE
#define CONFIG_FW_PSTATE_SIZE 544
#define CONFIG_HIBERNATE
#define CONFIG_HIBERNATE_WAKEUP_PINS STM32_PWR_CSR_EWUP1
#define CONFIG_HW_CRC
#undef CONFIG_LID_SWITCH
#define CONFIG_LTO
#define CONFIG_RSA
#define CONFIG_RWSIG_TYPE_USBPD1
#define CONFIG_SHA256_SW
#undef CONFIG_TASK_PROFILING
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_TCPMV1
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_CUSTOM_PDO
#undef CONFIG_USB_PD_DUAL_ROLE
#undef CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USB_PD_LOGGING
#undef CONFIG_EVENT_LOG_SIZE
#define CONFIG_EVENT_LOG_SIZE 256
#define CONFIG_USB_PD_LOW_POWER_IDLE_WHEN_CONNECTED
#define CONFIG_USB_PD_PORT_MAX_COUNT 1
#define CONFIG_USB_PD_TCPC
#define CONFIG_USB_PD_TCPM_STUB
#undef CONFIG_USB_PD_RX_COMP_IRQ
#define CONFIG_USB_PD_SIMPLE_DFP
#define CONFIG_USB_PD_VBUS_DETECT_GPIO
#define CONFIG_USBC_BACKWARDS_COMPATIBLE_DFP
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_WATCHDOG_PERIOD_MS
#define CONFIG_WATCHDOG_PERIOD_MS 2300

/* debug printf flash footprint is about 1400 bytes */
#undef CONFIG_DEBUG_PRINTF
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
#include "gpio_signal.h"

/* No GPIO abstraction layer */

enum adc_channel {
	ADC_CH_CC1_PD = 1,
	ADC_CH_A_SENSE = 2,
	ADC_CH_V_SENSE = 3,
	/* Number of ADC channels */
	ADC_CH_COUNT
};
/* captive cable : no CC2 */
#define ADC_CH_CC2_PD ADC_CH_CC1_PD

/* 3.0A Rp */
#define PD_SRC_VNC \
	(PD_SRC_3_0_VNC_MV * 4096 / 3300 /* 12-bit ADC, 3.3V range */)

/* delay necessary for the voltage transition on the power supply */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 50000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 50000 /* us */

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

void __enter_hibernate(uint32_t seconds, uint32_t microseconds);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
