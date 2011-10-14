/* power_button.h - Power button function.
 *                  Hides chip-specific implementation behind this interface.
 *
 * (Chromium license) */

#ifndef __CHIP_INTERFACE_BATTERY_H
#define __CHIP_INTERFACE_BATTERY_H


/***********  Battery SMBus  *********************************************/
/* Initialize the SMBus */
EcError CrBatteryInit(void);

/* Send a command to battery. Blocking */
EcError CrBatterySendCommand(...);

/* non-blocking read so that it can support both polling mode
 * and interrupt callback.
 */
EcError CrBatteryRecvCommand(...);

/* Register a callback when a packet comes from SMBus */
EcError CrRegisterBatteryInterrupt(void (*isr)(...));

#if 0  /* example code */
void BatteryPacketArrive(...) {
  CrBatteryRecvCommand();  // read the packet.
}

  ... somewhere in code ...
  CrRegisterBatteryInterrupt(BatteryPacketArrive);

#endif


/***********  Battery Present  *********************************************/
/*
 * Initialize the battery present as GPIO input pin and enable interrupt for
 * callback.
 */
EcError CrBatteryPresentInit(void);

/* Calls GPIOPinRead() to read the GPIO state. */
/* TODO: has the state been debounced? */
EcError CrBatteryPrensentState(void);

/* Register a calback function. It is called while AC is plugged in or
 * unplugged.
 */
EcError CrBatteryPresentRegister(void (*callback)(void));

/* Below is the example code to register this function. */
#if 0
/* This callback function is implemented in Chrome OS features layer. */
void BatteryStateChanged(void) {
  int battery_present = CrBatteryPresentState();
  if (battery_present) {
    // start to authenticate battery;
    // once authenticated, charge the battery;
  } else {
    // stop charge battery;
  }
}
  ... somewhere in init code ...
  CrBatteryPresentRegister(BatteryStateChanged);

#endif /* #if 0 */

#endif  /* __CHIP_INTERFACE_BATTERY_H */
