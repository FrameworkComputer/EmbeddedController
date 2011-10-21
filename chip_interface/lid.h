/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * lid.h - handle lid open/close
 */

#ifndef __CHIP_INTERFACE_LID_H
#define __CHIP_INTERFACE_LID_H

/* Initialize the GPIO pin */
EcError EcLidSwitchInit(void);

/* Calls GPIOPinRead() to read the GPIO state. */
/* TODO: has the state been debounced? */
EcError EcLidSwitchState(void);

/* Register a calback function. It is called while lid state is changed.
 */
EcError EcLidSwitchRegister(void (*callback)(void));

/* Below is the example code to register this function. */
#if 0
/* This callback function is implemented in Chrome OS features layer. */
void LidSwitchChanged(void) {
  int lid_open = EcLidSwitchState();
  if (lid_open) {
    if (system is in S3) {
      // resume system
    }
  } else {
    if (system is in S0) {
      // suspend system
    }
  }
}
  ... somewhere in init code ...
  EcLidSwitchRegister(LidSwitchChanged);
#endif

#endif  /* __CHIP_INTERFACE_LID_H */
