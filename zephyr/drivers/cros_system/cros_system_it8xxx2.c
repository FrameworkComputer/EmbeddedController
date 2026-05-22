/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/cros_system.h"
#include "gpio/gpio_int.h"
#include "system.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>

#include <soc.h>
#include <soc/ite_it8xxx2/reg_def_cros.h>

#define DT_DRV_COMPAT cros_ec_cros_system

LOG_MODULE_REGISTER(cros_system, LOG_LEVEL_ERR);

#define GCTRL_IT8XXX2_REG_BASE \
	((struct gctrl_it8xxx2_regs *)DT_REG_ADDR(DT_NODELABEL(gctrl)))

#define WDT_IT8XXX2_REG_BASE \
	((struct wdt_it8xxx2_regs *)DT_REG_ADDR(DT_NODELABEL(twd0)))

const char *cros_system_chip_vendor(void)
{
	return "ite";
}

static uint32_t system_get_chip_id(void)
{
	struct gctrl_it8xxx2_regs *const gctrl_base = GCTRL_IT8XXX2_REG_BASE;

	return (gctrl_base->GCTRL_ECHIPID1 << 16) |
	       (gctrl_base->GCTRL_ECHIPID2 << 8) | gctrl_base->GCTRL_ECHIPID3;
}

static uint8_t system_get_chip_version(void)
{
	struct gctrl_it8xxx2_regs *const gctrl_base = GCTRL_IT8XXX2_REG_BASE;

	/* bit[3-0], chip version */
	return gctrl_base->GCTRL_ECHIPVER & 0x0F;
}

const char *cros_system_chip_name(void)
{
	static char buf[8] = { 'i', 't' };
	uint32_t chip_id = system_get_chip_id();
	int num = 4;

	for (int n = 2; num >= 0; n++, num--)
		snprintf(buf + n, (sizeof(buf) - n), "%x",
			 chip_id >> (num * 4) & 0xF);

	return buf;
}

const char *cros_system_chip_revision(void)
{
	static char buf[3];
	uint8_t rev = system_get_chip_version();

	snprintf(buf, sizeof(buf), "%cx", rev + 'a');

	return buf;
}

int cros_system_get_reset_cause(void)
{
	struct gctrl_it8xxx2_regs *const gctrl_base = GCTRL_IT8XXX2_REG_BASE;
	uint8_t last_reset_source = gctrl_base->GCTRL_RSTS & IT8XXX2_GCTRL_LRS;
	uint8_t raw_reset_cause2 =
		gctrl_base->GCTRL_SPCTRL4 &
		(IT8XXX2_GCTRL_LRSIWR | IT8XXX2_GCTRL_LRSIPWRSWTR |
		 IT8XXX2_GCTRL_LRSIPGWR);

	/* Clear reset cause. */
	gctrl_base->GCTRL_RSTS |= IT8XXX2_GCTRL_LRS;
	gctrl_base->GCTRL_SPCTRL4 |=
		(IT8XXX2_GCTRL_LRSIWR | IT8XXX2_GCTRL_LRSIPWRSWTR |
		 IT8XXX2_GCTRL_LRSIPGWR);

	if (last_reset_source & IT8XXX2_GCTRL_IWDTR) {
		return WATCHDOG_RST;
	}
	if (raw_reset_cause2 & IT8XXX2_GCTRL_LRSIWR) {
		/*
		 * We can't differentiate between power-on and reset pin because
		 * LRSIWR is set on both ~WRST assertion and power-on, and LRS
		 * is either 0 or 1 in both cases.
		 *
		 * Some EC code paths care about only one of these options,
		 * so we force both causes to be reported (via
		 * system_set_reset_flags() behind our caller's back) even
		 * though in reality it had to be only one of them because
		 * being unable to report a hard reset breaks some
		 * functionality, as would being unable to report power-on
		 * reset.
		 */
		system_set_reset_flags(EC_RESET_FLAG_RESET_PIN);
		return POWERUP;
	}
	return UNKNOWN_RST;
}

static int cros_system_it8xxx2_init(void)
{
	struct gctrl_it8xxx2_regs *const gctrl_base = GCTRL_IT8XXX2_REG_BASE;

	/* System triggers a soft reset by default (command: reboot). */
	gctrl_base->GCTRL_ETWDUARTCR &= ~IT8XXX2_GCTRL_ETWD_HW_RST_EN;

	return 0;
}

int cros_system_soc_reset(void)
{
	struct gctrl_it8xxx2_regs *const gctrl_base = GCTRL_IT8XXX2_REG_BASE;
	struct wdt_it8xxx2_regs *const wdt_base = WDT_IT8XXX2_REG_BASE;
	uint32_t chip_reset_flags = chip_read_reset_flags();

	/* Disable interrupts to avoid task swaps during reboot. */
	interrupt_disable_all();

	if (chip_reset_flags & (EC_RESET_FLAG_HARD | EC_RESET_FLAG_HIBERNATE))
		gctrl_base->GCTRL_ETWDUARTCR |= IT8XXX2_GCTRL_ETWD_HW_RST_EN;

	/*
	 * Writing invalid key to watchdog module triggers a soft or hardware
	 * reset. It depends on the setting of bit0 at ETWDUARTCR register.
	 */
	wdt_base->ETWCFG |= IT8XXX2_WDT_EWDKEYEN;
	wdt_base->EWDKEYR = 0x00;

	/* Spin and wait for reboot */
	while (1)
		;

	/* Should never return */
	return 0;
}

/*
 * Fake wake ISR handler, needed for pins that do not have a handler.
 */
void wake_isr(enum gpio_signal signal)
{
}

static int system_it8xxx2_hibernate_by_deep_doze(uint32_t seconds,
						 uint32_t microseconds)
{
	if (seconds || microseconds) {
		/*
		 * Convert milliseconds(or at least 1 ms) to 32 Hz
		 * free run timer count for hibernate.
		 */
		uint32_t c =
			(seconds * 1000 + microseconds / 1000 + 1) * 32 / 1000;

		/* Enable a 32-bit timer and clock source is 32 Hz */
		/* Disable external timer x */
		IT8XXX2_EXT_CTRLX(FREE_RUN_TIMER) &= ~IT8XXX2_EXT_ETXEN;
		irq_disable(FREE_RUN_TIMER_IRQ);
		IT8XXX2_EXT_PSRX(FREE_RUN_TIMER) = EXT_PSR_32;
		IT8XXX2_EXT_CNTX(FREE_RUN_TIMER) = c & FREE_RUN_TIMER_MAX_CNT;
		/* Enable and re-start external timer x */
		IT8XXX2_EXT_CTRLX(FREE_RUN_TIMER) |=
			(IT8XXX2_EXT_ETXEN | IT8XXX2_EXT_ETXRST);
		irq_enable(FREE_RUN_TIMER_IRQ);
	}

#ifdef CONFIG_PLATFORM_EC_HIBERNATE_WAKE_PINS

/*
 * Get the interrupt DTS node for this wakeup pin
 */
#define WAKEUP_INT(id, prop, idx) DT_PHANDLE_BY_IDX(id, prop, idx)

/*
 * Get the named-gpio node for this wakeup pin by reading the
 * irq-gpio property from the interrupt node.
 */
#define WAKEUP_NGPIO(id, prop, idx) \
	DT_PHANDLE(WAKEUP_INT(id, prop, idx), irq_pin)

/*
 * Reset and re-enable interrupts on this wake pin.
 */
#define WAKEUP_SETUP(id, prop, idx)                                     \
	do {                                                            \
		gpio_pin_configure_dt(                                  \
			GPIO_DT_FROM_NODE(WAKEUP_NGPIO(id, prop, idx)), \
			GPIO_INPUT);                                    \
		gpio_enable_dt_interrupt(                               \
			GPIO_INT_FROM_NODE(WAKEUP_INT(id, prop, idx))); \
	} while (0);

	/*
	 * For all the wake-pins, re-init the GPIO and re-enable the interrupt.
	 */
	DT_FOREACH_PROP_ELEM(SYSTEM_DT_NODE_HIBERNATE_CONFIG, wakeup_irqs,
			     WAKEUP_SETUP);

#undef WAKEUP_INT
#undef WAKEUP_NGPIO
#undef WAKEUP_SETUP

#endif /* CONFIG_PLATFORM_EC_HIBERNATE_WAKE_PINS */

	/* EC sleep mode */
	chip_pll_ctrl(CHIP_PLL_SLEEP);

	/* Chip sleep and wait timer wake it up */
	__asm__ volatile("wfi");

	/* Reset EC when wake up from sleep mode (system hibernate) */
	system_reset(SYSTEM_RESET_HIBERNATE);

	return 0;
}

#ifdef CONFIG_PLATFORM_EC_HIBERNATE_ELPM
#define ELPM_NODE DT_INST(0, ite_it8xxx2_power_elpm)
#define ELPM_BASE_ADDR DT_REG_ADDR(ELPM_NODE)

#define ELPMF1_WAKE_UP_CTRL3 0xF1
#define XLPINS_BYPASS_EN BIT(2)
#define FIRMWARE_CTRL_EN BIT(1)
#define FIRMWARE_CTRL_OUTPUT_H BIT(0)

#define ELPMF2_XLPIN_LATCH_STS 0xF2
#define ELPMF3_XLPIN_RISING_EDGE_STS 0xF3
#define ELPMF4_XLPIN_FALLING_EDGE_STS 0xF4
#define ELPMF5_XLPIN_INPUT_ENABLE 0xF5
#define ELPMF7_XLPIN_POLARITY_CTRL 0xF7
#define ELPMF8_XLPIN_LATCH_EN 0xF8

PINCTRL_DT_DEFINE(ELPM_NODE);

#define XLPIN_POL_ENTRY(child) \
	[DT_REG_ADDR(child)] = DT_ENUM_IDX(child, polarity),
#define XLPIN_LATCH_ENTRY(child) \
	[DT_REG_ADDR(child)] = DT_PROP_OR(child, latch_enable, false),

enum elpm_xlpin_polarity {
	ELPM_POL_DEFAULT = 0, /* default, disable xlpin */
	ELPM_POL_LOW, /* low falling */
	ELPM_POL_HIGH, /* high rising */
};

static int system_it8xxx2_hibernate_by_elpm(void)
{
	const struct pinctrl_dev_config *elpm_pcfg =
		PINCTRL_DT_DEV_CONFIG_GET(ELPM_NODE);
	const enum elpm_xlpin_polarity xlpin_polarities[] = { DT_FOREACH_CHILD(
		ELPM_NODE, XLPIN_POL_ENTRY) };
	const bool xlpin_latches[] = { DT_FOREACH_CHILD(ELPM_NODE,
							XLPIN_LATCH_ENTRY) };
	uint8_t wake_up_ctrl3;
	uint8_t xlpins_enable = 0, polarity_ctrl_val = 0,
		xlpins_latch_enable = 0;
	int ret;

	/* apply xlpins pinctrl */
	ret = pinctrl_apply_state(elpm_pcfg, PINCTRL_STATE_DEFAULT);
	if (ret) {
		return ret;
	}

	for (uint8_t i = 0; i < ARRAY_SIZE(xlpin_polarities); i++) {
		switch (xlpin_polarities[i]) {
		case ELPM_POL_DEFAULT:
			/* ignored as xlpin[i] is disabled */
			break;
		case ELPM_POL_HIGH:
			polarity_ctrl_val |= BIT(i);
			__fallthrough;
		case ELPM_POL_LOW:
			xlpins_enable |= BIT(i);
			if (xlpin_latches[i]) {
				xlpins_latch_enable |= BIT(i);
			}
			break;
		default:
			/* unknown polarity control setting */
			return -ENOTSUP;
		};
	}
	if (xlpins_enable == 0) {
		/* no xlpins are enabled */
		return -EINVAL;
	}

	/* write 1 to clear xlpin latch status before enabling them */
	sys_write8(xlpins_latch_enable,
		   ELPM_BASE_ADDR + ELPMF2_XLPIN_LATCH_STS);
	sys_write8(xlpins_latch_enable, ELPM_BASE_ADDR + ELPMF8_XLPIN_LATCH_EN);

	/* enable bypass mode (non-debounced) */
	wake_up_ctrl3 = sys_read8(ELPM_BASE_ADDR + ELPMF1_WAKE_UP_CTRL3);
	wake_up_ctrl3 |= XLPINS_BYPASS_EN;
	sys_write8(wake_up_ctrl3, ELPM_BASE_ADDR + ELPMF1_WAKE_UP_CTRL3);

	/* clear xlpins status before enabling them */
	sys_write8(xlpins_enable,
		   ELPM_BASE_ADDR + ELPMF3_XLPIN_RISING_EDGE_STS);
	sys_write8(xlpins_enable,
		   ELPM_BASE_ADDR + ELPMF4_XLPIN_FALLING_EDGE_STS);

	/* configure xlpins polarity and enable them */
	sys_write8(polarity_ctrl_val,
		   ELPM_BASE_ADDR + ELPMF7_XLPIN_POLARITY_CTRL);
	sys_write8(xlpins_enable, ELPM_BASE_ADDR + ELPMF5_XLPIN_INPUT_ENABLE);

	/* Disable firmware control mode so that the EC chip’s main
	 * power (VSTBY) turns off, entering hibernate mode. The chip is only
	 * woken upon the assertion of one of configured XLPIN wake-up pins.
	 */
	wake_up_ctrl3 &= ~(FIRMWARE_CTRL_EN | FIRMWARE_CTRL_OUTPUT_H);
	sys_write8(wake_up_ctrl3, ELPM_BASE_ADDR + ELPMF1_WAKE_UP_CTRL3);

	return 0;
}
#endif /* CONFIG_PLATFORM_EC_HIBERNATE_ELPM */

int cros_system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	struct wdt_it8xxx2_regs *const wdt_base = WDT_IT8XXX2_REG_BASE;

	/* Disable all interrupts. */
	interrupt_disable_all();

	/* Save and disable interrupts */
	ite_intc_save_and_disable_interrupts();

	/* bit5: watchdog is disabled. */
	wdt_base->ETWCTRL |= IT8XXX2_WDT_EWDSCEN;

	/*
	 * Setup GPIOs for hibernate. On some boards, it's possible that this
	 * may not return at all. On those boards, power to the EC is likely
	 * being turn off entirely.
	 */
	if (board_hibernate_late) {
		/*
		 * Set reset flag in case board_hibernate_late() doesn't
		 * return.
		 */
		chip_save_reset_flags(EC_RESET_FLAG_HIBERNATE);
		board_hibernate_late();
	}

#ifdef CONFIG_PLATFORM_EC_HIBERNATE_ELPM
	if (seconds || microseconds) {
		/* TODO: the ELPM time-based wake-up is currently unsupported;
		 * ITE will add this feature in future update.
		 */
		LOG_ERR("unsupported elpm time-based wake-up, hibernating until"
			"wake pin asserted");
	}
	return system_it8xxx2_hibernate_by_elpm();
#endif

	return system_it8xxx2_hibernate_by_deep_doze(seconds, microseconds);
}

SYS_INIT(cros_system_it8xxx2_init, PRE_KERNEL_1,
	 CONFIG_CROS_SYSTEM_INIT_PRIORITY);

#if CONFIG_CROS_SYSTEM_INIT_PRIORITY >= \
	CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY
#error "CROS_SYSTEM must initialize before the SYSTEM_PRE initialization"
#endif
