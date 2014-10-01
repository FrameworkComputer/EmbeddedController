/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lightbar IC interface */

#ifndef __CROS_EC_LB_COMMON_H
#define __CROS_EC_LB_COMMON_H

#include "ec_commands.h"

/* How many (logical) LEDs do we have? */
#define NUM_LEDS 4

/* Set the color of one LED (or all if the LED number is too large) */
void lb_set_rgb(unsigned int led, int red, int green, int blue);
/* Get the current color of one LED. Fails if the LED number is too large. */
int lb_get_rgb(unsigned int led, uint8_t *red, uint8_t *green, uint8_t *blue);
/* Set the overall brightness level. */
void lb_set_brightness(unsigned int newval);
/* Get the overall brighness level. */
uint8_t lb_get_brightness(void);
/* Initialize the IC controller registers to sane values. */
void lb_init(void);
/* Disable the LED current off (the IC stays on). */
void lb_off(void);
/* Enable the LED current. */
void lb_on(void);
/* Fill in the response fields for the LIGHTBAR_CMD_DUMP command. */
void lb_hc_cmd_dump(struct ec_response_lightbar *out);
/* Write the IC controller register given by the LIGHTBAR_CMD_REG command. */
void lb_hc_cmd_reg(const struct ec_params_lightbar *in);
/*
 * Optional (see config.h). Request that the lightbar power rails be on or off.
 * Returns true if a change to the rails was made, false if it wasn't.
 */
int lb_power(int enabled);

#endif  /* __CROS_EC_LB_COMMON_H */
