/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header for tablet_mode.c */

#ifdef CONFIG_TABLET_MODE

/* Return 1 if in tablet mode, 0 otherwise */
int tablet_get_mode(void);
void tablet_set_mode(int mode);

/**
 * Interrupt service routine for tablet switch.
 *
 * TABLET_MODE_GPIO_L must be defined.
 *
 * @param signal: GPIO signal
 */
void tablet_mode_isr(enum gpio_signal signal);

/**
 * Disables the tablet mode switch sub-system and turns off tablet mode. This is
 * useful for clamshell devices.
 */
void tablet_disable_switch(void);

#else

static inline int tablet_get_mode(void) { return 0; }

#endif
