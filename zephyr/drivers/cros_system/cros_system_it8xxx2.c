/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT ite_it8xxx2_gctrl

#include "drivers/cros_system.h"
#include "gpio/gpio_int.h"
#include "system.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <soc.h>
#include <soc/ite_it8xxx2/reg_def_cros.h>

LOG_MODULE_REGISTER(cros_system, LOG_LEVEL_ERR);

#define GCTRL_IT8XXX2_REG_BASE \
	((struct gctrl_it8xxx2_regs *)DT_INST_REG_ADDR(0))

#define WDT_IT8XXX2_REG_BASE \
	((struct wdt_it8xxx2_regs *)DT_REG_ADDR(DT_NODELABEL(twd0)))

static const char *cros_system_it8xxx2_get_chip_vendor(const struct device *dev)
{
	ARG_UNUSED(dev);

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

static const char *cros_system_it8xxx2_get_chip_name(const struct device *dev)
{
	ARG_UNUSED(dev);

	static char buf[8] = { 'i', 't' };
	uint32_t chip_id = system_get_chip_id();
	int num = 4;

	for (int n = 2; num >= 0; n++, num--)
		snprintf(buf + n, (sizeof(buf) - n), "%x",
			 chip_id >> (num * 4) & 0xF);

	return buf;
}

static const char *
cros_system_it8xxx2_get_chip_revision(const struct device *dev)
{
	ARG_UNUSED(dev);

	static char buf[3];
	uint8_t rev = system_get_chip_version();

	snprintf(buf, sizeof(buf), "%cx", rev + 'a');

	return buf;
}

static int cros_system_it8xxx2_get_reset_cause(const struct device *dev)
{
	ARG_UNUSED(dev);
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

static int cros_system_it8xxx2_init(const struct device *dev)
{
	struct gctrl_it8xxx2_regs *const gctrl_base = GCTRL_IT8XXX2_REG_BASE;

	/* System triggers a soft reset by default (command: reboot). */
	gctrl_base->GCTRL_ETWDUARTCR &= ~IT8XXX2_GCTRL_ETWD_HW_RST_EN;

	return 0;
}

static int cros_system_it8xxx2_soc_reset(const struct device *dev)
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

static int cros_system_it8xxx2_hibernate(const struct device *dev,
					 uint32_t seconds,
					 uint32_t microseconds)
{
	struct wdt_it8xxx2_regs *const wdt_base = WDT_IT8XXX2_REG_BASE;

	/* Disable all interrupts. */
	interrupt_disable_all();

	/* Save and disable interrupts */
	if (IS_ENABLED(CONFIG_ITE_IT8XXX2_INTC))
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

#if DT_NODE_EXISTS(SYSTEM_DT_NODE_HIBERNATE_CONFIG)

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

#endif

	/* EC sleep mode */
	chip_pll_ctrl(CHIP_PLL_SLEEP);

	/* Chip sleep and wait timer wake it up */
	__asm__ volatile("wfi");

	/* Reset EC when wake up from sleep mode (system hibernate) */
	system_reset(SYSTEM_RESET_HIBERNATE);

	return 0;
}

static const struct cros_system_driver_api cros_system_driver_it8xxx2_api = {
	.get_reset_cause = cros_system_it8xxx2_get_reset_cause,
	.soc_reset = cros_system_it8xxx2_soc_reset,
	.hibernate = cros_system_it8xxx2_hibernate,
	.chip_vendor = cros_system_it8xxx2_get_chip_vendor,
	.chip_name = cros_system_it8xxx2_get_chip_name,
	.chip_revision = cros_system_it8xxx2_get_chip_revision,
};

#if CONFIG_CROS_SYSTEM_IT8XXX2_INIT_PRIORITY >= \
	CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY
#error "CROS_SYSTEM must initialize before the SYSTEM_PRE initialization"
#endif
DEVICE_DEFINE(cros_system_it8xxx2_0, "CROS_SYSTEM", cros_system_it8xxx2_init,
	      NULL, NULL, NULL, PRE_KERNEL_1,
	      CONFIG_CROS_SYSTEM_IT8XXX2_INIT_PRIORITY,
	      &cros_system_driver_it8xxx2_api);
