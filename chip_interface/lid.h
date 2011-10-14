/* lid.h - handle lid open/close
 *
 * (Chromium license) */

#ifndef __CHIP_INTERFACE_LID_H
#define __CHIP_INTERFACE_LID_H

/* Initialize the GPIO pin */
EcError CrLidSwitchInit(void);

/* Calls GPIOPinRead() to read the GPIO state. */
/* TODO: has the state been debounced? */
EcError CrLidSwitchState(void);

/* Register a calback function. It is called while lid state is changed.
 */
EcError CrLidSwitchRegister(void (*callback)(void));

/* Below is the example code to register this function. */
#if 0
/* This callback function is implemented in Chrome OS features layer. */
void LidSwitchChanged(void) {
  int lid_open = CrLidSwitchState();
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
  CrLidSwitchRegister(LidSwitchChanged);
#endif

#endif  /* __CHIP_INTERFACE_LID_H */
