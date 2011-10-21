/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * power_button.h - Power button function.
 *                  Hides chip-specific implementation behind this interface.
 */

#ifndef __CHIP_INTERFACE_POWER_BUTTON_H
#define __CHIP_INTERFACE_POWER_BUTTON_H

/*
 * Initialize the power button as GPIO input pin and enable interrupt for
 *   keyboard scanner code.
 */
EcError EcPowerButtonInit(void);


/* Calls GPIOPinRead() to read the GPIO state. */
/* TODO: has the state been debounced? */
EcError EcPowerButtonGetState(void);


/* Register a calback function. It is called while power button is changing its
 * state (pressed or released ).
 */
EcError EcPowerButtonRegister(void (*callback)(void));

/* Below is the example code to register this function. */
#if 0
/* This callback function is implemented in Chrome OS features layer. */
void PowerButtonCallback(void) {
  int pressed = EcPowerButtonGetState();
  if (!prev_status) {
    if (pressed) {
      // Power button is just pressed. Generate scan code,
      // and kick off the state machine for PWRBTN# signal.
    }
  } else {
    if (!pressed) {
      // Power button is just released. Generate scan code,
      // and clear the state machine.
    }
  }
}

  ... somewhere in init code ...
  EcPowerButtonRegister(PowerButtonCallback);

#endif /* #if 0 */

#endif  /* __CHIP_INTERFACE_POWER_BUTTON_H */
