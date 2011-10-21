/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * power_button.h - Power button function.
 *                  Hides chip-specific implementation behind this interface.
 */

#ifndef __CHIP_INTERFACE_AC_PRESENT_H
#define __CHIP_INTERFACE_AC_PRESENT_H

/*
 * Initialize the AC present as GPIO input pin and enable interrupt for
 * callback.
 */
EcError EcAcPresentInit(void);


/* Calls GPIOPinRead() to read the GPIO state. */
/* TODO: has the state been debounced? */
EcError EcAcPrensentState(void);


/* Register a calback function. It is called while AC is plugged in or
 * unplugged.
 */
EcError EcAcPresentRegister(void (*callback)(void));

/* Below is the example code to register this function. */
#if 0
/* This callback function is implemented in Chrome OS features layer. */
void CrAcStateChanged(void) {
  int ac_present = CrAcPrensentState();
  if (ac_present) {
    if (battery_present && authenticated) {
      // start to charge battery;
    }
  } else {
    // stop charge battery;
  }
}
  ... somewhere in init code ...
  CrAcPresentRegister(CrACStateChanged);

#endif /* #if 0 */

#endif  /* __CHIP_INTERFACE_AC_PRESENT_H */
