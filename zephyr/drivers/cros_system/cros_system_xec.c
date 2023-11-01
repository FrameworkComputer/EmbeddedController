/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "bbram.h"
#include "gpio/gpio_int.h"
#include "system.h"
#include "system_chip.h"

#include <zephyr/drivers/interrupt_controller/intc_mchp_xec_ecia.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <cmsis_core.h>
#include <drivers/cros_system.h>
#include <soc.h>
#include <soc/microchip_xec/reg_def_cros.h>

LOG_MODULE_REGISTER(cros_system, LOG_LEVEL_ERR);

/* Modules Map */
#define STRUCT_ADC_REG_BASE_ADDR \
	((struct adc_regs *)(DT_REG_ADDR(DT_NODELABEL(adc0))))

#define STRUCT_UART_REG_BASE_ADDR \
	((struct uart_regs *)(DT_REG_ADDR(DT_NODELABEL(uart0))))

#define STRUCT_ECS_REG_BASE_ADDR \
	((struct ecs_regs *)(DT_REG_ADDR(DT_NODELABEL(ecs))))

#define STRUCT_TIMER4_REG_BASE_ADDR \
	((struct btmr_regs *)(DT_REG_ADDR(DT_NODELABEL(timer4))))

#define STRUCT_ESPI_REG_BASE_ADDR \
	((struct espi_iom_regs *)(DT_REG_ADDR(DT_NODELABEL(espi0))))

#define STRUCT_KBD_REG_BASE_ADDR \
	((struct kscan_regs *)(DT_REG_ADDR(DT_NODELABEL(cros_kb_raw))))

#define STRUCT_QMSPI_REG_BASE_ADDR \
	((struct qmspi_regs *)(DT_REG_ADDR(DT_NODELABEL(spi0))))

#define STRUCT_PWM_REG_BASE_ADDR \
	((struct pwm_regs *)(DT_REG_ADDR(DT_NODELABEL(pwm0))))

#define STRUCT_TACH_REG_BASE_ADDR \
	((struct tach_regs *)(DT_REG_ADDR(DT_NODELABEL(tach0))))

#define STRUCT_HTMR0_REG_BASE_ADDR \
	((struct htmr_regs *)(DT_REG_ADDR(DT_NODELABEL(hibtimer0))))

/* Driver config */
struct cros_system_xec_config {
	/* hardware module base address */
	uintptr_t base_pcr;
	uintptr_t base_vbr;
	uintptr_t base_wdog;
};

/* Driver data */
struct cros_system_xec_data {
	int reset; /* reset cause */
};

/* Driver convenience defines */
#define DRV_CONFIG(dev) ((const struct cros_system_xec_config *)(dev)->config)
#define DRV_DATA(dev) ((struct cros_system_xec_data *)(dev)->data)

#define HAL_PCR_INST(dev) (struct pcr_regs *)(DRV_CONFIG(dev)->base_pcr)
#define HAL_VBATR_INST(dev) (struct vbatr_regs *)(DRV_CONFIG(dev)->base_vbr)
#define HAL_WDOG_INST(dev) (struct wdt_regs *)(DRV_CONFIG(dev)->base_wdog)

/* Get saved reset flag address in battery-backed ram */
#define BBRAM_SAVED_RESET_FLAG_ADDR                     \
	(DT_REG_ADDR(DT_INST(0, microchip_xec_bbram)) + \
	 BBRAM_REGION_OFFSET(offset)

/* Soc specific system local functions */
static int system_xec_watchdog_stop(void)
{
	if (IS_ENABLED(CONFIG_WATCHDOG)) {
		const struct device *wdt_dev =
			DEVICE_DT_GET(DT_NODELABEL(wdog));
		if (!device_is_ready(wdt_dev)) {
			LOG_ERR("Error: device %s is not ready", wdt_dev->name);
			return -ENODEV;
		}

		wdt_disable(wdt_dev);
	}

	return 0;
}

static const char *cros_system_xec_get_chip_vendor(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "MCHP";
}

/* TODO - return specific chip name such as MEC1727 or MEC1723 */
static const char *cros_system_xec_get_chip_name(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "MEC172X";
}

/* TODO return chip revision from HW as an ASCII string */
static const char *cros_system_xec_get_chip_revision(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "B0";
}

static int cros_system_xec_get_reset_cause(const struct device *dev)
{
	struct cros_system_xec_data *data = DRV_DATA(dev);

	return data->reset;
}

/* MCHP TODO check and verify this logic for all corner cases:
 * Someone doing ARM Vector Reset insead of SYSRESETREQ or HW reset.
 * Does NRESETIN# status get set also on power on from no power state?
 */
static int cros_system_xec_init(const struct device *dev)
{
	struct vbatr_regs *vbr = HAL_VBATR_INST(dev);
	struct cros_system_xec_data *data = DRV_DATA(dev);
	uint32_t pfsr = vbr->PFRS;

	if (IS_BIT_SET(pfsr, MCHP_VBATR_PFRS_WDT_POS)) {
		data->reset = WATCHDOG_RST;
		vbr->PFRS = BIT(MCHP_VBATR_PFRS_WDT_POS);
	} else if (IS_BIT_SET(pfsr, MCHP_VBATR_PFRS_SYSRESETREQ_POS)) {
		data->reset = DEBUG_RST;
		vbr->PFRS = BIT(MCHP_VBATR_PFRS_SYSRESETREQ_POS);
	} else if (IS_BIT_SET(pfsr, MCHP_VBATR_PFRS_RESETI_POS)) {
		data->reset = VCC1_RST_PIN;
	} else {
		data->reset = POWERUP;
	}

	return 0;
}

noreturn static int cros_system_xec_soc_reset(const struct device *dev)
{
	struct pcr_regs *const pcr = HAL_PCR_INST(dev);

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable_all();
	/* Stop the watchdog */
	system_xec_watchdog_stop();

	/* Trigger chip reset */
	pcr->SYS_RST |= MCHP_PCR_SYS_RESET_NOW;
	/* Wait for the soc reset */
	while (1)
		;
	/* should never return */
	/* return 0; */
}

/* Configure wakeup GPIOs in hibernate (from hibernate-wake-pins). */
static void system_xec_set_wakeup_gpios_before_hibernate(void)
{
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
}

/**
 * initialization of Hibernation timer 0
 * GIRQ=23, aggregator bit = 16, Direct NVIC = 112
 * NVIC direct connect interrupts are used for all peripherals
 * (exception GPIO's)
 */
static void htimer_init(void)
{
	struct htmr_regs *htmr0 = STRUCT_HTMR0_REG_BASE_ADDR;

	/* disable HT0 at beginning */
	htmr0->PRLD = 0U;
	mchp_soc_ecia_girq_src_clr(MCHP_GIRQ23_ID, MCHP_HTMR_0_GIRQ_POS);
	mchp_soc_ecia_girq_src_en(MCHP_GIRQ23_ID, MCHP_HTMR_0_GIRQ_POS);

	/* enable NVIC interrupt for HT0 */
	irq_enable(MCHP_HTMR_0_GIRQ_NVIC_DIRECT);
}

/**
 * Use hibernate module to set up an htimer interrupt at a given
 * time from now
 *
 * @param seconds      Number of seconds before htimer interrupt
 * @param microseconds Number of microseconds before htimer interrupt
 * @note hibernation timer input clock is 32.768KHz.
 */
static void system_set_htimer_alarm(uint32_t seconds, uint32_t microseconds)
{
	uint32_t hcnt, ns;
	uint8_t hctrl;
	struct htmr_regs *htmr0 = STRUCT_HTMR0_REG_BASE_ADDR;

	/* disable HT0 */
	htmr0->PRLD = 0U;

	if (microseconds > 1000000ul) {
		ns = (microseconds / 1000000ul);
		microseconds %= 1000000ul;
		if ((0xfffffffful - seconds) > ns)
			seconds += ns;
		else
			seconds = 0xfffffffful;
	}

	/*
	 * Hibernation timer input clock is 32.768KHz.
	 * Control register bit[0] selects the divider.
	 * If bit[0] is 0, divide by 1 for 30.5 us per LSB for a maximum of
	 * 65535 * 30.5 us = 1998817.5 us or 32.786 counts per second
	 * If bit[0] is 1, divide by 4096 for 0.125 s per LSB for a maximum
	 * of ~2 hours, 65535 * 0.125 s ~ 8192 s = 2.27 hours
	 */
	if (seconds > 1) {
		hcnt = (seconds << 3); /* divide by 0.125 */
		if (hcnt > 0xfffful)
			hcnt = 0xfffful;
		hctrl = 1;
	} else {
		/*
		 * approximate(~2% error) as seconds is 0 or 1
		 * seconds / 30.5e-6 + microseconds / 30.5
		 */
		hcnt = (seconds << 15) + (microseconds >> 5) +
		       (microseconds >> 10);
		hctrl = 0;
	}

	htmr0->CTRL = hctrl;
	htmr0->PRLD = hcnt;
}

/* Put the EC in hibernate (lowest EC power state). */
noreturn static int cros_system_xec_hibernate(const struct device *dev,
					      uint32_t seconds,
					      uint32_t microseconds)
{
	struct pcr_regs *const pcr = HAL_PCR_INST(dev);
#ifdef CONFIG_ADC_XEC_V2
	struct adc_regs *adc0 = STRUCT_ADC_REG_BASE_ADDR;
#endif
#ifdef CONFIG_UART_XEC
	struct uart_regs *uart0 = STRUCT_UART_REG_BASE_ADDR;
#endif
	struct ecs_regs *ecs = STRUCT_ECS_REG_BASE_ADDR;
	struct btmr_regs *btmr4 = STRUCT_TIMER4_REG_BASE_ADDR;
	struct espi_iom_regs *espi0 = STRUCT_ESPI_REG_BASE_ADDR;
#ifdef CONFIG_CROS_KB_RAW_XEC
	struct kscan_regs *kbd = STRUCT_KBD_REG_BASE_ADDR;
#endif
	struct qmspi_regs *qmspi0 = STRUCT_QMSPI_REG_BASE_ADDR;
#if defined(CONFIG_PWM_XEC)
	struct pwm_regs *pwm0 = STRUCT_PWM_REG_BASE_ADDR;
#endif
#if defined(CONFIG_TACH_XEC)
	struct tach_regs *tach0 = STRUCT_TACH_REG_BASE_ADDR;
#endif
	struct ecia_regs *ecia = (struct ecia_regs *)(ECIA_BASE_ADDR);
	int i;

	/* Disable interrupt first */
	interrupt_disable_all();
	/* Stop the watchdog */
	system_xec_watchdog_stop();

	/* Disable all individaul block interrupt and source */
	for (i = 0; i < MCHP_GIRQ_IDX_MAX; ++i) {
		ecia->GIRQ[i].EN_CLR = 0xffffffff;
		ecia->GIRQ[i].SRC = 0xffffffff;
	}

	/* Disable and clear all NVIC interrupt pending */
	for (i = 0; i < MCHP_MAX_NVIC_EXT_INPUTS; ++i) {
		mchp_xec_ecia_nvic_clr_pend(i);
	}

	/* Disable blocks */
#ifdef CONFIG_ADC_XEC_V2
	/* Disable ADC */
	adc0->CONTROL &= ~(MCHP_ADC_CTRL_ACTV);
#endif
	/* Disable eSPI */
	espi0->ACTV &= ~0x01;
#ifdef CONFIG_CROS_KB_RAW_XEC
	/* Disable Keyboard Scanner */
	kbd->KSO_SEL &= ~(MCHP_KSCAN_KSO_EN);
#endif
#ifdef CONFIG_I2C
	/* Disable SMB / I2C */
	for (i = 0; i < MCHP_I2C_SMB_INSTANCES; i++) {
		uint32_t addr =
			MCHP_I2C_SMB_BASE_ADDR(i) + MCHP_I2C_SMB_CFG_OFS;
		uint32_t regval = sys_read32(addr);

		sys_write32(regval & ~(MCHP_I2C_SMB_CFG_ENAB), addr);
	}
#endif
	/* Disable QMSPI */
	qmspi0->MODE &= ~MCHP_QMSPI_M_ACTIVATE;
#if defined(CONFIG_PWM_XEC)
	/* Disable PWM0 */
	pwm0->CONFIG &= ~MCHP_PWM_CFG_ENABLE;
#endif
#if defined(CONFIG_TACH_XEC)
	/* Disable TACH0 */
	tach0->CONTROL &= ~MCHP_TACH_CTRL_EN;
#endif
#if defined(CONFIG_TACH_XEC) || defined(CONFIG_PWM_XEC)
	/* This low-speed clock derived from the 48MHz clock domain is used as
	 * a time base for PWMs and TACHs
	 * Set SLOW_CLOCK_DIVIDE = CLKOFF to save additional power
	 */
	pcr->SLOW_CLK_CTRL &=
		(~MCHP_PCR_SLOW_CLK_CTRL_100KHZ & MCHP_PCR_SLOW_CLK_CTRL_MASK);
#endif
	/* Disable timers - 32bit timer 0 */
	btmr4->CTRL &= ~MCHP_BTMR_CTRL_ENABLE;
	/*
	 * Give the board a chance to do any late stage hibernation work.  This
	 * is likely going to configure GPIOs for hibernation.  On some boards,
	 * it's possible that this may not return at all.  On those boards,
	 * power to the EC is likely being turn off entirely.
	 */
	if (board_hibernate_late) {
		board_hibernate_late();
	}

	/* Setup wakeup GPIOs for hibernate */
	system_xec_set_wakeup_gpios_before_hibernate();
	/* Init htimer and enable interrupt if times are not 0 */
	if (seconds || microseconds) {
		htimer_init();
		system_set_htimer_alarm(seconds, microseconds);
	}

#ifdef CONFIG_UART_XEC
	/* Disable UART0 */
	/* Flush console before hibernating */
	cflush();
	uart0->ACTV &= ~(MCHP_UART_LD_ACTIVATE);
#endif

	/* Disable JATG and RTM */
	ecs->DEBUG_CTRL = 0;
	ecs->ETM_CTRL = 0;

	/*
	 * Set sleep state
	 * arm sleep state to trigger on next WFI
	 */
	pcr->SYS_SLP_CTRL |= MCHP_PCR_SYS_SLP_HEAVY;
	/*
	 * Set PRIMASK = 1 so on wake the CPU will not vector to any ISR.
	 * Set BASEPRI = 0 to allow any priority to wake.
	 */
	__set_BASEPRI(0);
	/* triggers sleep hardware */
	__WFI();
	__NOP();
	__NOP();

	/* Reset EC chip */
	cros_system_xec_soc_reset(dev);

	/* Should not reach here... */
	/* return 0; */
}

static struct cros_system_xec_data cros_system_xec_dev_data;

static const struct cros_system_xec_config cros_system_dev_cfg = {
	.base_pcr = DT_REG_ADDR_BY_NAME(DT_INST(0, microchip_xec_pcr), pcrr),
	.base_vbr = DT_REG_ADDR_BY_NAME(DT_INST(0, microchip_xec_pcr), vbatr),
	.base_wdog = DT_REG_ADDR(DT_INST(0, microchip_xec_watchdog)),
};

static const struct cros_system_driver_api cros_system_driver_xec_api = {
	.get_reset_cause = cros_system_xec_get_reset_cause,
	.soc_reset = cros_system_xec_soc_reset,
	.hibernate = cros_system_xec_hibernate,
	.chip_vendor = cros_system_xec_get_chip_vendor,
	.chip_name = cros_system_xec_get_chip_name,
	.chip_revision = cros_system_xec_get_chip_revision,
};

DEVICE_DEFINE(cros_system_xec_0, "CROS_SYSTEM", cros_system_xec_init, NULL,
	      &cros_system_xec_dev_data, &cros_system_dev_cfg, PRE_KERNEL_1,
	      CONFIG_CROS_SYSTEM_XEC_INIT_PRIORITY,
	      &cros_system_driver_xec_api);
