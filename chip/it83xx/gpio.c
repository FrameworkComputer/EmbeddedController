/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "builtin/assert.h"
#include "clock.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "intc.h"
#include "it83xx_pd.h"
#include "kmsc_chip.h"
#include "registers.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Data structure to define KSI/KSO GPIO mode control registers. */
struct kbs_gpio_ctrl_t {
	/* GPIO mode control register. */
	volatile uint8_t *gpio_mode;
	/* GPIO output enable register. */
	volatile uint8_t *gpio_out;
};

static const struct kbs_gpio_ctrl_t kbs_gpio_ctrl_regs[] = {
	/* KSI pins 7:0 */
	{ &IT83XX_KBS_KSIGCTRL, &IT83XX_KBS_KSIGOEN },
	/* KSO pins 15:8 */
	{ &IT83XX_KBS_KSOHGCTRL, &IT83XX_KBS_KSOHGOEN },
	/* KSO pins 7:0 */
	{ &IT83XX_KBS_KSOLGCTRL, &IT83XX_KBS_KSOLGOEN },
};

/**
 * Convert wake-up controller (WUC) group to the corresponding wake-up edge
 * sense register (WUESR). Return pointer to the register.
 *
 * @param grp  WUC group.
 *
 * @return Pointer to corresponding WUESR register.
 */
static volatile uint8_t *wuesr(uint8_t grp)
{
	/*
	 * From WUESR1-WUESR4, the address increases by ones. From WUESR5 on
	 * the address increases by fours.
	 */
	return (grp <= 4) ?
		       (volatile uint8_t *)(IT83XX_WUC_WUESR1 + grp - 1) :
		       (volatile uint8_t *)(IT83XX_WUC_WUESR5 + 4 * (grp - 5));
}

/**
 * Convert wake-up controller (WUC) group to the corresponding wake-up edge
 * mode register (WUEMR). Return pointer to the register.
 *
 * @param grp  WUC group.
 *
 * @return Pointer to corresponding WUEMR register.
 */
static volatile uint8_t *wuemr(uint8_t grp)
{
	/*
	 * From WUEMR1-WUEMR4, the address increases by ones. From WUEMR5 on
	 * the address increases by fours.
	 */
	return (grp <= 4) ?
		       (volatile uint8_t *)(IT83XX_WUC_WUEMR1 + grp - 1) :
		       (volatile uint8_t *)(IT83XX_WUC_WUEMR5 + 4 * (grp - 5));
}

/**
 * Convert wake-up controller (WUC) group to the corresponding wake-up both edge
 * mode register (WUBEMR). Return pointer to the register.
 *
 * @param grp  WUC group.
 *
 * @return Pointer to corresponding WUBEMR register.
 */
#ifdef IT83XX_GPIO_INT_FLEXIBLE
static volatile uint8_t *wubemr(uint8_t grp)
{
	/*
	 * From WUBEMR1-WUBEMR4, the address increases by ones. From WUBEMR5 on
	 * the address increases by fours.
	 */
	return (grp <= 4) ?
		       (volatile uint8_t *)(IT83XX_WUC_WUBEMR1 + grp - 1) :
		       (volatile uint8_t *)(IT83XX_WUC_WUBEMR5 + 4 * (grp - 5));
}
#endif

/*
 * Array to store the corresponding GPIO port and mask, and WUC group and mask
 * for each WKO interrupt. This allows GPIO interrupts coming in through WKO
 * to easily identify which pin caused the interrupt.
 * Note: Using designated initializers here in addition to using the array size
 * assert because many rows are purposely skipped. Not all IRQs are WKO IRQs,
 * so the IRQ index skips around. But, we still want the entire array to take
 * up the size of the total number of IRQs because the index to the array could
 * be any IRQ number.
 */
static const struct {
	uint8_t gpio_port;
	uint8_t gpio_mask;
	uint8_t wuc_group;
	uint8_t wuc_mask;
} gpio_irqs[] = {
	/*     irq           gpio_port,gpio_mask,wuc_group,wuc_mask */
	[IT83XX_IRQ_WKO20] = { GPIO_D, BIT(0), 2, BIT(0) },
	[IT83XX_IRQ_WKO21] = { GPIO_D, BIT(1), 2, BIT(1) },
	[IT83XX_IRQ_WKO22] = { GPIO_C, BIT(4), 2, BIT(2) },
	[IT83XX_IRQ_WKO23] = { GPIO_C, BIT(6), 2, BIT(3) },
	[IT83XX_IRQ_WKO24] = { GPIO_D, BIT(2), 2, BIT(4) },
#ifdef IT83XX_GPIO_INT_FLEXIBLE
	[IT83XX_IRQ_WKO40] = { GPIO_E, BIT(5), 4, BIT(0) },
	[IT83XX_IRQ_WKO45] = { GPIO_E, BIT(6), 4, BIT(5) },
	[IT83XX_IRQ_WKO46] = { GPIO_E, BIT(7), 4, BIT(6) },
#endif
	[IT83XX_IRQ_WKO50] = { GPIO_K, BIT(0), 5, BIT(0) },
	[IT83XX_IRQ_WKO51] = { GPIO_K, BIT(1), 5, BIT(1) },
	[IT83XX_IRQ_WKO52] = { GPIO_K, BIT(2), 5, BIT(2) },
	[IT83XX_IRQ_WKO53] = { GPIO_K, BIT(3), 5, BIT(3) },
	[IT83XX_IRQ_WKO54] = { GPIO_K, BIT(4), 5, BIT(4) },
	[IT83XX_IRQ_WKO55] = { GPIO_K, BIT(5), 5, BIT(5) },
	[IT83XX_IRQ_WKO56] = { GPIO_K, BIT(6), 5, BIT(6) },
	[IT83XX_IRQ_WKO57] = { GPIO_K, BIT(7), 5, BIT(7) },
	[IT83XX_IRQ_WKO60] = { GPIO_H, BIT(0), 6, BIT(0) },
	[IT83XX_IRQ_WKO61] = { GPIO_H, BIT(1), 6, BIT(1) },
	[IT83XX_IRQ_WKO62] = { GPIO_H, BIT(2), 6, BIT(2) },
	[IT83XX_IRQ_WKO63] = { GPIO_H, BIT(3), 6, BIT(3) },
	[IT83XX_IRQ_WKO64] = { GPIO_F, BIT(4), 6, BIT(4) },
	[IT83XX_IRQ_WKO65] = { GPIO_F, BIT(5), 6, BIT(5) },
	[IT83XX_IRQ_WKO66] = { GPIO_F, BIT(6), 6, BIT(6) },
	[IT83XX_IRQ_WKO67] = { GPIO_F, BIT(7), 6, BIT(7) },
	[IT83XX_IRQ_WKO70] = { GPIO_E, BIT(0), 7, BIT(0) },
	[IT83XX_IRQ_WKO71] = { GPIO_E, BIT(1), 7, BIT(1) },
	[IT83XX_IRQ_WKO72] = { GPIO_E, BIT(2), 7, BIT(2) },
	[IT83XX_IRQ_WKO73] = { GPIO_E, BIT(3), 7, BIT(3) },
	[IT83XX_IRQ_WKO74] = { GPIO_I, BIT(4), 7, BIT(4) },
	[IT83XX_IRQ_WKO75] = { GPIO_I, BIT(5), 7, BIT(5) },
	[IT83XX_IRQ_WKO76] = { GPIO_I, BIT(6), 7, BIT(6) },
	[IT83XX_IRQ_WKO77] = { GPIO_I, BIT(7), 7, BIT(7) },
	[IT83XX_IRQ_WKO80] = { GPIO_A, BIT(3), 8, BIT(0) },
	[IT83XX_IRQ_WKO81] = { GPIO_A, BIT(4), 8, BIT(1) },
	[IT83XX_IRQ_WKO82] = { GPIO_A, BIT(5), 8, BIT(2) },
	[IT83XX_IRQ_WKO83] = { GPIO_A, BIT(6), 8, BIT(3) },
	[IT83XX_IRQ_WKO84] = { GPIO_B, BIT(2), 8, BIT(4) },
	[IT83XX_IRQ_WKO85] = { GPIO_C, BIT(0), 8, BIT(5) },
	[IT83XX_IRQ_WKO86] = { GPIO_C, BIT(7), 8, BIT(6) },
	[IT83XX_IRQ_WKO87] = { GPIO_D, BIT(7), 8, BIT(7) },
	[IT83XX_IRQ_WKO88] = { GPIO_H, BIT(4), 9, BIT(0) },
	[IT83XX_IRQ_WKO89] = { GPIO_H, BIT(5), 9, BIT(1) },
	[IT83XX_IRQ_WKO90] = { GPIO_H, BIT(6), 9, BIT(2) },
	[IT83XX_IRQ_WKO91] = { GPIO_A, BIT(0), 9, BIT(3) },
	[IT83XX_IRQ_WKO92] = { GPIO_A, BIT(1), 9, BIT(4) },
	[IT83XX_IRQ_WKO93] = { GPIO_A, BIT(2), 9, BIT(5) },
	[IT83XX_IRQ_WKO94] = { GPIO_B, BIT(4), 9, BIT(6) },
	[IT83XX_IRQ_WKO95] = { GPIO_C, BIT(2), 9, BIT(7) },
	[IT83XX_IRQ_WKO96] = { GPIO_F, BIT(0), 10, BIT(0) },
	[IT83XX_IRQ_WKO97] = { GPIO_F, BIT(1), 10, BIT(1) },
	[IT83XX_IRQ_WKO98] = { GPIO_F, BIT(2), 10, BIT(2) },
	[IT83XX_IRQ_WKO99] = { GPIO_F, BIT(3), 10, BIT(3) },
	[IT83XX_IRQ_WKO100] = { GPIO_A, BIT(7), 10, BIT(4) },
	[IT83XX_IRQ_WKO101] = { GPIO_B, BIT(0), 10, BIT(5) },
	[IT83XX_IRQ_WKO102] = { GPIO_B, BIT(1), 10, BIT(6) },
	[IT83XX_IRQ_WKO103] = { GPIO_B, BIT(3), 10, BIT(7) },
	[IT83XX_IRQ_WKO104] = { GPIO_B, BIT(5), 11, BIT(0) },
	[IT83XX_IRQ_WKO105] = { GPIO_B, BIT(6), 11, BIT(1) },
	[IT83XX_IRQ_WKO106] = { GPIO_B, BIT(7), 11, BIT(2) },
	[IT83XX_IRQ_WKO107] = { GPIO_C, BIT(1), 11, BIT(3) },
	[IT83XX_IRQ_WKO108] = { GPIO_C, BIT(3), 11, BIT(4) },
	[IT83XX_IRQ_WKO109] = { GPIO_C, BIT(5), 11, BIT(5) },
	[IT83XX_IRQ_WKO110] = { GPIO_D, BIT(3), 11, BIT(6) },
	[IT83XX_IRQ_WKO111] = { GPIO_D, BIT(4), 11, BIT(7) },
	[IT83XX_IRQ_WKO112] = { GPIO_D, BIT(5), 12, BIT(0) },
	[IT83XX_IRQ_WKO113] = { GPIO_D, BIT(6), 12, BIT(1) },
	[IT83XX_IRQ_WKO114] = { GPIO_E, BIT(4), 12, BIT(2) },
	[IT83XX_IRQ_WKO115] = { GPIO_G, BIT(0), 12, BIT(3) },
	[IT83XX_IRQ_WKO116] = { GPIO_G, BIT(1), 12, BIT(4) },
	[IT83XX_IRQ_WKO117] = { GPIO_G, BIT(2), 12, BIT(5) },
	[IT83XX_IRQ_WKO118] = { GPIO_G, BIT(6), 12, BIT(6) },
	[IT83XX_IRQ_WKO119] = { GPIO_I, BIT(0), 12, BIT(7) },
	[IT83XX_IRQ_WKO120] = { GPIO_I, BIT(1), 13, BIT(0) },
	[IT83XX_IRQ_WKO121] = { GPIO_I, BIT(2), 13, BIT(1) },
	[IT83XX_IRQ_WKO122] = { GPIO_I, BIT(3), 13, BIT(2) },
#ifdef IT83XX_GPIO_INT_FLEXIBLE
	[IT83XX_IRQ_WKO123] = { GPIO_G, BIT(3), 13, BIT(3) },
	[IT83XX_IRQ_WKO124] = { GPIO_G, BIT(4), 13, BIT(4) },
	[IT83XX_IRQ_WKO125] = { GPIO_G, BIT(5), 13, BIT(5) },
	[IT83XX_IRQ_WKO126] = { GPIO_G, BIT(7), 13, BIT(6) },
#endif
	[IT83XX_IRQ_WKO128] = { GPIO_J, BIT(0), 14, BIT(0) },
	[IT83XX_IRQ_WKO129] = { GPIO_J, BIT(1), 14, BIT(1) },
	[IT83XX_IRQ_WKO130] = { GPIO_J, BIT(2), 14, BIT(2) },
	[IT83XX_IRQ_WKO131] = { GPIO_J, BIT(3), 14, BIT(3) },
	[IT83XX_IRQ_WKO132] = { GPIO_J, BIT(4), 14, BIT(4) },
	[IT83XX_IRQ_WKO133] = { GPIO_J, BIT(5), 14, BIT(5) },
	[IT83XX_IRQ_WKO134] = { GPIO_J, BIT(6), 14, BIT(6) },
	[IT83XX_IRQ_WKO135] = { GPIO_J, BIT(7), 14, BIT(7) },
	[IT83XX_IRQ_WKO136] = { GPIO_L, BIT(0), 15, BIT(0) },
	[IT83XX_IRQ_WKO137] = { GPIO_L, BIT(1), 15, BIT(1) },
	[IT83XX_IRQ_WKO138] = { GPIO_L, BIT(2), 15, BIT(2) },
	[IT83XX_IRQ_WKO139] = { GPIO_L, BIT(3), 15, BIT(3) },
	[IT83XX_IRQ_WKO140] = { GPIO_L, BIT(4), 15, BIT(4) },
	[IT83XX_IRQ_WKO141] = { GPIO_L, BIT(5), 15, BIT(5) },
	[IT83XX_IRQ_WKO142] = { GPIO_L, BIT(6), 15, BIT(6) },
	[IT83XX_IRQ_WKO143] = { GPIO_L, BIT(7), 15, BIT(7) },
#ifdef IT83XX_GPIO_INT_FLEXIBLE
	[IT83XX_IRQ_WKO144] = { GPIO_M, BIT(0), 16, BIT(0) },
	[IT83XX_IRQ_WKO145] = { GPIO_M, BIT(1), 16, BIT(1) },
	[IT83XX_IRQ_WKO146] = { GPIO_M, BIT(2), 16, BIT(2) },
	[IT83XX_IRQ_WKO147] = { GPIO_M, BIT(3), 16, BIT(3) },
	[IT83XX_IRQ_WKO148] = { GPIO_M, BIT(4), 16, BIT(4) },
	[IT83XX_IRQ_WKO149] = { GPIO_M, BIT(5), 16, BIT(5) },
	[IT83XX_IRQ_WKO150] = { GPIO_M, BIT(6), 16, BIT(6) },
#endif
#if defined(CHIP_FAMILY_IT8XXX1) || defined(CHIP_FAMILY_IT8XXX2)
	[IT83XX_IRQ_GPO0] = { GPIO_O, BIT(0), 19, BIT(0) },
	[IT83XX_IRQ_GPO1] = { GPIO_O, BIT(1), 19, BIT(1) },
	[IT83XX_IRQ_GPO2] = { GPIO_O, BIT(2), 19, BIT(2) },
	[IT83XX_IRQ_GPO3] = { GPIO_O, BIT(3), 19, BIT(3) },
	[IT83XX_IRQ_GPP0] = { GPIO_P, BIT(0), 20, BIT(0) },
	[IT83XX_IRQ_GPP1] = { GPIO_P, BIT(1), 20, BIT(1) },
	[IT83XX_IRQ_GPP2] = { GPIO_P, BIT(2), 20, BIT(2) },
	[IT83XX_IRQ_GPP3] = { GPIO_P, BIT(3), 20, BIT(3) },
	[IT83XX_IRQ_GPP4] = { GPIO_P, BIT(4), 20, BIT(4) },
	[IT83XX_IRQ_GPP5] = { GPIO_P, BIT(5), 20, BIT(5) },
	[IT83XX_IRQ_GPP6] = { GPIO_P, BIT(6), 20, BIT(6) },
	[IT83XX_IRQ_GPQ0] = { GPIO_Q, BIT(0), 21, BIT(0) },
	[IT83XX_IRQ_GPQ1] = { GPIO_Q, BIT(1), 21, BIT(1) },
	[IT83XX_IRQ_GPQ2] = { GPIO_Q, BIT(2), 21, BIT(2) },
	[IT83XX_IRQ_GPQ3] = { GPIO_Q, BIT(3), 21, BIT(3) },
	[IT83XX_IRQ_GPQ4] = { GPIO_Q, BIT(4), 21, BIT(4) },
	[IT83XX_IRQ_GPQ5] = { GPIO_Q, BIT(5), 21, BIT(5) },
	[IT83XX_IRQ_GPR0] = { GPIO_R, BIT(0), 22, BIT(0) },
	[IT83XX_IRQ_GPR1] = { GPIO_R, BIT(1), 22, BIT(1) },
	[IT83XX_IRQ_GPR2] = { GPIO_R, BIT(2), 22, BIT(2) },
	[IT83XX_IRQ_GPR3] = { GPIO_R, BIT(3), 22, BIT(3) },
	[IT83XX_IRQ_GPR4] = { GPIO_R, BIT(4), 22, BIT(4) },
	[IT83XX_IRQ_GPR5] = { GPIO_R, BIT(5), 22, BIT(5) },
#endif
	[IT83XX_IRQ_COUNT] = { 0, 0, 0, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(gpio_irqs) == IT83XX_IRQ_COUNT + 1);

/**
 * Given a GPIO port and mask, find the corresponding WKO interrupt number.
 *
 * @param port  GPIO port
 * @param mask  GPIO mask
 *
 * @return IRQ for the WKO interrupt on the corresponding input pin.
 */
static int gpio_to_irq(uint8_t port, uint8_t mask)
{
	int i;

	for (i = 0; i < IT83XX_IRQ_COUNT; i++) {
		if (gpio_irqs[i].gpio_port == port &&
		    gpio_irqs[i].gpio_mask == mask)
			return i;
	}

	return -1;
}

struct gpio_1p8v_t {
	volatile uint8_t *reg;
	uint8_t sel;
};

static const struct gpio_1p8v_t gpio_1p8v_sel[GPIO_PORT_COUNT][8] = {
#ifdef IT83XX_GPIO_1P8V_PIN_EXTENDED
	[GPIO_A] = { [4] = { &IT83XX_GPIO_GRC24, BIT(0) },
		     [5] = { &IT83XX_GPIO_GRC24, BIT(1) },
		     [6] = { &IT83XX_GPIO_GRC24, BIT(5) },
		     [7] = { &IT83XX_GPIO_GRC24, BIT(6) } },
	[GPIO_B] = { [3] = { &IT83XX_GPIO_GRC22, BIT(1) },
		     [4] = { &IT83XX_GPIO_GRC22, BIT(0) },
		     [5] = { &IT83XX_GPIO_GRC19, BIT(7) },
		     [6] = { &IT83XX_GPIO_GRC19, BIT(6) },
		     [7] = { &IT83XX_GPIO_GRC24, BIT(4) } },
	[GPIO_C] = { [0] = { &IT83XX_GPIO_GRC22, BIT(7) },
		     [1] = { &IT83XX_GPIO_GRC19, BIT(5) },
		     [2] = { &IT83XX_GPIO_GRC19, BIT(4) },
		     [4] = { &IT83XX_GPIO_GRC24, BIT(2) },
		     [6] = { &IT83XX_GPIO_GRC24, BIT(3) },
		     [7] = { &IT83XX_GPIO_GRC19, BIT(3) } },
	[GPIO_D] = { [0] = { &IT83XX_GPIO_GRC19, BIT(2) },
		     [1] = { &IT83XX_GPIO_GRC19, BIT(1) },
		     [2] = { &IT83XX_GPIO_GRC19, BIT(0) },
		     [3] = { &IT83XX_GPIO_GRC20, BIT(7) },
		     [4] = { &IT83XX_GPIO_GRC20, BIT(6) },
		     [5] = { &IT83XX_GPIO_GRC22, BIT(4) },
		     [6] = { &IT83XX_GPIO_GRC22, BIT(5) },
		     [7] = { &IT83XX_GPIO_GRC22, BIT(6) } },
	[GPIO_E] = { [0] = { &IT83XX_GPIO_GRC20, BIT(5) },
		     [1] = { &IT83XX_GPIO_GCR28, BIT(6) },
		     [2] = { &IT83XX_GPIO_GCR28, BIT(7) },
		     [4] = { &IT83XX_GPIO_GRC22, BIT(2) },
		     [5] = { &IT83XX_GPIO_GRC22, BIT(3) },
		     [6] = { &IT83XX_GPIO_GRC20, BIT(4) },
		     [7] = { &IT83XX_GPIO_GRC20, BIT(3) } },
	[GPIO_F] = { [0] = { &IT83XX_GPIO_GCR28, BIT(4) },
		     [1] = { &IT83XX_GPIO_GCR28, BIT(5) },
		     [2] = { &IT83XX_GPIO_GRC20, BIT(2) },
		     [3] = { &IT83XX_GPIO_GRC20, BIT(1) },
		     [4] = { &IT83XX_GPIO_GRC20, BIT(0) },
		     [5] = { &IT83XX_GPIO_GRC21, BIT(7) },
		     [6] = { &IT83XX_GPIO_GRC21, BIT(6) },
		     [7] = { &IT83XX_GPIO_GRC21, BIT(5) } },
	[GPIO_G] = { [0] = { &IT83XX_GPIO_GCR28, BIT(2) },
		     [1] = { &IT83XX_GPIO_GRC21, BIT(4) },
		     [2] = { &IT83XX_GPIO_GCR28, BIT(3) },
		     [6] = { &IT83XX_GPIO_GRC21, BIT(3) } },
	[GPIO_H] = { [0] = { &IT83XX_GPIO_GRC21, BIT(2) },
		     [1] = { &IT83XX_GPIO_GRC21, BIT(1) },
		     [2] = { &IT83XX_GPIO_GRC21, BIT(0) },
		     [5] = { &IT83XX_GPIO_GCR27, BIT(7) },
		     [6] = { &IT83XX_GPIO_GCR28, BIT(0) } },
	[GPIO_I] = { [0] = { &IT83XX_GPIO_GCR27, BIT(3) },
		     [1] = { &IT83XX_GPIO_GRC23, BIT(4) },
		     [2] = { &IT83XX_GPIO_GRC23, BIT(5) },
		     [3] = { &IT83XX_GPIO_GRC23, BIT(6) },
		     [4] = { &IT83XX_GPIO_GRC23, BIT(7) },
		     [5] = { &IT83XX_GPIO_GCR27, BIT(4) },
		     [6] = { &IT83XX_GPIO_GCR27, BIT(5) },
		     [7] = { &IT83XX_GPIO_GCR27, BIT(6) } },
	[GPIO_J] = { [0] = { &IT83XX_GPIO_GRC23, BIT(0) },
		     [1] = { &IT83XX_GPIO_GRC23, BIT(1) },
		     [2] = { &IT83XX_GPIO_GRC23, BIT(2) },
		     [3] = { &IT83XX_GPIO_GRC23, BIT(3) },
		     [4] = { &IT83XX_GPIO_GCR27, BIT(0) },
		     [5] = { &IT83XX_GPIO_GCR27, BIT(1) },
		     [6] = { &IT83XX_GPIO_GCR27, BIT(2) },
		     [7] = { &IT83XX_GPIO_GCR33, BIT(2) } },
	[GPIO_K] = { [0] = { &IT83XX_GPIO_GCR26, BIT(0) },
		     [1] = { &IT83XX_GPIO_GCR26, BIT(1) },
		     [2] = { &IT83XX_GPIO_GCR26, BIT(2) },
		     [3] = { &IT83XX_GPIO_GCR26, BIT(3) },
		     [4] = { &IT83XX_GPIO_GCR26, BIT(4) },
		     [5] = { &IT83XX_GPIO_GCR26, BIT(5) },
		     [6] = { &IT83XX_GPIO_GCR26, BIT(6) },
		     [7] = { &IT83XX_GPIO_GCR26, BIT(7) } },
	[GPIO_L] = { [0] = { &IT83XX_GPIO_GCR25, BIT(0) },
		     [1] = { &IT83XX_GPIO_GCR25, BIT(1) },
		     [2] = { &IT83XX_GPIO_GCR25, BIT(2) },
		     [3] = { &IT83XX_GPIO_GCR25, BIT(3) },
		     [4] = { &IT83XX_GPIO_GCR25, BIT(4) },
		     [5] = { &IT83XX_GPIO_GCR25, BIT(5) },
		     [6] = { &IT83XX_GPIO_GCR25, BIT(6) },
		     [7] = { &IT83XX_GPIO_GCR25, BIT(7) } },
#if defined(CHIP_FAMILY_IT8XXX1) || defined(CHIP_FAMILY_IT8XXX2)
	[GPIO_O] = { [0] = { &IT83XX_GPIO_GCR31, BIT(0) },
		     [1] = { &IT83XX_GPIO_GCR31, BIT(1) },
		     [2] = { &IT83XX_GPIO_GCR31, BIT(2) },
		     [3] = { &IT83XX_GPIO_GCR31, BIT(3) } },
	[GPIO_P] = { [0] = { &IT83XX_GPIO_GCR32, BIT(0) },
		     [1] = { &IT83XX_GPIO_GCR32, BIT(1) },
		     [2] = { &IT83XX_GPIO_GCR32, BIT(2) },
		     [3] = { &IT83XX_GPIO_GCR32, BIT(3) },
		     [4] = { &IT83XX_GPIO_GCR32, BIT(4) },
		     [5] = { &IT83XX_GPIO_GCR32, BIT(5) },
		     [6] = { &IT83XX_GPIO_GCR32, BIT(6) } },
#endif
#else
	[GPIO_A] = { [4] = { &IT83XX_GPIO_GRC24, BIT(0) },
		     [5] = { &IT83XX_GPIO_GRC24, BIT(1) } },
	[GPIO_B] = { [3] = { &IT83XX_GPIO_GRC22, BIT(1) },
		     [4] = { &IT83XX_GPIO_GRC22, BIT(0) },
		     [5] = { &IT83XX_GPIO_GRC19, BIT(7) },
		     [6] = { &IT83XX_GPIO_GRC19, BIT(6) } },
	[GPIO_C] = { [1] = { &IT83XX_GPIO_GRC19, BIT(5) },
		     [2] = { &IT83XX_GPIO_GRC19, BIT(4) },
		     [7] = { &IT83XX_GPIO_GRC19, BIT(3) } },
	[GPIO_D] = { [0] = { &IT83XX_GPIO_GRC19, BIT(2) },
		     [1] = { &IT83XX_GPIO_GRC19, BIT(1) },
		     [2] = { &IT83XX_GPIO_GRC19, BIT(0) },
		     [3] = { &IT83XX_GPIO_GRC20, BIT(7) },
		     [4] = { &IT83XX_GPIO_GRC20, BIT(6) } },
	[GPIO_E] = { [0] = { &IT83XX_GPIO_GRC20, BIT(5) },
		     [6] = { &IT83XX_GPIO_GRC20, BIT(4) },
		     [7] = { &IT83XX_GPIO_GRC20, BIT(3) } },
	[GPIO_F] = { [2] = { &IT83XX_GPIO_GRC20, BIT(2) },
		     [3] = { &IT83XX_GPIO_GRC20, BIT(1) },
		     [4] = { &IT83XX_GPIO_GRC20, BIT(0) },
		     [5] = { &IT83XX_GPIO_GRC21, BIT(7) },
		     [6] = { &IT83XX_GPIO_GRC21, BIT(6) },
		     [7] = { &IT83XX_GPIO_GRC21, BIT(5) } },
	[GPIO_H] = { [0] = { &IT83XX_GPIO_GRC21, BIT(2) },
		     [1] = { &IT83XX_GPIO_GRC21, BIT(1) },
		     [2] = { &IT83XX_GPIO_GRC21, BIT(0) } },
	[GPIO_I] = { [1] = { &IT83XX_GPIO_GRC23, BIT(4) },
		     [2] = { &IT83XX_GPIO_GRC23, BIT(5) },
		     [3] = { &IT83XX_GPIO_GRC23, BIT(6) },
		     [4] = { &IT83XX_GPIO_GRC23, BIT(7) } },
	[GPIO_J] = { [0] = { &IT83XX_GPIO_GRC23, BIT(0) },
		     [1] = { &IT83XX_GPIO_GRC23, BIT(1) },
		     [2] = { &IT83XX_GPIO_GRC23, BIT(2) },
		     [3] = { &IT83XX_GPIO_GRC23, BIT(3) } },
#endif
};

static void gpio_1p8v_3p3v_sel_by_pin(uint8_t port, uint8_t pin, int sel_1p8v)
{
	volatile uint8_t *reg_1p8v = gpio_1p8v_sel[port][pin].reg;
	uint8_t sel = gpio_1p8v_sel[port][pin].sel;

	if (reg_1p8v == NULL)
		return;

	if (sel_1p8v)
		*reg_1p8v |= sel;
	else
		*reg_1p8v &= ~sel;
}

static void it83xx_enable_tristate_on_adc(uint32_t port, uint32_t pin,
					  enum gpio_alternate_func fn)
{
	/* GPIOL pin 0/1/2/3: ADC 13/14/15/16 */
	if ((port == GPIO_L) && (pin < 4)) {
		/*
		 * On IT8320/IT81302, enabling ADC ALT function on group I also
		 * automatically enable tri-state on the pins. But this
		 * mechanism is not vailable for GPIOL 0~3 (ADC 13 ~ 16).
		 * Therefore, enable tri-state on these pins automatically and
		 * making measurement accurate.
		 */
		if ((fn == GPIO_ALT_FUNC_DEFAULT) || (fn == GPIO_ALT_FUNC_1)) {
			IT83XX_GPIO_CTRL(port, pin) |=
				GPCR_PORT_PIN_MODE_TRISTATE;
		}
	}
}

static inline void it83xx_set_alt_func(uint32_t port, uint32_t pin,
				       enum gpio_alternate_func func)
{
	/*
	 * If func is not ALT_FUNC_NONE, set for alternate function.
	 * Otherwise, turn the pin into an input as it's default.
	 */
	if (func != GPIO_ALT_FUNC_NONE) {
		IT83XX_GPIO_CTRL(port, pin) &=
			~(GPCR_PORT_PIN_MODE_OUTPUT | GPCR_PORT_PIN_MODE_INPUT);
		it83xx_enable_tristate_on_adc(port, pin, func);
	} else {
		IT83XX_GPIO_CTRL(port, pin) = (IT83XX_GPIO_CTRL(port, pin) |
					       GPCR_PORT_PIN_MODE_INPUT) &
					      ~GPCR_PORT_PIN_MODE_OUTPUT;
	}
}

void gpio_set_alternate_function(uint32_t port, uint32_t mask,
				 enum gpio_alternate_func func)
{
	uint32_t pin = 0;

	/* Alternate function configuration for KSI/KSO pins */
	if (port > GPIO_PORT_COUNT) {
		port -= GPIO_KSI;
		/*
		 * If func is non-negative, set for keyboard scan function.
		 * Otherwise, turn the pin into a GPIO input.
		 */
		if (func >= GPIO_ALT_FUNC_DEFAULT) {
			/* KBS mode */
			*kbs_gpio_ctrl_regs[port].gpio_mode &= ~mask;
		} else {
			/* input */
			*kbs_gpio_ctrl_regs[port].gpio_out &= ~mask;
			/* GPIO mode */
			*kbs_gpio_ctrl_regs[port].gpio_mode |= mask;
		}
		return;
	}

	/* For each bit high in the mask, set that pin to use alt. func. */
	while (mask > 0) {
		if (mask & 1)
			it83xx_set_alt_func(port, pin, func);
		pin++;
		mask >>= 1;
	}
}

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	return (IT83XX_GPIO_DATA_MIRROR(gpio_list[signal].port) &
		gpio_list[signal].mask) ?
		       1 :
		       0;
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	/* critical section with interrupts off */
	uint32_t int_mask = read_clear_int_mask();

	if (value)
		IT83XX_GPIO_DATA(gpio_list[signal].port) |=
			gpio_list[signal].mask;
	else
		IT83XX_GPIO_DATA(gpio_list[signal].port) &=
			~gpio_list[signal].mask;
	/* restore interrupts */
	set_int_mask(int_mask);
}

void gpio_kbs_pin_gpio_mode(uint32_t port, uint32_t mask, uint32_t flags)
{
	uint32_t idx = port - GPIO_KSI;

	/* Set GPIO mode */
	*kbs_gpio_ctrl_regs[idx].gpio_mode |= mask;

	/* Set input or output */
	if (flags & GPIO_OUTPUT) {
		/*
		 * Select open drain first, so that we don't glitch the signal
		 * when changing the line to an output.
		 */
		if (flags & GPIO_OPEN_DRAIN)
			/*
			 * it83xx: need external pullup for output data high
			 * it8xxx2: this pin is always internal pullup
			 */
			IT83XX_GPIO_GPOT(port) |= mask;
		else
			/*
			 * it8xxx2: this pin is not internal pullup
			 */
			IT83XX_GPIO_GPOT(port) &= ~mask;

		/* Set level before change to output. */
		if (flags & GPIO_HIGH)
			IT83XX_GPIO_DATA(port) |= mask;
		else if (flags & GPIO_LOW)
			IT83XX_GPIO_DATA(port) &= ~mask;
		*kbs_gpio_ctrl_regs[idx].gpio_out |= mask;
	} else {
		*kbs_gpio_ctrl_regs[idx].gpio_out &= ~mask;
		if (flags & GPIO_PULL_UP)
			IT83XX_GPIO_GPOT(port) |= mask;
		else
			/* No internal pullup and pulldown */
			IT83XX_GPIO_GPOT(port) &= ~mask;
	}
}

#ifndef IT83XX_GPIO_INT_FLEXIBLE
/* Returns true when the falling trigger bit actually mean both trigger. */
static int group_falling_is_both(const int group)
{
	return group == 7 || group == 10 || group == 12;
}

static const char *get_gpio_string(const int port, const int mask)
{
	static char buffer[3];
	int i;

	buffer[0] = port - GPIO_A + 'A';
	buffer[1] = '!';

	for (i = 0; i < 8; ++i) {
		if (mask & BIT(i)) {
			buffer[1] = i + '0';
			break;
		}
	}
	return buffer;
}
#endif /* IT83XX_GPIO_INT_FLEXIBLE */

void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	uint32_t pin = 0;
	uint32_t mask_copy = mask;

	/* Set GPIO mode for KSI/KSO pins */
	if (port > GPIO_PORT_COUNT) {
		gpio_kbs_pin_gpio_mode(port, mask, flags);
		return;
	}

	/*
	 * Select open drain first, so that we don't glitch the signal
	 * when changing the line to an output.
	 */
	if (flags & GPIO_OPEN_DRAIN)
		IT83XX_GPIO_GPOT(port) |= mask;
	else
		IT83XX_GPIO_GPOT(port) &= ~mask;

	/* If output, set level before changing type to an output. */
	if (flags & GPIO_OUTPUT) {
		if (flags & GPIO_HIGH)
			IT83XX_GPIO_DATA(port) |= mask;
		else if (flags & GPIO_LOW)
			IT83XX_GPIO_DATA(port) &= ~mask;
	}

	/* For each bit high in the mask, set input/output and pullup/down. */
	while (mask_copy > 0) {
		if (mask_copy & 1) {
			/* Set input or output. */
			if (flags & GPIO_OUTPUT)
				IT83XX_GPIO_CTRL(port, pin) =
					(IT83XX_GPIO_CTRL(port, pin) |
					 GPCR_PORT_PIN_MODE_OUTPUT) &
					~GPCR_PORT_PIN_MODE_INPUT;
			else
				IT83XX_GPIO_CTRL(port, pin) =
					(IT83XX_GPIO_CTRL(port, pin) |
					 GPCR_PORT_PIN_MODE_INPUT) &
					~GPCR_PORT_PIN_MODE_OUTPUT;

			/* Handle pullup / pulldown */
			if (flags & GPIO_PULL_UP) {
				IT83XX_GPIO_CTRL(port, pin) =
					(IT83XX_GPIO_CTRL(port, pin) |
					 GPCR_PORT_PIN_MODE_PULLUP) &
					~GPCR_PORT_PIN_MODE_PULLDOWN;
			} else if (flags & GPIO_PULL_DOWN) {
				IT83XX_GPIO_CTRL(port, pin) =
					(IT83XX_GPIO_CTRL(port, pin) |
					 GPCR_PORT_PIN_MODE_PULLDOWN) &
					~GPCR_PORT_PIN_MODE_PULLUP;
			} else {
				/* No pull up/down */
				IT83XX_GPIO_CTRL(port, pin) &=
					~(GPCR_PORT_PIN_MODE_PULLUP |
					  GPCR_PORT_PIN_MODE_PULLDOWN);
			}

			/* To select 1.8v or 3.3v support. */
			gpio_1p8v_3p3v_sel_by_pin(port, pin,
						  (flags & GPIO_SEL_1P8V));
		}

		pin++;
		mask_copy >>= 1;
	}

	if (flags & (GPIO_INT_F_RISING | GPIO_INT_F_FALLING)) {
		int irq, wuc_group, wuc_mask;
		irq = gpio_to_irq(port, mask);
		wuc_group = gpio_irqs[irq].wuc_group;
		wuc_mask = gpio_irqs[irq].wuc_mask;

		/*
		 * Set both edges interrupt.
		 * The WUBEMR register is valid on IT8320 DX version.
		 * And the setting (falling or rising edge) of WUEMR register is
		 * invalid if this mode is set.
		 */
#ifdef IT83XX_GPIO_INT_FLEXIBLE
		if ((flags & GPIO_INT_BOTH) == GPIO_INT_BOTH)
			*(wubemr(wuc_group)) |= wuc_mask;
		else
			*(wubemr(wuc_group)) &= ~wuc_mask;
#endif

		if (flags & GPIO_INT_F_FALLING) {
#ifndef IT83XX_GPIO_INT_FLEXIBLE
			if (!!(flags & GPIO_INT_F_RISING) !=
			    group_falling_is_both(wuc_group)) {
				ccprintf("!!Fix GPIO %s interrupt config!!\n",
					 get_gpio_string(port, mask));
			}
#endif
			*(wuemr(wuc_group)) |= wuc_mask;
		} else {
			*(wuemr(wuc_group)) &= ~wuc_mask;
		}
		/*
		 * Always write 1 to clear the WUC status register after
		 * modifying edge mode selection register (WUBEMR and WUEMR).
		 */
		*(wuesr(wuc_group)) = wuc_mask;
	}
}

int gpio_enable_interrupt(enum gpio_signal signal)
{
	int irq = gpio_to_irq(gpio_list[signal].port, gpio_list[signal].mask);

	if (irq == -1)
		return EC_ERROR_UNKNOWN;
	else
		task_enable_irq(irq);

	return EC_SUCCESS;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	int irq = gpio_to_irq(gpio_list[signal].port, gpio_list[signal].mask);

	if (irq == -1)
		return EC_ERROR_UNKNOWN;
	else
		task_disable_irq(irq);

	return EC_SUCCESS;
}

int gpio_clear_pending_interrupt(enum gpio_signal signal)
{
	int irq = gpio_to_irq(gpio_list[signal].port, gpio_list[signal].mask);

	if (irq == -1)
		return EC_ERROR_UNKNOWN;

	*(wuesr(gpio_irqs[irq].wuc_group)) = gpio_irqs[irq].wuc_mask;
	task_clear_pending_irq(irq);
	return EC_SUCCESS;
}

/* To prevent cc pins leakage, disables integrated cc module. */
void it83xx_disable_cc_module(int port)
{
	/* Power down all CC, and disable CC voltage detector */
	IT83XX_USBPD_CCGCR(port) |= USBPD_REG_MASK_DISABLE_CC;
#if defined(CONFIG_USB_PD_TCPM_DRIVER_IT83XX)
	IT83XX_USBPD_CCCSR(port) |= USBPD_REG_MASK_DISABLE_CC_VOL_DETECTOR;
#elif defined(CONFIG_USB_PD_TCPM_DRIVER_IT8XXX2)
	IT83XX_USBPD_CCGCR(port) |= USBPD_REG_MASK_DISABLE_CC_VOL_DETECTOR;
#endif
	/*
	 * Disconnect CC analog module (ex.UP/RD/DET/TX/RX), and
	 * disconnect CC 5.1K to GND
	 */
	IT83XX_USBPD_CCCSR(port) |= (USBPD_REG_MASK_CC2_DISCONNECT |
				     USBPD_REG_MASK_CC2_DISCONNECT_5_1K_TO_GND |
				     USBPD_REG_MASK_CC1_DISCONNECT |
				     USBPD_REG_MASK_CC1_DISCONNECT_5_1K_TO_GND);
	/* Disconnect CC 5V tolerant */
	IT83XX_USBPD_CCPSR(port) |= (USBPD_REG_MASK_DISCONNECT_POWER_CC2 |
				     USBPD_REG_MASK_DISCONNECT_POWER_CC1);
}

void gpio_pre_init(void)
{
	const struct gpio_info *g = gpio_list;
	int is_warm = system_is_reboot_warm();
	int flags;
	int i;

	IT83XX_GPIO_GCR = 0x06;

#if !defined(CONFIG_IT83XX_VCC_1P8V) && !defined(CONFIG_IT83XX_VCC_3P3V)
#error Please select voltage level of VCC for EC.
#endif

#if defined(CONFIG_IT83XX_VCC_1P8V) && defined(CONFIG_IT83XX_VCC_3P3V)
#error Must select only one voltage level of VCC for EC.
#endif
	/* The power level of GPM6 follows VCC */
	IT83XX_GPIO_GCR29 |= BIT(0);

	/* The power level (VCC) of GPM0~6 is 1.8V */
	if (IS_ENABLED(CONFIG_IT83XX_VCC_1P8V))
		IT83XX_GPIO_GCR30 |= BIT(4);

	/* The power level (VCC) of GPM0~6 is 3.3V */
	if (IS_ENABLED(CONFIG_IT83XX_VCC_3P3V))
		IT83XX_GPIO_GCR30 &= ~BIT(4);

#if IT83XX_USBPD_PHY_PORT_COUNT < CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT
#error "ITE pd active port count should be less than physical port count !"
#endif
	/*
	 * To prevent cc pins leakage and cc pins can be used as gpio,
	 * disable board not active ITE TCPC port cc modules.
	 */
	for (i = CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT;
	     i < IT83XX_USBPD_PHY_PORT_COUNT; i++) {
		it83xx_disable_cc_module(i);
		/* Dis-connect 5.1K dead battery resistor to CC */
		IT83XX_USBPD_CCPSR(i) |=
			(USBPD_REG_MASK_DISCONNECT_5_1K_CC2_DB |
			 USBPD_REG_MASK_DISCONNECT_5_1K_CC1_DB);
	}

#ifndef CONFIG_USB
	/*
	 * We need to enable USB's clock so we can config USB control register.
	 * This is important for a software reset as the hardware clock may
	 * already be disabled from the previous run.
	 * We will disable clock to USB module in clock_module_disable() later.
	 */
	clock_enable_peripheral(CGC_OFFSET_USB, 0, 0);
	/*
	 * Disable default pull-down of USB controller (GPH5 and GPH6) if we
	 * don't use this module.
	 */
	IT83XX_USB_P0MCR &= ~USB_DP_DM_PULL_DOWN_EN;
#endif

#if defined(CHIP_FAMILY_IT8XXX1) || defined(CHIP_FAMILY_IT8XXX2)
	/* Q group pins are default GPI mode, clear alternate setting. */
	IT83XX_VBATPC_XLPIER = 0x0;
	/*
	 * R group pins are default alternate output low, we clear alternate
	 * setting (sink power switch from VBAT to VSTBY) to become GPO output
	 * low.
	 * NOTE: GPR0~5 pins are output low by default. It should consider
	 *       that if output low signal effect external circuit or not,
	 *       until we reconfig these pins in gpio.inc.
	 */
	IT83XX_VBATPC_BGPOPSCR = 0x0;
#endif

	/*
	 * On IT81202 (128-pins package), the pins of GPIO group K and L aren't
	 * bonding with pad. So we configure these pins as internal pull-down
	 * at default to prevent leakage current due to floating.
	 */
	if (IS_ENABLED(IT83XX_GPIO_GROUP_K_L_DEFAULT_PULL_DOWN)) {
		for (i = 0; i < 8; i++) {
			IT83XX_GPIO_CTRL(GPIO_K, i) =
				(GPCR_PORT_PIN_MODE_INPUT |
				 GPCR_PORT_PIN_MODE_PULLDOWN);
			IT83XX_GPIO_CTRL(GPIO_L, i) =
				(GPCR_PORT_PIN_MODE_INPUT |
				 GPCR_PORT_PIN_MODE_PULLDOWN);
		}
	}

	/*
	 * On IT81202/IT81302, the GPIOH7 isn't bonding with pad and is left
	 * floating internally. We need to enable internal pull-down for the pin
	 * to prevent leakage current, but IT81202/IT81302 doesn't have the
	 * capability to pull it down. We can only set it as output low,
	 * so we enable output low for it at initialization to prevent leakage.
	 */
	if (IS_ENABLED(IT83XX_GPIO_H7_DEFAULT_OUTPUT_LOW)) {
		IT83XX_GPIO_CTRL(GPIO_H, 7) = GPCR_PORT_PIN_MODE_OUTPUT;
		IT83XX_GPIO_DATA(GPIO_H) &= ~BIT(7);
	}

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
	}
}

/**
 * Handle a GPIO interrupt by calling the pins corresponding handler if
 * one exists.
 *
 * @param port		GPIO port (GPIO_*)
 * @param mask		GPIO mask
 */
static void gpio_interrupt(int port, uint8_t mask)
{
	int i = 0;
	const struct gpio_info *g = gpio_list;

	for (i = 0; i < GPIO_IH_COUNT; i++, g++) {
		if (port == g->port && (mask & g->mask)) {
			gpio_irq_handlers[i](i);
			return;
		}
	}
}

/**
 * Define one IRQ function to handle all GPIO interrupts. The IRQ determines
 * the interrupt number which was triggered, calls the master handler above,
 * and clears status registers.
 */
static void __gpio_irq(void)
{
	/* Determine interrupt number. */
	int irq = intc_get_ec_int();

	/* assert failure if interrupt number is zero */
	ASSERT(irq);

#if defined(HAS_TASK_KEYSCAN) && !defined(CONFIG_KEYBOARD_DISCRETE)
	if (irq == IT83XX_IRQ_WKINTC) {
		keyboard_raw_interrupt();
		return;
	}
#endif

#ifdef CONFIG_HOSTCMD_X86
	if (irq == IT83XX_IRQ_WKINTAD)
		return;
#endif

	/*
	 * Clear the WUC status register. Note the external pin first goes
	 * to the WUC module and is always edge triggered.
	 */
	*(wuesr(gpio_irqs[irq].wuc_group)) = gpio_irqs[irq].wuc_mask;

	/*
	 * Clear the interrupt controller  status register. Note the interrupt
	 * controller is level triggered from the WUC status.
	 */
	task_clear_pending_irq(irq);

	/* Run the GPIO master handler above with corresponding port/mask. */
	gpio_interrupt(gpio_irqs[irq].gpio_port, gpio_irqs[irq].gpio_mask);
}

/* Route all WKO interrupts coming from INT#2 into __gpio_irq. */
DECLARE_IRQ(CPU_INT_2_ALL_GPIOS, __gpio_irq, 1);
