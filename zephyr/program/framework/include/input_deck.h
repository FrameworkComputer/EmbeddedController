#ifndef __CROS_EC_INPUT_DECK_H
#define __CROS_EC_INPUT_DECK_H

#include <stdbool.h>
#include "gpio_signal.h"

/* Consumer Control Configuration, mapped to non-fn F12 */
#define HID_USAGEID_AL_CCC 0x0183u

struct input_deck {
	/* Enable power (S5 -> S3) */
	void (*power_on)(void);

	/* Turn off power (S3 -> S5) */
	void (*power_off)(void);

	/* Called after power on, or on resume */
	void (*resume)(void);

	/* Called before suspending */
	void (*suspend)(void);

	/* Keyboard interrupt */
	void (*kb_irq)(void);

	/* Set leds according to 8042 bit mask. */
	void (*set_kb_leds)(unsigned int data);

	/* Tablet-mode or lid-state change, as detected/triggered internally */
	void (*lid_change)(void);
};

/* Lifecycle entry points dispatched through the active deck's ops. */
void input_deck_power_on(void);
void input_deck_power_off(void);
void input_deck_resume(void);
void input_deck_suspend(void);
void input_deck_lid_change(void);
bool input_deck_is_present(void);

/* called from the 8042 input layer */
void board_caps_led_control(int data);

/* referenced from device trees */
void input_deck_keyboard_interrupt(enum gpio_signal signal);

/**
 * If ESC key is detected, set flag SYSTEM_IN_MANUAL_RECOVERY.
 */
void check_bios_crisis_key(bool esc_key_press);

#endif /* __CROS_EC_INPUT_DECK_H */
