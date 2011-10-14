/* power_button.h - Power button function.
 *                  Hides chip-specific implementation behind this interface.
 *
 * (Chromium license) */

#ifndef __CHIP_INTERFACE_POWER_BUTTON_H
#define __CHIP_INTERFACE_POWER_BUTTON_H

/*
 * Initialize the power button as GPIO input pin and enable interrupt for
 *   keyboard scanner code.
 */
EcError CrPowerButtonInit(void);


/* Calls GPIOPinRead() to read the GPIO state. */
/* TODO: has the state been debounced? */
EcError CrPowerButtonGetState(void);


/* Register a calback function. It is called while power button is changing its
 * state (pressed or released ).
 */
EcError CrPowerButtonRegister(void (*callback)(void));

/* Below is the example code to register this function. */
#if 0
/* This callback function is implemented in Chrome OS features layer. */
void PowerButtonCallback(void) {
  int pressed = CrPowerButtonGetState();
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
  CrPowerButtonRegister(PowerButtonCallback);

#endif /* #if 0 */

#endif  /* __CHIP_INTERFACE_POWER_BUTTON_H */
