/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * power_button.h - Power button function.
 *                  Hides chip-specific implementation behind this interface.
 */

#ifndef __CHIP_INTERFACE_BATTERY_H
#define __CHIP_INTERFACE_BATTERY_H


/***********  Battery SMBus  *********************************************/
/* Initialize the SMBus */
EcError EcBatteryInit(void);

/* Send a command to battery. Blocking */
EcError EcBatterySendCommand(...);

/* non-blocking read so that it can support both polling mode
 * and interrupt callback.
 */
EcError EcBatteryRecvCommand(...);

/* Register a callback when a packet comes from SMBus */
EcError EcRegisterBatteryInterrupt(void (*isr)(...));

#if 0  /* example code */
void BatteryPacketArrive(...) {
  EcBatteryRecvCommand();  // read the packet.
}

  ... somewhere in code ...
  EcRegisterBatteryInterrupt(BatteryPacketArrive);

#endif


/***********  Battery Present  *********************************************/
/*
 * Initialize the battery present as GPIO input pin and enable interrupt for
 * callback.
 */
EcError EcBatteryPresentInit(void);

/* Calls GPIOPinRead() to read the GPIO state. */
/* TODO: has the state been debounced? */
EcError EcBatteryPrensentState(void);

/* Register a calback function. It is called while AC is plugged in or
 * unplugged.
 */
EcError EcBatteryPresentRegister(void (*callback)(void));

/* Below is the example code to register this function. */
#if 0
/* This callback function is implemented in Chrome OS features layer. */
void BatteryStateChanged(void) {
  int battery_present = EcBatteryPresentState();
  if (battery_present) {
    // start to authenticate battery;
    // once authenticated, charge the battery;
  } else {
    // stop charge battery;
  }
}
  ... somewhere in init code ...
  EcBatteryPresentRegister(BatteryStateChanged);

#endif /* #if 0 */

#endif  /* __CHIP_INTERFACE_BATTERY_H */
