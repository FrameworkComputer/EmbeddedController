/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Functions needed by keyboard scanner module for Chrome EC */

#include "common.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "clock.h"
#include "gpio.h"
#include "registers.h"
#include "task.h"

/**
 * Initialize the raw keyboard interface.
 */
void keyboard_raw_init(void)
{
	/* Enable clock for KBS peripheral */
	clock_enable_peripheral(CGC_OFFSET_KBS, CGC_KBS_MASK,
				CGC_MODE_RUN | CGC_MODE_SLEEP);

	/* Ensure top-level interrupt is disabled */
	keyboard_raw_enable_interrupt(0);

	/*
	 * Select quasi-bidirectional buffers for KSO pins. It reduces the
	 * low-to-high transition time. This feature only supports in npcx7.
	 */
#ifdef CONFIG_KEYBOARD_KSO_HIGH_DRIVE
	SET_FIELD(NPCX_KBSCTL, NPCX_KBHDRV_FIELD, 0x01);
#endif

	/* pull-up KBSIN 0-7 internally */
	NPCX_KBSINPU = 0xFF;

	/* Disable automatic scan mode */
	CLEAR_BIT(NPCX_KBSCTL, NPCX_KBSMODE);

	/* Disable automatic interrupt enable */
	CLEAR_BIT(NPCX_KBSCTL, NPCX_KBSIEN);

	/* Disable increment enable */
	CLEAR_BIT(NPCX_KBSCTL, NPCX_KBSINC);

	/* Set KBSOUT to zero to detect key-press */
	NPCX_KBSOUT0 = 0x00;
	NPCX_KBSOUT1 = 0x00;

	gpio_config_module(MODULE_KEYBOARD_SCAN, 1);

	/*
	 * Enable interrupts for the inputs. The top-level interrupt is still
	 * masked off, so this won't trigger interrupts yet.
	 */

	/* Clear pending input sources used by scanner */
	NPCX_WKPCL(MIWU_TABLE_WKKEY, MIWU_GROUP_WKKEY) = 0xFF;

	/* Enable Wake-up Button */
	NPCX_WKEN(MIWU_TABLE_WKKEY, MIWU_GROUP_WKKEY) = 0xFF;

	/* Select high to low transition (falling edge) */
	NPCX_WKEDG(MIWU_TABLE_WKKEY, MIWU_GROUP_WKKEY) = 0xFF;

	/* Enable interrupt of WK KBS */
	keyboard_raw_enable_interrupt(1);
}

/**
 * Finish initialization after task scheduling has started.
 */
#if !defined(CONFIG_KEYBOARD_SCAN_ADC)
void keyboard_raw_task_start(void)
{
	/* Enable MIWU to trigger KBS interrupt */
	task_enable_irq(NPCX_IRQ_KSI_WKINTC_1);
}

/**
 * Drive the specified column low.
 */
test_mockable void keyboard_raw_drive_column(int col)
{
	/*
	 * Nuvoton Keyboard Scan IP supports 18x8 Matrix
	 * It also support automatic scan functionality
	 */
	uint32_t mask, col_out;

	/* Add support for CONFIG_KEYBOARD_KSO_BASE shifting */
	col_out = col + CONFIG_KEYBOARD_KSO_BASE;

	/* Drive all lines to high */
	if (col == KEYBOARD_COLUMN_NONE) {
		mask = ~0;
#if defined(CONFIG_KEYBOARD_CUSTOMIZATION)
		board_keyboard_drive_col(col);
#elif defined(CONFIG_KEYBOARD_COL2_INVERTED)
		gpio_set_level(GPIO_KBD_KSO2, 0);
#endif
	}
	/* Set KBSOUT to zero to detect key-press */
	else if (col == KEYBOARD_COLUMN_ALL) {
		mask = ~(BIT(keyboard_cols) - 1);
#if defined(CONFIG_KEYBOARD_CUSTOMIZATION)
		board_keyboard_drive_col(col);
#elif defined(CONFIG_KEYBOARD_COL2_INVERTED)
		gpio_set_level(GPIO_KBD_KSO2, 1);
#endif
	}
	/* Drive one line for detection */
	else {
#if defined(CONFIG_KEYBOARD_CUSTOMIZATION)
		board_keyboard_drive_col(col);
#elif defined(CONFIG_KEYBOARD_COL2_INVERTED)
		if (col == 2)
			gpio_set_level(GPIO_KBD_KSO2, 1);
		else
			gpio_set_level(GPIO_KBD_KSO2, 0);
#endif
		mask = ~BIT(col_out);
	}

	/* Set KBSOUT */
	NPCX_KBSOUT0 = (mask & 0xFFFF);
	NPCX_KBSOUT1 = ((mask >> 16) & 0x03);
}

/**
 * Read raw row state.
 * Bits are 1 if signal is present, 0 if not present.
 */
test_mockable int keyboard_raw_read_rows(void)
{
	/* Bits are active-low, so invert returned levels */
	return (~NPCX_KBSIN) & KB_ROW_MASK;
}

#ifndef NPCX_SELECT_KSI_TO_GPIO
/**
 * Enable or disable keyboard interrupts.
 */
void keyboard_raw_enable_interrupt(int enable)
{
	if (enable)
		task_enable_irq(NPCX_IRQ_KSI_WKINTC_1);
	else
		task_disable_irq(NPCX_IRQ_KSI_WKINTC_1);
}

/*
 * Interrupt handler for the entire GPIO bank of keyboard rows.
 */
static void keyboard_raw_interrupt(void)
{
	/* Clear pending input sources used by scanner */
	NPCX_WKPCL(MIWU_TABLE_WKKEY, MIWU_GROUP_WKKEY) = 0xFF;

	/* Wake the scan task */
	task_wake(TASK_ID_KEYSCAN);
}
DECLARE_IRQ(NPCX_IRQ_KSI_WKINTC_1, keyboard_raw_interrupt, 5);
#endif

#else
void keyboard_raw_task_start(void)
{
	/* Enable interrupts for keyboard matrix inputs */
	keyboard_raw_enable_interrupt(1);
}

static void set_kb_columns(int level)
{
	gpio_set_level(GPIO_KSO_00, level & BIT(0));
	gpio_set_level(GPIO_KSO_01, level & BIT(1));
	gpio_set_level(GPIO_KSO_02, level & BIT(2));
	gpio_set_level(GPIO_KSO_03, level & BIT(3));
	gpio_set_level(GPIO_KSO_04, level & BIT(4));
	gpio_set_level(GPIO_KSO_05, level & BIT(5));
	gpio_set_level(GPIO_KSO_06, level & BIT(6));
	gpio_set_level(GPIO_KSO_07, level & BIT(7));
	gpio_set_level(GPIO_KSO_08, level & BIT(8));
	gpio_set_level(GPIO_KSO_09, level & BIT(9));
	gpio_set_level(GPIO_KSO_10, level & BIT(10));
	gpio_set_level(GPIO_KSO_11, level & BIT(11));
	gpio_set_level(GPIO_KSO_12, level & BIT(12));
	gpio_set_level(GPIO_KSO_13, level & BIT(13));
	gpio_set_level(GPIO_KSO_14, level & BIT(14));
}

void keyboard_raw_drive_column(int col)
{
	if (col == KEYBOARD_COLUMN_NONE)
		/* Drive all lines to low */
		set_kb_columns(0);

	else if (col == KEYBOARD_COLUMN_ALL)
		/* Drive all lines to high to detect any key press */
		set_kb_columns(0xFFFF);

	else
		set_kb_columns(BIT(col));
}

void keyboard_raw_enable_interrupt(int enable)
{
	if (enable) {
		gpio_enable_interrupt(GPIO_KSI_00);
		gpio_enable_interrupt(GPIO_KSI_01);
		gpio_enable_interrupt(GPIO_KSI_02);
		gpio_enable_interrupt(GPIO_KSI_03);
		gpio_enable_interrupt(GPIO_KSI_04);
		gpio_enable_interrupt(GPIO_KSI_05);
		gpio_enable_interrupt(GPIO_KSI_06);
		gpio_enable_interrupt(GPIO_KSI_07);
		gpio_enable_interrupt(GPIO_RFR_KEY_L);
	} else {
		gpio_disable_interrupt(GPIO_KSI_00);
		gpio_disable_interrupt(GPIO_KSI_01);
		gpio_disable_interrupt(GPIO_KSI_02);
		gpio_disable_interrupt(GPIO_KSI_03);
		gpio_disable_interrupt(GPIO_KSI_04);
		gpio_disable_interrupt(GPIO_KSI_05);
		gpio_disable_interrupt(GPIO_KSI_06);
		gpio_disable_interrupt(GPIO_KSI_07);
		gpio_disable_interrupt(GPIO_RFR_KEY_L);
	}
}

void keyboard_raw_gpio_interrupt(enum gpio_signal signal)
{
	/* Wake the scan task */
	task_wake(TASK_ID_KEYSCAN);
}
#endif

int keyboard_raw_is_input_low(int port, int id)
{
	return (NPCX_PDIN(port) & BIT(id)) == 0;
}
