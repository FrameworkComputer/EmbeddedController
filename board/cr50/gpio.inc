/* -*- mode:c -*-
 * Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/*
 * We can assert SYS_RST_L but so can the EC, so we need react if it's pulled
 * low. The ARM core can't trigger an interrupt if it's driving it as an output
 * so we attach two internal GPIOs to the same pad.
 * TODO: Remove this internal pullup at some point. It's only here so that
 * boards that don't have an external pullup don't trigger due to noise.
 */
GPIO_INT(SYS_RST_L_IN,    PIN(1, 0),  GPIO_INT_FALLING | GPIO_PULL_UP,
	sys_rst_asserted)
GPIO_INT(AP_ON,           PIN(1, 1),  GPIO_INT_RISING, device_state_on)
GPIO_INT(EC_ON,           PIN(1, 2),  GPIO_INT_RISING, device_state_on)
GPIO_INT(SERVO_UART1_ON,  PIN(1, 3),  GPIO_INT_RISING | GPIO_PULL_DOWN,
	device_state_on)
GPIO_INT(SERVO_UART2_ON,  PIN(1, 4),  GPIO_INT_RISING | GPIO_PULL_DOWN,
	device_state_on)
GPIO_INT(AP_OFF,          PIN(1, 5),  GPIO_INT_FALLING,  device_state_off)
GPIO_INT(EC_OFF,          PIN(1, 6),  GPIO_INT_FALLING,  device_state_off)
GPIO_INT(SERVO_UART1_OFF, PIN(1, 7),  GPIO_INT_FALLING | GPIO_PULL_DOWN,
	device_state_off)
GPIO_INT(SERVO_UART2_OFF, PIN(1, 8),  GPIO_INT_FALLING | GPIO_PULL_DOWN,
	device_state_off)

/* Pull this low to interrupt the AP */
GPIO(INT_AP_L,        PIN(0, 0), GPIO_INPUT)

/* Use these to take over the AP & EC flash (only when AP & EC are off!) */
GPIO(EC_FLASH_SELECT, PIN(0, 1), GPIO_OUT_LOW)
GPIO(AP_FLASH_SELECT, PIN(0, 2), GPIO_OUT_LOW)

/* As an input this mirrors EC_WP_L (which is controlled by RBOX, not a GPIO).
 * As an output it can override EC_WP_L, although why would we? */
GPIO(AP_WP_L,         PIN(0, 3), GPIO_INPUT)

/* Pull this low to reset the AP. (We reset the EC with the RBOX.) */
GPIO(SYS_RST_L_OUT,   PIN(0, 4), GPIO_INPUT)

/* Indicate to EC when CCD is enabled. EC can pull this down too, to tell us if
 * it decided instead. The pullup is on the EC's side. */
GPIO(CCD_MODE_L,      PIN(0, 5), GPIO_ODR_HIGH)

/* Battery has a 10K pulldown on its side. We provide the pullup. */
GPIO(BATT_PRES,       PIN(0, 6), GPIO_INPUT)

/* GPIOs used to tristate the SPI bus */
GPIO(SPI_MOSI,        PIN(0, 7), GPIO_INPUT)
GPIO(SPI_CLK,         PIN(0, 8), GPIO_INPUT)
GPIO(SPI_CS_L,        PIN(0, 9), GPIO_INPUT)

/* Control the load switch powering the INA 3.3V rail */
GPIO(EN_PP3300_INA_L, PIN(1, 9), GPIO_ODR_HIGH)

/* Unimplemented signals which we need to emulate for now */
/* TODO(wfrichar): Half the boards don't use this signal. Take it out. */
UNIMPLEMENTED(ENTERING_RW)

/*
 * If we are included by generic GPIO code that doesn't know about the PINMUX
 * macro we need to provide an empty definition so that the invocations don't
 * interfere with other GPIO processing.
 */
#ifndef PINMUX
#define PINMUX(...)
#endif

/* GPIOs - mark outputs as inputs too, to read back from the driven pad */
PINMUX(GPIO(INT_AP_L),        A5, DIO_INPUT)	/* DIOB7 is p_digitial_od */
                                                /* We can't pull it up */
PINMUX(GPIO(EC_FLASH_SELECT), B2, DIO_INPUT)
PINMUX(GPIO(AP_FLASH_SELECT), B3, DIO_INPUT)
PINMUX(GPIO(EN_PP3300_INA_L), B7, DIO_INPUT)
PINMUX(GPIO(AP_WP_L),         M3, 0)
PINMUX(GPIO(SYS_RST_L_IN),    M0, DIO_WAKE_FALLING)
PINMUX(GPIO(SYS_RST_L_OUT),   M0, DIO_INPUT)
PINMUX(GPIO(CCD_MODE_L),      M1, DIO_INPUT)
PINMUX(GPIO(BATT_PRES),       M2, 0)

/* UARTs */
PINMUX(FUNC(UART0_TX),        A0, DIO_OUTPUT)	/* Cr50 console */
/*
 * UART0_RX should only be used for debugging purposes. There is no internal
 * pull up on DIOA13, so make sure there is an external pull up. If there is
 * no external pull up cr50 will be held in UART0RX interrupts and will
 * constantly reboot.
 *   UART0_RX = DIOA13
 *
 * UART1_TX and UART2_TX are configured in usart.c. They are not set as outputs
 * here in order to avoid interfering with servo. They can be controlled using
 * the 'uart' console command.
 *   UART1_TX = DIOA7	AP console
 *   UART2_TX = DIOB5	EC console
 */
PINMUX(FUNC(UART1_RX),        A3, DIO_INPUT)	/* AP console */
PINMUX(FUNC(UART2_RX),        B6, DIO_INPUT)	/* EC console */
/*
 * Monitor UART RX/TX signals to detect state changes on the EC, AP, and servo.
 *
 * The idle state of the RX signals when the AP or EC are powered on is high.
 * When they are not powered, the signals will remain low. When servo is
 * connected it drives the TX signals high. The servo TX signals are wired
 * to cr50's. Because the two device TX signals are directly wired together,
 * driving the cr50 uart TX at the same time as servo is driving those pins may
 * damage both servo and cr50.
 */
PINMUX(GPIO(AP_ON),           A3, DIO_INPUT)
PINMUX(GPIO(AP_OFF),          A3, DIO_INPUT)
PINMUX(GPIO(EC_ON),           B6, DIO_INPUT)
PINMUX(GPIO(EC_OFF),          B6, DIO_INPUT)
PINMUX(GPIO(SERVO_UART1_ON),  A7, DIO_INPUT)
PINMUX(GPIO(SERVO_UART1_OFF), A7, DIO_INPUT)
PINMUX(GPIO(SERVO_UART2_ON),  B5, DIO_INPUT)
PINMUX(GPIO(SERVO_UART2_OFF), B5, DIO_INPUT)

/* I2C pins are bi-directional */
PINMUX(FUNC(I2C0_SCL),        B0, DIO_INPUT)
PINMUX(FUNC(I2C0_SDA),        B1, DIO_INPUT)

/*
 * Both SPI master and slave buses are wired directly to specific pads
 *
 * If CONFIG_SPS is defined, these pads are used:
 *   DIOA2  = SPS_MOSI  (input)
 *   DIOA6  = SPS_CLK   (input)
 *   DIOA10 = SPS_MISO  (output)
 *   DIOA12 = SPS_CS_L  (input)
 * The digital inputs are enabled in sps.c
 *
 * If CONFIG_SPI_MASTER is defined, these pads are used:
 *   DIOA4  = SPI_MOSI  (output)
 *   DIOA8  = SPI_CLK   (output)
 *   DIOA11 = SPI_MISO  (input)
 *   DIOA14 = SPI_CS_L  (output)
 * The pads are only connected to the peripheral outputs when SPI is enabled to
 * avoid interfering with other things on the board.
 * Note: Double-check to be sure these are configured in spi_master.c
 */
PINMUX(GPIO(SPI_MOSI),           A4, DIO_OUTPUT)
PINMUX(GPIO(SPI_CLK),            A8, DIO_OUTPUT)
PINMUX(GPIO(SPI_CS_L),          A14, DIO_OUTPUT)
#undef PINMUX
