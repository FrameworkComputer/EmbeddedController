/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#ifndef __CROS_EC_GPIO_H
#define __CROS_EC_GPIO_H

#include "ec_common.h"

/* GPIO signal definitions. */
typedef enum EcGpioSignal {
  /* Firmware write protect */
  EC_GPIO_WRITE_PROTECT = 0,
  /* Recovery switch */
  EC_GPIO_RECOVERY_SWITCH,
  /* Debug LED */
  EC_GPIO_DEBUG_LED
} EcGpioSignal;


/* Initializes the GPIO module. */
EcError EcGpioInit(void);

/* Functions should return an error if the requested signal is not
 * supported / not present on the board. */

/* Gets the current value of a signal (0=low, 1=hi). */
EcError EcGpioGet(EcGpioSignal signal, int* value_ptr);

/* Sets the current value of a signal.  Returns error if the signal is
 * not supported or is an input signal. */
EcError EcGpioSet(EcGpioSignal signal, int value);

#endif  /* __CROS_EC_GPIO_H */
