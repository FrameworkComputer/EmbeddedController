/* power_button.h - Power button function.
 *                  Hides chip-specific implementation behind this interface.
 *
 * (Chromium license) */

#ifndef __CHIP_INTERFACE_AC_PRESENT_H
#define __CHIP_INTERFACE_AC_PRESENT_H

/*
 * Initialize the AC present as GPIO input pin and enable interrupt for
 * callback.
 */
EcError CrAcPresentInit(void);


/* Calls GPIOPinRead() to read the GPIO state. */
/* TODO: has the state been debounced? */
EcError CrAcPrensentState(void);


/* Register a calback function. It is called while AC is plugged in or
 * unplugged.
 */
EcError CrAcPresentRegister(void (*callback)(void));

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
