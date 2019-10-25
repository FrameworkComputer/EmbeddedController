/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "gpio.h"
#include "gpio_chip.h"
#include "i2c.h"
#include "keyboard_config.h"
#include "hooks.h"
#include "registers.h"
#include "switch.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "system.h"
#include "system_chip.h"
#include "lpc_chip.h"
#include "ec_commands.h"
#include "host_command.h"
#include "hwtimer_chip.h"

#if !(DEBUG_GPIO)
#define CPUTS(...)
#define CPRINTS(...)
#else
#define CPUTS(outstr) cputs(CC_GPIO, outstr)
#define CPRINTS(format, args...) cprints(CC_GPIO, format, ## args)
#endif

/* Constants for GPIO interrupt mapping */
#define GPIO_INT(name, pin, flags, signal) NPCX_WUI_GPIO_##pin,
#ifdef CONFIG_LOW_POWER_IDLE
/* Extend gpio_wui_table for the bypass of better power consumption */
#define GPIO(name, pin, flags) NPCX_WUI_GPIO_##pin,
#define UNIMPLEMENTED(name) WUI_NONE,
#else
/* Ignore GPIO and UNIMPLEMENTED definitions if not using lower power idle */
#define GPIO(name, pin, flags)
#define UNIMPLEMENTED(name)
#endif
static const struct npcx_wui gpio_wui_table[] = {
	#include "gpio.wrap"
};

struct npcx_gpio {
	uint8_t port  : 4;
	uint8_t bit   : 3;
	uint8_t valid : 1;
};

BUILD_ASSERT(sizeof(struct npcx_gpio) == 1);

struct npcx_alt {
	uint8_t group     : 4;
	uint8_t bit       : 3;
	uint8_t inverted  : 1;
};

struct gpio_alt_map {
	struct npcx_gpio gpio;
	struct npcx_alt  alt;
};

BUILD_ASSERT(sizeof(struct gpio_alt_map) == 2);

/* Constants for GPIO alternative mapping */
const struct gpio_alt_map gpio_alt_table[] = NPCX_ALT_TABLE;

struct gpio_lvol_item {
	struct npcx_gpio lvol_gpio[8];
};

/* Constants for GPIO low-voltage mapping */
const struct gpio_lvol_item gpio_lvol_table[] = NPCX_LVOL_TABLE;

/*****************************************************************************/
/* Internal functions */

static int gpio_match(uint8_t port, uint8_t bit, struct npcx_gpio gpio)
{
	return (gpio.valid && (gpio.port == port) && (gpio.bit == bit));
}

#ifdef CONFIG_CMD_GPIO_EXTENDED
static uint8_t gpio_is_alt_sel(uint8_t port, uint8_t bit)
{
	struct gpio_alt_map const *map;
	uint8_t alt_mask, devalt;

	for (map = ARRAY_BEGIN(gpio_alt_table);
	     map < ARRAY_END(gpio_alt_table);
	     map++) {
		if (gpio_match(port, bit, map->gpio)) {
			alt_mask = 1 << map->alt.bit;
			devalt = NPCX_DEVALT(map->alt.group);
			/*
			 * alt.inverted == 0:
			 *   !!(devalt & alt_mask) == 0 -> GPIO
			 *   !!(devalt & alt_mask) == 1 -> Alternate
			 * alt.inverted == 1:
			 *   !!(devalt & alt_mask) == 0 -> Alternate
			 *   !!(devalt & alt_mask) == 1 -> GPIO
			 */
			return !!(devalt & alt_mask) ^ map->alt.inverted;
		}
	}
	return 0;
}
#endif

static int gpio_alt_sel(uint8_t port, uint8_t bit,
			enum gpio_alternate_func func)
{
	struct gpio_alt_map const *map;

	for (map = ARRAY_BEGIN(gpio_alt_table);
	     map < ARRAY_END(gpio_alt_table);
	     map++) {
		if (gpio_match(port, bit, map->gpio)) {
			uint8_t alt_mask = 1 << map->alt.bit;

			/*
			 * func < GPIO_ALT_FUNC_DEFAULT -> GPIO functionality
			 * map->alt.inverted -> Set DEVALT bit for GPIO
			 */
			if ((func < GPIO_ALT_FUNC_DEFAULT) ^ map->alt.inverted)
				NPCX_DEVALT(map->alt.group) &= ~alt_mask;
			else
				NPCX_DEVALT(map->alt.group) |=  alt_mask;

			return 1;
		}
	}

	if (func > GPIO_ALT_FUNC_DEFAULT)
		CPRINTS("Warn! No alter func in port%d, pin%d", port, bit);

	return -1;
}

/* Set interrupt type for GPIO input */
static void gpio_interrupt_type_sel(enum gpio_signal signal, uint32_t flags)
{
	uint8_t table, group, pmask;

	if (signal >= GPIO_IH_COUNT)
		return;

	table = gpio_wui_table[signal].table;
	group = gpio_wui_table[signal].group;
	pmask = 1 << gpio_wui_table[signal].bit;

	ASSERT(flags & GPIO_INT_ANY);

	/* Handle interrupt for level trigger */
	if ((flags & GPIO_INT_F_HIGH) || (flags & GPIO_INT_F_LOW)) {
		/* Set detection mode to level */
		NPCX_WKMOD(table, group) |= pmask;
		/* Handle interrupting on level high */
		if (flags & GPIO_INT_F_HIGH)
			NPCX_WKEDG(table, group) &= ~pmask;
		/* Handle interrupting on level low */
		else if (flags & GPIO_INT_F_LOW)
			NPCX_WKEDG(table, group) |= pmask;
	}
	/* Handle interrupt for edge trigger */
	else {
		/* Set detection mode to edge */
		NPCX_WKMOD(table, group) &= ~pmask;
		/* Handle interrupting on both edges */
		if ((flags & GPIO_INT_F_RISING) &&
			(flags & GPIO_INT_F_FALLING)) {
			/* Enable any edge */
			NPCX_WKAEDG(table, group) |= pmask;
		}
		/* Handle interrupting on rising edge */
		else if (flags & GPIO_INT_F_RISING) {
			/* Disable any edge */
			NPCX_WKAEDG(table, group) &= ~pmask;
			NPCX_WKEDG(table, group) &= ~pmask;
		}
		/* Handle interrupting on falling edge */
		else if (flags & GPIO_INT_F_FALLING) {
			/* Disable any edge */
			NPCX_WKAEDG(table, group) &= ~pmask;
			NPCX_WKEDG(table, group) |= pmask;
		}
	}

	/* Enable wake-up input sources */
	NPCX_WKINEN(table, group) |= pmask;
	/*
	 * Clear pending bit since it might be set
	 * if WKINEN bit is changed.
	 */
	NPCX_WKPCL(table, group) |= pmask;

	/* No support analog mode */
}

#ifdef CONFIG_CMD_GPIO_EXTENDED
static uint8_t gpio_is_low_voltage_level_sel(uint8_t port, uint8_t bit)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(gpio_lvol_table); i++) {
		const struct npcx_gpio *gpio = gpio_lvol_table[i].lvol_gpio;

		for (j = 0; j < ARRAY_SIZE(gpio_lvol_table[0].lvol_gpio); j++) {
			if (gpio_match(port, bit, gpio[j]))
				return IS_BIT_SET(NPCX_LV_GPIO_CTL(i), j);
		}
	}
	return 0;
}
#endif

/* Select low voltage detection level */
void gpio_low_voltage_level_sel(uint8_t port, uint8_t bit, uint8_t low_voltage)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(gpio_lvol_table); i++) {
		const struct npcx_gpio *gpio = gpio_lvol_table[i].lvol_gpio;

		for (j = 0; j < ARRAY_SIZE(gpio_lvol_table[0].lvol_gpio); j++) {
			if (gpio_match(port, bit, gpio[j])) {
				if (low_voltage)
					/* Select vol-detect level for 1.8V */
					SET_BIT(NPCX_LV_GPIO_CTL(i), j);
				else
					/* Select vol-detect level for 3.3V */
					CLEAR_BIT(NPCX_LV_GPIO_CTL(i), j);
				return;
			}
		}
	}

	if (low_voltage)
		CPRINTS("Warn! No low voltage support in port:0x%x, bit:%d",
								port, bit);
}

/* Set the low voltage detection level by mask */
static void gpio_low_vol_sel_by_mask(uint8_t p, uint8_t mask, uint8_t low_vol)
{
	int bit;
	uint32_t lv_mask = mask;

	while (lv_mask) {
		bit = get_next_bit(&lv_mask);
		gpio_low_voltage_level_sel(p, bit, low_vol);
	};
}
/* The bypass of low voltage IOs for better power consumption */
#ifdef CONFIG_LOW_POWER_IDLE
static int gpio_is_i2c_pin(enum gpio_signal signal)
{
	int i;

	for (i = 0; i < i2c_ports_used; i++)
		if (i2c_ports[i].scl == signal || i2c_ports[i].sda == signal)
			return 1;

	return 0;
}

static void gpio_enable_wake_up_input(enum gpio_signal signal, int enable)
{
	const struct npcx_wui *wui = gpio_wui_table + signal;

	/* Is it a valid wui mapping item? */
	if (wui->table != MIWU_TABLE_COUNT) {
		/* Turn on/off input io buffer by WKINENx registers */
		if (enable)
			SET_BIT(NPCX_WKINEN(wui->table, wui->group), wui->bit);
		else
			CLEAR_BIT(NPCX_WKINEN(wui->table, wui->group),
						wui->bit);
	}
}

void gpio_enable_1p8v_i2c_wake_up_input(int enable)
{
	int i;

	/* Set input buffer of 1.8V i2c ports. */
	for (i = 0; i < i2c_ports_used; i++) {
		if (gpio_list[i2c_ports[i].scl].flags & GPIO_SEL_1P8V)
			gpio_enable_wake_up_input(i2c_ports[i].scl, enable);
		if (gpio_list[i2c_ports[i].sda].flags & GPIO_SEL_1P8V)
			gpio_enable_wake_up_input(i2c_ports[i].sda, enable);
	}
}
#endif

/*
 * Make sure the bit depth of low voltage register.
 */
BUILD_ASSERT(ARRAY_SIZE(gpio_lvol_table[0].lvol_gpio) == 8);

/*****************************************************************************/
/* IC specific low-level driver */

void gpio_set_alternate_function(uint32_t port, uint32_t mask,
				enum gpio_alternate_func func)
{
	/* Enable alternative pins by func*/
	int pin;

	/* check each bit from mask  */
	for (pin = 0; pin < 8; pin++)
		if (mask & BIT(pin))
			gpio_alt_sel(port, pin, func);
}

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	ASSERT(signal_is_gpio(signal));

	return !!(NPCX_PDIN(gpio_list[signal].port) & gpio_list[signal].mask);
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	ASSERT(signal_is_gpio(signal));

	if (value)
		NPCX_PDOUT(gpio_list[signal].port) |=  gpio_list[signal].mask;
	else
		NPCX_PDOUT(gpio_list[signal].port) &= ~gpio_list[signal].mask;
}

#ifdef CONFIG_GPIO_GET_EXTENDED
int gpio_get_flags_by_mask(uint32_t port, uint32_t mask)
{
	uint32_t flags = 0;

	if (NPCX_PDIR(port) & mask)
		flags |= GPIO_OUTPUT;
	else
		flags |= GPIO_INPUT;

	if (NPCX_PDIN(port) & mask)
		flags |= GPIO_HIGH;
	else
		flags |= GPIO_LOW;

	if (NPCX_PTYPE(port) & mask)
		flags |= GPIO_OPEN_DRAIN;

	/* If internal pulling is enabled */
	if (NPCX_PPULL(port) & mask) {
		if (NPCX_PPUD(port) & mask)
			flags |= GPIO_PULL_DOWN;
		else
			flags |= GPIO_PULL_UP;
	}

	if (gpio_is_alt_sel(port, GPIO_MASK_TO_NUM(mask)))
		flags |= GPIO_ALTERNATE;

	if (gpio_is_low_voltage_level_sel(port, GPIO_MASK_TO_NUM(mask)))
		flags |= GPIO_SEL_1P8V;

	if (NPCX_PLOCK_CTL(port) & mask)
		flags |= GPIO_LOCKED;

	return flags;
}
#endif

void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	/* If all GPIO pins are locked, return directly */
#if defined(CHIP_FAMILY_NPCX7)
	if ((NPCX_PLOCK_CTL(port) & mask) == mask)
		return;
#endif

	/*
	 * Configure pin as input, if requested. Output is configured only
	 * after setting all other attributes, so as not to create a
	 * temporary incorrect logic state 0:input 1:output
	 */
	if (!(flags & GPIO_OUTPUT))
		NPCX_PDIR(port) &= ~mask;

	/* Select open drain 0:push-pull 1:open-drain */
	if (flags & GPIO_OPEN_DRAIN)
		NPCX_PTYPE(port) |= mask;
	else
		NPCX_PTYPE(port) &= ~mask;

	/* Select pull-up/down of GPIO 0:pull-up 1:pull-down */
	if (flags & GPIO_PULL_UP) {
		if (flags & GPIO_SEL_1P8V) {
			CPRINTS("Warn! enable internal PU and low voltage mode"
					" at the same time is illegal. port 0x%x, mask 0x%x",
					port, mask);
		} else {
			NPCX_PPUD(port)  &= ~mask;
			NPCX_PPULL(port) |= mask; /* enable pull down/up */
		}
	} else if (flags & GPIO_PULL_DOWN) {
		NPCX_PPUD(port)  |= mask;
		NPCX_PPULL(port) |= mask; /* enable pull down/up */
	} else {
		/* No pull up/down */
		NPCX_PPULL(port) &= ~mask; /* disable pull down/up */
	}

	/* 1.8V low voltage select */
	if (flags & GPIO_SEL_1P8V) {
		/*
		 * Set IO type to open-drain before selecting low-voltage level
		 */
		NPCX_PTYPE(port) |= mask;
		gpio_low_vol_sel_by_mask(port, mask, 1);
	} else
		gpio_low_vol_sel_by_mask(port, mask, 0);

	/* Set up interrupt type */
	if (flags & GPIO_INT_ANY) {
		const struct gpio_info *g = gpio_list;
		enum gpio_signal gpio_int;

		/* Find gpio signal in GPIO_INTs by port and mask */
		for (gpio_int = 0; gpio_int < GPIO_IH_COUNT; gpio_int++, g++)
			if ((g->port == port) && (g->mask & mask))
				gpio_interrupt_type_sel(gpio_int, flags);
	}

	/* Set level 0:low 1:high*/
	if (flags & GPIO_HIGH)
		NPCX_PDOUT(port) |= mask;
	else if (flags & GPIO_LOW)
		NPCX_PDOUT(port) &= ~mask;

	/* Configure pin as output, if requested 0:input 1:output */
	if (flags & GPIO_OUTPUT)
		NPCX_PDIR(port) |= mask;

	/* Lock GPIO output and configuration if need */
#if defined(CHIP_FAMILY_NPCX7)
	if (flags & GPIO_LOCKED)
		NPCX_PLOCK_CTL(port) |= mask;
#endif
}

int gpio_enable_interrupt(enum gpio_signal signal)
{
	struct npcx_wui wui;

	/* Fail if not an interrupt handler */
	if (signal >= GPIO_IH_COUNT)
		return EC_ERROR_PARAM1;

	wui = gpio_wui_table[signal];
	/* Set MIWU enable bit */
	NPCX_WKEN(wui.table, wui.group) |= 1 << wui.bit;

	return EC_SUCCESS;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	struct npcx_wui wui;

	/* Fail if not an interrupt handler */
	if (signal >= GPIO_IH_COUNT)
		return EC_ERROR_PARAM1;

	wui = gpio_wui_table[signal];
	NPCX_WKEN(wui.table, wui.group) &= ~(1 << wui.bit);

	return EC_SUCCESS;
}

int gpio_clear_pending_interrupt(enum gpio_signal signal)
{
	struct npcx_wui wui;

	/* Fail if not an interrupt handler */
	if (signal >= GPIO_IH_COUNT)
		return EC_ERROR_PARAM1;

	wui = gpio_wui_table[signal];
	NPCX_WKPCL(wui.table, wui.group) |= 1 << wui.bit;

	return EC_SUCCESS;
}

void gpio_pre_init(void)
{
	const struct gpio_info *g = gpio_list;
	int is_warm;
	int flags;
	int i, j;

	system_check_bbram_on_reset();
	is_warm = system_is_reboot_warm();

#ifdef CONFIG_GPIO_INIT_POWER_ON_DELAY_MS
	/*
	 * On power-on of some boards, H1 releases the EC from reset but then
	 * quickly asserts and releases the reset a second time. This means the
	 * EC sees 2 resets: (1) power-on reset, (2) reset-pin reset. If we add
	 * a delay between reset (1) and configuring GPIO output levels, then
	 * reset (2) will happen before the end of the delay so we avoid extra
	 * output toggles.
	 *
	 * Make sure to set up the timer before using udelay().
	 */
	if (system_get_reset_flags() & EC_RESET_FLAG_POWER_ON) {
		__hw_early_init_hwtimer(0);
		udelay(CONFIG_GPIO_INIT_POWER_ON_DELAY_MS * MSEC);
	}
#endif

#ifdef CHIP_FAMILY_NPCX7
	/*
	 * TODO: Set bit 7 of DEVCNT again for npcx7 series. Please see Errata
	 * for more information. It will be fixed in next chip.
	 */
	SET_BIT(NPCX_DEVCNT, 7);
	/* Lock VCC_RST# alternative bit in case switch to GPO77 unexpectedly */
	SET_BIT(NPCX_DEV_CTL4, NPCX_DEV_CTL4_VCC1_RST_LK);
#endif

	/* Pin_Mux for FIU/SPI (set to GPIO) */
	SET_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_GPIO_NO_SPIP);
#if defined(NPCX_INT_FLASH_SUPPORT)
	SET_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_NO_F_SPI);
#endif

	/* Pin_Mux for PWRGD */
	SET_BIT(NPCX_DEVALT(1), NPCX_DEVALT1_NO_PWRGD);

	/* Pin_Mux for PECI */
#ifndef CONFIG_PECI
	SET_BIT(NPCX_DEVALT(0xA), NPCX_DEVALTA_NO_PECI_EN);
#endif

	/* Pin_Mux for LPC & SHI */
#ifdef CONFIG_HOSTCMD_SPS
	/* Switching to eSPI mode for SHI interface */
	NPCX_DEVCNT |= 0x08;
	/* Alternate Intel bus interface LPC/eSPI to GPIOs first */
	SET_BIT(NPCX_DEVALT(ALT_GROUP_1), NPCX_DEVALT1_NO_LPC_ESPI);
#endif

	/* Clear all interrupt pending and enable bits of GPIOS */
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 8; j++) {
			NPCX_WKPCL(i, j) = 0xFF;
			NPCX_WKEN(i, j) = 0;
		}
	}

	/* No support enable clock for the GPIO port in run and sleep. */
	/* Set flag for each GPIO pin in gpio_list */
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		flags = g->flags;

		if (flags & GPIO_DEFAULT)
			continue;
		/*
		 * If this is a warm reboot, don't set the output levels or
		 * we'll shut off the AP.
		 */
		if (is_warm)
			flags &= ~(GPIO_LOW | GPIO_HIGH);

		/* Set up GPIO based on flags */
		gpio_set_flags_by_mask(g->port, g->mask, flags);

		/*
		 * Ensure that any GPIO defined in gpio.inc is actually
		 * configured as a GPIO, and not left in its default state,
		 * which may or may not be as a GPIO.
		 */
		gpio_set_alternate_function(g->port, g->mask,
					GPIO_ALT_FUNC_NONE);
	}

	/* The bypass of low voltage IOs for better power consumption */
#ifdef CONFIG_LOW_POWER_IDLE
	/* Disable input buffer of 1.8V GPIOs without ISR */
	g = gpio_list + GPIO_IH_COUNT;
	for (i = GPIO_IH_COUNT; i < GPIO_COUNT; i++, g++) {
		/*
		 * I2c ports are both alternate mode and normal gpio pin, but
		 * the alternate mode needs the wake up input even though the
		 * normal gpio definition doesn't have an ISR.
		 */
		if ((g->flags & GPIO_SEL_1P8V) && !gpio_is_i2c_pin(i))
			gpio_enable_wake_up_input(i, 0);
	}
#endif
}

/* List of GPIO IRQs to enable. Don't automatically enable interrupts for
 * the keyboard input GPIO bank - that's handled separately. Of course the
 * bank is different for different systems. */
static void gpio_init(void)
{
	/* Enable IRQs now that pins are set up */
	task_enable_irq(NPCX_IRQ_MTC_WKINTAD_0);
	task_enable_irq(NPCX_IRQ_WKINTEFGH_0);
	task_enable_irq(NPCX_IRQ_WKINTC_0);
	task_enable_irq(NPCX_IRQ_TWD_WKINTB_0);
	task_enable_irq(NPCX_IRQ_WKINTA_1);
	task_enable_irq(NPCX_IRQ_WKINTB_1);
#ifndef HAS_TASK_KEYSCAN
	task_enable_irq(NPCX_IRQ_KSI_WKINTC_1);
#endif
	task_enable_irq(NPCX_IRQ_WKINTD_1);
	task_enable_irq(NPCX_IRQ_WKINTE_1);
	task_enable_irq(NPCX_IRQ_WKINTF_1);
	task_enable_irq(NPCX_IRQ_WKINTG_1);
	task_enable_irq(NPCX_IRQ_WKINTH_1);
#if defined(CHIP_FAMILY_NPCX7)
	task_enable_irq(NPCX_IRQ_WKINTFG_2);
#endif
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Interrupt handlers */

/**
 * Handle a GPIO interrupt.
 *
 * @param wui_int wui table & group for GPIO interrupt no.
 */

static void gpio_interrupt(struct npcx_wui wui_int)
{
	int i;
	uint8_t wui_mask;
	uint8_t table = wui_int.table;
	uint8_t group = wui_int.group;

	/* Get pending mask */
	wui_mask = NPCX_WKPND(table, group) & NPCX_WKEN(table, group);

	/* Find GPIOs and execute interrupt service routine */
	for (i = 0; i < GPIO_IH_COUNT && wui_mask; i++) {
		uint8_t pin_mask = 1 << gpio_wui_table[i].bit;

		if ((gpio_wui_table[i].table == table) &&
			(gpio_wui_table[i].group == group) &&
			(wui_mask & pin_mask)) {
			/* Clear pending bit of GPIO */
			NPCX_WKPCL(table, group) = pin_mask;
			/* Execute GPIO's ISR */
			gpio_irq_handlers[i](i);
			/* In case declare the same GPIO in gpio_wui_table */
			wui_mask &= ~pin_mask;
		}
	}

	if (wui_mask)
		/* No ISR for this interrupt, just clear it */
		NPCX_WKPCL(table, group) = wui_mask;
}

/**
 * Handlers for each GPIO port.  These read and clear the interrupt bits for
 * the port, then call the master handler above.
 */

#define GPIO_IRQ_FUNC(_irq_func, wui_int)		\
void _irq_func(void)					\
{							\
	gpio_interrupt(wui_int);			\
}

/* If we need to handle the other type interrupts except GPIO, add code here */
void __gpio_wk0efgh_interrupt(void)
{
#ifdef CONFIG_HOSTCMD_X86
	/* Pending bit 7 or 6 or 5? */
	if (IS_BIT_SET(NPCX_WKEN(MIWU_TABLE_0 , MIWU_GROUP_5), 6) &&
	    IS_BIT_SET(NPCX_WKPND(MIWU_TABLE_0 , MIWU_GROUP_5), 6)) {
		/* Disable host wake-up */
		CLEAR_BIT(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_5), 6);
		/* Clear pending bit of WUI */
		SET_BIT(NPCX_WKPCL(MIWU_TABLE_0, MIWU_GROUP_5), 6);
	}
#ifdef CONFIG_HOSTCMD_ESPI
	else if (IS_BIT_SET(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_5), 5) &&
		 IS_BIT_SET(NPCX_WKPND(MIWU_TABLE_0, MIWU_GROUP_5), 5))
		espi_espirst_handler();
#else
	else if (IS_BIT_SET(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_5), 7) &&
		 IS_BIT_SET(NPCX_WKPND(MIWU_TABLE_0, MIWU_GROUP_5), 7))
		lpc_lreset_pltrst_handler();
#endif
	else
#endif
	{
		gpio_interrupt(WUI_INT(MIWU_TABLE_0, MIWU_GROUP_5));
		gpio_interrupt(WUI_INT(MIWU_TABLE_0, MIWU_GROUP_6));
		gpio_interrupt(WUI_INT(MIWU_TABLE_0, MIWU_GROUP_7));
		gpio_interrupt(WUI_INT(MIWU_TABLE_0, MIWU_GROUP_8));
	}
}

void __gpio_rtc_interrupt(void)
{
	/* Check pending bit 7 */
#ifdef CONFIG_HOSTCMD_RTC
	if (NPCX_WKPND(MIWU_TABLE_0, MIWU_GROUP_4) & 0x80) {
		/* Clear pending bit for WUI */
		SET_BIT(NPCX_WKPCL(MIWU_TABLE_0, MIWU_GROUP_4), 7);
		host_set_single_event(EC_HOST_EVENT_RTC);
		return;
	}
#endif
#if defined(CHIP_FAMILY_NPCX7) && defined(CONFIG_LOW_POWER_IDLE) && \
	(CONFIG_CONSOLE_UART == 1)
	/* Handle the interrupt from UART wakeup event */
	if (IS_BIT_SET(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_1), 6) &&
	    IS_BIT_SET(NPCX_WKPND(MIWU_TABLE_0, MIWU_GROUP_1), 6)) {
		/*
		 * Disable WKEN bit to avoid the other unnecessary interrupts
		 * from the coming data bits after the start bit. (Pending bit
		 * of CR_SIN is set when a high-to-low transaction occurs.)
		 */
		CLEAR_BIT(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_1), 6);
		/* Clear pending bit for WUI */
		SET_BIT(NPCX_WKPCL(MIWU_TABLE_0, MIWU_GROUP_1), 6);
		/* Notify the clock module that the console is in use. */
		clock_refresh_console_in_use();
		return;
	}
#endif
	gpio_interrupt(WUI_INT(MIWU_TABLE_0, MIWU_GROUP_1));
	gpio_interrupt(WUI_INT(MIWU_TABLE_0, MIWU_GROUP_4));
}

void __gpio_wk1h_interrupt(void)
{
#if defined(CHIP_FAMILY_NPCX7) && defined(CONFIG_LOW_POWER_IDLE) && \
	(CONFIG_CONSOLE_UART == 0)
	/* Handle the interrupt from UART wakeup event */
	if (IS_BIT_SET(NPCX_WKEN(MIWU_TABLE_1, MIWU_GROUP_8), 7) &&
	    IS_BIT_SET(NPCX_WKPND(MIWU_TABLE_1, MIWU_GROUP_8), 7)) {
		/*
		 * Disable WKEN bit to avoid the other unnecessary interrupts
		 * from the coming data bits after the start bit. (Pending bit
		 * of CR_SIN is set when a high-to-low transaction occurs.)
		 */
		CLEAR_BIT(NPCX_WKEN(MIWU_TABLE_1, MIWU_GROUP_8), 7);
		/* Clear pending bit for WUI */
		SET_BIT(NPCX_WKPCL(MIWU_TABLE_1, MIWU_GROUP_8), 7);
		/* Notify the clock module that the console is in use. */
		clock_refresh_console_in_use();
	} else
#endif
		gpio_interrupt(WUI_INT(MIWU_TABLE_1, MIWU_GROUP_8));
}

GPIO_IRQ_FUNC(__gpio_wk0b_interrupt, WUI_INT(MIWU_TABLE_0, MIWU_GROUP_2));
GPIO_IRQ_FUNC(__gpio_wk0c_interrupt, WUI_INT(MIWU_TABLE_0, MIWU_GROUP_3));
GPIO_IRQ_FUNC(__gpio_wk1a_interrupt, WUI_INT(MIWU_TABLE_1, MIWU_GROUP_1));
GPIO_IRQ_FUNC(__gpio_wk1b_interrupt, WUI_INT(MIWU_TABLE_1, MIWU_GROUP_2));
#ifndef HAS_TASK_KEYSCAN
/* Delcare GPIO irq functions for KSI pins if there's no keyboard scan task, */
GPIO_IRQ_FUNC(__gpio_wk1c_interrupt, WUI_INT(MIWU_TABLE_1, MIWU_GROUP_3));
#endif
GPIO_IRQ_FUNC(__gpio_wk1d_interrupt, WUI_INT(MIWU_TABLE_1, MIWU_GROUP_4));
GPIO_IRQ_FUNC(__gpio_wk1e_interrupt, WUI_INT(MIWU_TABLE_1, MIWU_GROUP_5));
GPIO_IRQ_FUNC(__gpio_wk1f_interrupt, WUI_INT(MIWU_TABLE_1, MIWU_GROUP_6));
GPIO_IRQ_FUNC(__gpio_wk1g_interrupt, WUI_INT(MIWU_TABLE_1, MIWU_GROUP_7));
#if defined(CHIP_FAMILY_NPCX7)
GPIO_IRQ_FUNC(__gpio_wk2fg_interrupt, WUI_INT(MIWU_TABLE_2, MIWU_GROUP_6));
#endif

DECLARE_IRQ(NPCX_IRQ_MTC_WKINTAD_0, __gpio_rtc_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_TWD_WKINTB_0,  __gpio_wk0b_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_WKINTC_0,      __gpio_wk0c_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_WKINTEFGH_0,   __gpio_wk0efgh_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_WKINTA_1,      __gpio_wk1a_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_WKINTB_1,      __gpio_wk1b_interrupt, 3);
#ifndef HAS_TASK_KEYSCAN
DECLARE_IRQ(NPCX_IRQ_KSI_WKINTC_1,  __gpio_wk1c_interrupt, 3);
#endif
DECLARE_IRQ(NPCX_IRQ_WKINTD_1,      __gpio_wk1d_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_WKINTE_1,      __gpio_wk1e_interrupt, 3);
#ifdef CONFIG_HOSTCMD_SPS
/*
 * HACK: Make CS GPIO P2 to improve SHI reliability.
 * TODO: Increase CS-assertion-to-transaction-start delay on host to
 * accommodate P3 CS interrupt.
 */
DECLARE_IRQ(NPCX_IRQ_WKINTF_1,      __gpio_wk1f_interrupt, 2);
#else
DECLARE_IRQ(NPCX_IRQ_WKINTF_1,      __gpio_wk1f_interrupt, 3);
#endif
DECLARE_IRQ(NPCX_IRQ_WKINTG_1,      __gpio_wk1g_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_WKINTH_1,      __gpio_wk1h_interrupt, 3);
#if defined(CHIP_FAMILY_NPCX7)
DECLARE_IRQ(NPCX_IRQ_WKINTFG_2,     __gpio_wk2fg_interrupt, 3);
#endif

#undef GPIO_IRQ_FUNC
#if DEBUG_GPIO && defined(CONFIG_LOW_POWER_IDLE)
/*
 * Command used to disable input buffer of gpios one by one to
 * investigate power consumption
 */
static int command_gpiodisable(int argc, char **argv)
{
	uint8_t i;
	uint8_t offset;
	const uint8_t non_isr_gpio_num = GPIO_COUNT - GPIO_IH_COUNT;
	const struct gpio_info *g_list;
	int flags;
	static uint8_t idx = 0;
	int num = -1;
	int enable;
	char *e;

	if (argc == 2) {
		if (!strcasecmp(argv[1], "info")) {
			offset = idx + GPIO_IH_COUNT;
			g_list = gpio_list + offset;
			flags = g_list->flags;

			ccprintf("Total GPIO declaration: %d\n", GPIO_COUNT);
			ccprintf("Total Non-ISR GPIO declaration: %d\n",
						non_isr_gpio_num);
			ccprintf("Next GPIO Num to check by ");
			ccprintf("\"gpiodisable next\"\n");
			ccprintf("  offset: %d\n", offset);
			ccprintf("  current GPIO name: %s\n", g_list->name);
			ccprintf("  current GPIO flags: 0x%08x\n", flags);
			return EC_SUCCESS;
		}
		/* List all non-ISR GPIOs in gpio.inc */
		if (!strcasecmp(argv[1], "list")) {
			for (i = GPIO_IH_COUNT; i < GPIO_COUNT; i++)
				ccprintf("%d: %s\n", i, gpio_get_name(i));
			return EC_SUCCESS;
		}

		if (!strcasecmp(argv[1], "next")) {
			while (1) {
				if (idx == non_isr_gpio_num)
					break;

				offset = idx + GPIO_IH_COUNT;
				g_list = gpio_list + offset;
				flags = g_list->flags;
				ccprintf("current GPIO : %d %s --> ",
							offset, g_list->name);
				if (gpio_is_i2c_pin(offset)) {
					ccprintf("Ignore I2C pin!\n");
					idx++;
					continue;
				} else if (flags & GPIO_SEL_1P8V) {
					ccprintf("Ignore 1v8 pin!\n");
					idx++;
					continue;
				} else {
					if ((flags & GPIO_INPUT) ||
						    (flags & GPIO_OPEN_DRAIN)) {
						ccprintf("Disable WKINEN!\n");
						gpio_enable_wake_up_input(
								offset, 0);
						idx++;
						break;
					}
					ccprintf("Not Input or OpenDrain\n");
					idx++;
					continue;
				}
			};
			if (idx == non_isr_gpio_num) {
				ccprintf("End of GPIO list, reset index!\n");
				idx = 0;
			};
			return EC_SUCCESS;
		}
	}
	if (argc == 3) {
		num = strtoi(argv[1], &e, 0);
		if (*e || num < GPIO_IH_COUNT || num >= GPIO_COUNT)
			return EC_ERROR_PARAM1;

		if (parse_bool(argv[2], &enable))
			gpio_enable_wake_up_input(num, enable ? 1 : 0);
		else
			return EC_ERROR_PARAM2;

		return EC_SUCCESS;
	}
	return EC_ERROR_INVAL;
}
DECLARE_CONSOLE_COMMAND(gpiodisable, command_gpiodisable,
		"info/list/next/<num> on|off",
		"Disable GPIO input buffer to investigate power consumption");
#endif
