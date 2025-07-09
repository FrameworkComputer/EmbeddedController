/* -*- mode:c -*-
 *
 * Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

GPIO_INT(LID_OPEN,             PIN(0, 0), GPIO_INT_BOTH, lid_interrupt)
GPIO_INT(POWER_BUTTON_L,       PIN(0, 1), GPIO_INT_BOTH, power_button_interrupt)
GPIO_INT(AC_PRESENT,           PIN(0, 2), GPIO_INT_BOTH, extpower_interrupt)
GPIO_INT(VOLUME_DOWN_L,        PIN(0, 3), GPIO_INT_BOTH, button_interrupt)
GPIO_INT(VOLUME_UP_L,          PIN(0, 4), GPIO_INT_BOTH, button_interrupt)
GPIO_INT(CHARGE_DONE,          PIN(0, 5), GPIO_INT_BOTH, inductive_charging_interrupt)
GPIO_INT(TABLET_MODE_L,        PIN(0, 19), GPIO_INT_BOTH, gmr_tablet_switch_isr)
/* Fingerprint */
GPIO_INT(FPS_INT,              PIN(0, 14), GPIO_INT_RISING, fps_event)

GPIO(EC_INT_L,             PIN(0, 6), 0)
GPIO(WP,                   PIN(0, 7), 0)
GPIO(ENTERING_RW,          PIN(0, 8), 0)
GPIO(PCH_BKLTEN,           PIN(0, 9), 0)
GPIO(ENABLE_BACKLIGHT,     PIN(0, 10), 0)

/* Inductive charging */
GPIO(CHARGE_EN,            PIN(0, 11), 0)
GPIO(BASE_CHG_VDD_EN,      PIN(0, 12), 0)

/* Fingerprint */
GPIO(SPI1_NSS,              PIN(0, 13), GPIO_OUT_HIGH)

GPIO(USB_C0_DISCHARGE,     PIN(0, 15), 0)

GPIO(I2C_SCL,             PIN(0, 16),  GPIO_INPUT)
GPIO(I2C_SDA,             PIN(0, 17),  GPIO_INPUT)

GPIO(EC_CBI_WP,           PIN(0, 18), GPIO_OUT_LOW)

GPIO(USB_C0_5V_EN,        PIN(0,20), GPIO_INPUT)
GPIO(USB_C1_5V_EN,        PIN(0,21), GPIO_INPUT)
