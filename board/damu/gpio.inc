/* -*- mode:c -*-
 *
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first.
 */

/* Interrupts */
GPIO_INT(USB_C0_PD_INT_ODL, PIN(B, 1), GPIO_INT_FALLING | GPIO_PULL_UP,
         tcpc_alert_event)
GPIO_INT(POWER_BUTTON_L,    PIN(A, 0),  GPIO_INT_BOTH | GPIO_PULL_UP,
         power_button_interrupt) /* EC_PWR_BTN_ODL */
GPIO_INT(AP_IN_SLEEP_L,     PIN(C, 12), GPIO_INT_BOTH | GPIO_PULL_DOWN,
         power_signal_interrupt)
GPIO_INT(PMIC_EC_RESETB,    PIN(B, 7),  GPIO_INT_BOTH | GPIO_PULL_DOWN,
         power_signal_interrupt)
GPIO_INT(WARM_RESET_REQ,    PIN(A, 3),  GPIO_INT_RISING | GPIO_PULL_DOWN,
         chipset_reset_request_interrupt)
GPIO_INT_RW(ACCEL_INT_ODL,  PIN(A, 4), GPIO_INT_FALLING | GPIO_SEL_1P8V | GPIO_PULL_UP,
         bmi160_interrupt)
GPIO_INT_RO(EMMC_CMD,       PIN(B, 15), GPIO_INT_FALLING,
         emmc_cmd_interrupt)
GPIO_INT(SPI1_NSS,          PIN(A, 15), GPIO_INT_BOTH | GPIO_PULL_UP,
         spi_event) /* SPI_AP_EC_CS_L */
GPIO_INT(LID_OPEN,          PIN(C, 5),  GPIO_INT_BOTH,
         lid_interrupt)
GPIO_INT(AC_PRESENT,        PIN(A, 6),  GPIO_INT_BOTH,
         extpower_interrupt) /* ACOK_OD */
GPIO_INT(BC12_EC_INT_ODL,   PIN(C, 9),  GPIO_INT_FALLING,
         bc12_interrupt)
GPIO_INT(IT8801_SMB_INT,    PIN(A, 8),  GPIO_INT_FALLING | GPIO_PULL_UP,
         io_expander_it8801_interrupt) /* KB_INT_ODL */
GPIO_INT(AP_EC_WATCHDOG_L,  PIN(D, 2),  GPIO_INT_FALLING,
         chipset_watchdog_interrupt)
GPIO_INT(TABLET_MODE_L,     PIN(B, 11), GPIO_INT_BOTH,
         gmr_tablet_switch_isr)

/* Unimplemented interrupts */
GPIO(ALS_RGB_INT_ODL,      PIN(C, 10), GPIO_INPUT)

/* Reset pins */
GPIO(AP_SYS_RST_L,         PIN(C, 11), GPIO_OUT_LOW)
GPIO(PMIC_WATCHDOG_L,      PIN(A, 2),  GPIO_OUT_LOW)
GPIO(PMIC_EN_ODL,          PIN(F, 0),  GPIO_ODR_HIGH)

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C1_SCL,             PIN(B, 8),  GPIO_INPUT)
GPIO(I2C1_SDA,             PIN(B, 9),  GPIO_INPUT)
GPIO(I2C2_SCL,             PIN(A, 11), GPIO_INPUT)
GPIO(I2C2_SDA,             PIN(A, 12), GPIO_INPUT)
GPIO(I2C3_SCL,             PIN(A, 5),  GPIO_ODR_HIGH)
GPIO(I2C3_SDA,             PIN(C, 4),  GPIO_ODR_HIGH)

/* Analog pins */
GPIO(BOARD_ID,             PIN(C, 0),  GPIO_ANALOG)
GPIO(EC_SKU_ID,            PIN(B, 0),  GPIO_ANALOG)

/* Other input pins */
GPIO(WP_L,                 PIN(C, 8),  GPIO_INPUT) /* EC_FLASH_WP_ODL */
GPIO(BOOT0,                PIN(F, 11), GPIO_INPUT)
GPIO(CCD_MODE_ODL,         PIN(A, 1),  GPIO_INPUT)

/* Other output pins */
GPIO(EC_BATT_PRES_ODL,     PIN(A, 7),  GPIO_INPUT)
GPIO(EC_BL_EN_OD,          PIN(A, 13), GPIO_ODR_HIGH)
GPIO(EN_USBA_5V,           PIN(C, 14), GPIO_OUT_LOW)
GPIO(EC_SENSOR_SPI_MISO,   PIN(C, 2),  GPIO_INPUT)
GPIO(EC_SENSOR_SPI_MOSI,   PIN(C, 3),  GPIO_OUT_LOW)
GPIO(EC_SENSOR_SPI_NSS,    PIN(B, 12), GPIO_OUT_HIGH)
GPIO(EC_SENSOR_SPI_CK,     PIN(B, 10), GPIO_OUT_LOW)
GPIO(ENTERING_RW,          PIN(C, 6),  GPIO_ODR_HIGH) /* EC_ENTERING_RW_ODL */
GPIO(EC_INT_L,             PIN(C, 7),  GPIO_ODR_HIGH) /* EC_AP_INT_ODL */
GPIO(EC_BOARD_ID_EN_L,     PIN(C, 15), GPIO_ODR_HIGH) /* EC_BOARD_ID_EN_ODL */
GPIO(USB_C0_HPD_OD,        PIN(F, 1),  GPIO_ODR_LOW)
GPIO(BOOTBLOCK_EN_L,       PIN(C, 1),  GPIO_ODR_HIGH)
GPIO(EN_PP1800_S5_L,       PIN(A, 14), GPIO_OUT_LOW)
GPIO(USB_C0_DISCHARGE,     PIN(B, 6),  GPIO_OUT_LOW)

IOEX(PWR_LED_WHITE_L,      EXPIN(0, 1, 4), GPIO_OUT_HIGH)
IOEX(BAT_LED_WHITE_L,      EXPIN(0, 1, 3), GPIO_OUT_HIGH)
IOEX(BAT_LED_AMBER_L,      EXPIN(0, 1, 2), GPIO_OUT_HIGH)
/*
 * TODO(b:138352732): On IT88801 expander, To be readded once IT8801 driver and
 * gpio expander framework has landed.
 */
UNIMPLEMENTED(EN_PP5000_USBC)
UNIMPLEMENTED(PMIC_FORCE_RESET_ODL)
UNIMPLEMENTED(EN_USBC_CHARGE_L)

/* USART1: PA9/PA10 */
ALTERNATE(PIN_MASK(A, 0x0600), 1, MODULE_UART,       0)
/* I2C MASTER: PB8/9 */
ALTERNATE(PIN_MASK(B, 0x0300), 1, MODULE_I2C,        GPIO_ODR_HIGH )
/* I2C MASTER: PA11/12 */
ALTERNATE(PIN_MASK(A, 0x1800), 5, MODULE_I2C,        GPIO_ODR_HIGH )
/* SPI SLAVE: PB3/4/5 */
ALTERNATE(PIN_MASK(B, 0x0038), 0, MODULE_SPI,        0)
/* SPI SLAVE CS: PA15 */
ALTERNATE(PIN_MASK(A, 0x8000), 0, MODULE_SPI,        0)

/* Shared between slave for emmc and master for bmi160 */
/* They're mutually exclusive with gpio_config_module. */
/* EMMC SPI SLAVE: PB13/14/15 */
ALTERNATE(PIN_MASK(B, 0xE000), 0, MODULE_SPI_FLASH,  0)
/* SENSORS SPI MASTER: PB10, PB12, PC2, PC3 */
ALTERNATE(PIN_MASK(B, 0x0400), 5, MODULE_SPI_MASTER, 0)
ALTERNATE(PIN_MASK(C, 0x000C), 1, MODULE_SPI_MASTER, 0)
