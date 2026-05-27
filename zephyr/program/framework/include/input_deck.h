#ifndef __CROS_EC_INPUT_DECK_H
#define __CROS_EC_INPUT_DECK_H

#include <stdbool.h>
#include "gpio_signal.h"

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

/* called from the 8042 input layer */
void board_caps_led_control(int data);

/* referenced from device trees */
void input_deck_keyboard_interrupt(enum gpio_signal signal);

#endif /* __CROS_EC_INPUT_DECK_H */
