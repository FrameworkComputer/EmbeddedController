/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog driver */

#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "watchdog.h"
#include "tfdp_chip.h"

void watchdog_reload(void)
{
	MCHP_WDG_KICK = 1;

	if (IS_ENABLED(CONFIG_WATCHDOG_HELP)) {
		/* Reload the auxiliary timer */
		MCHP_TMR16_CTL(0) &= ~BIT(5);
		MCHP_TMR16_CNT(0) = CONFIG_AUX_TIMER_PERIOD_MS;
		MCHP_TMR16_CTL(0) |= BIT(5);
	}
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

#if defined(CHIP_FAMILY_MEC152X) || defined(CHIP_FAMILY_MEC172X)
static void wdg_intr_enable(int enable)
{
	if (enable) {
		MCHP_WDG_STATUS = MCHP_WDG_STS_IRQ;
		MCHP_WDG_IEN = MCHP_WDG_IEN_IRQ_EN;
		MCHP_WDG_CTL |= MCHP_WDG_RESET_IRQ_EN;
		MCHP_INT_ENABLE(MCHP_WDG_GIRQ) = MCHP_WDG_GIRQ_BIT;
		task_enable_irq(MCHP_IRQ_WDG);
	} else {
		MCHP_WDG_IEN = 0U;
		MCHP_WDG_CTL &= ~(MCHP_WDG_RESET_IRQ_EN);
		MCHP_INT_DISABLE(MCHP_WDG_GIRQ) = MCHP_WDG_GIRQ_BIT;
		task_disable_irq(MCHP_IRQ_WDG);
	}
}
#else
static void wdg_intr_enable(int enable)
{
	(void) enable;
}
#endif


/*
 * MEC1701 WDG asserts chip reset on LOAD count expiration.
 * WDG interrupt is simulated using a 16-bit general purpose
 * timer whose period is sufficiently less that the WDG timeout
 * period allowing watchdog trace data to be saved.
 *
 * MEC152x adds interrupt capability to the WDT.
 * Enable MEC152x WDG interrupt. WDG event will assert
 * IRQ and kick itself starting another LOAD timeout.
 * After the new LOAD expires WDG will assert chip reset.
 * The WDG ISR calls watchdog trace save API, upon return we
 * enter a spin loop waiting for the LOAD period to expire.
 * WDG does not have a way to trigger an immediate reset except
 * by re-programming it.
 */
int watchdog_init(void)
{
	if (IS_ENABLED(CONFIG_WATCHDOG_HELP)) {
		/*
		 * MEC170x Watchdog does not warn us before expiring.
		 * Let's use a 16-bit timer as an auxiliary timer.
		 */

		/* Clear 16-bit basic timer 0 PCR sleep enable */
		MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_BTMR16_0);

		/* Stop the auxiliary timer if it's running */
		MCHP_TMR16_CTL(0) &= ~BIT(5);

		/* Enable auxiliary timer */
		MCHP_TMR16_CTL(0) |= BIT(0);

		/* Prescaler = 48000 -> 1kHz -> Period = 1 ms */
		MCHP_TMR16_CTL(0) = (MCHP_TMR16_CTL(0) & 0xffffU)
					| (47999 << 16);

		/* No auto restart */
		MCHP_TMR16_CTL(0) &= ~BIT(3);

		/* Count down */
		MCHP_TMR16_CTL(0) &= ~BIT(2);

		/* Enable interrupt from auxiliary timer */
		MCHP_TMR16_IEN(0) |= BIT(0);
		task_enable_irq(MCHP_IRQ_TIMER16_0);
		MCHP_INT_ENABLE(MCHP_TMR16_GIRQ) = MCHP_TMR16_GIRQ_BIT(0);

		/* Load and start the auxiliary timer */
		MCHP_TMR16_CNT(0) = CONFIG_AUX_TIMER_PERIOD_MS;
		MCHP_TMR16_CNT(0) |= BIT(5);
	}

	MCHP_WDG_CTL = 0;

	/* Clear WDT PCR sleep enable */
	MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_WDT);

	/* Set timeout. It takes 1007 us to decrement WDG_CNT by 1. */
	MCHP_WDG_LOAD = CONFIG_WATCHDOG_PERIOD_MS * 1000 / 1007;

	wdg_intr_enable(1);

	/*
	 * Start watchdog
	 * If chipset debug build enable feature to prevent watchdog from
	 * counting if a debug cable is attached to JTAG_RST#.
	 */
	if (IS_ENABLED(CONFIG_CHIPSET_DEBUG))
		MCHP_WDG_CTL |= (MCHP_WDT_CTL_ENABLE
			| MCHP_WDT_CTL_JTAG_STALL_EN);
	else
		MCHP_WDG_CTL |= MCHP_WDT_CTL_ENABLE;

	return EC_SUCCESS;
}

/* MEC152x Watchdog can fire an interrupt to CPU before system reset */
#if defined(CHIP_FAMILY_MEC152X) || defined(CHIP_FAMILY_MEC172X)

void __keep watchdog_check(uint32_t excep_lr, uint32_t excep_sp)
{
	/* Clear WDG first then aggregator */
	MCHP_WDG_STATUS = MCHP_WDG_STS_IRQ;
	MCHP_INT_SOURCE(MCHP_WDG_GIRQ) = MCHP_WDG_GIRQ_BIT;

	/* Cause WDG to reload again. */
	MCHP_WDG_KICK = 1;

	watchdog_trace(excep_lr, excep_sp);

	/* Reset system by re-programing WDT to trigger after 2 32KHz clocks */
	MCHP_WDG_CTL = 0; /* clear enable to allow write to load register */
	MCHP_WDG_LOAD = 2;
	MCHP_WDG_CTL |= MCHP_WDT_CTL_ENABLE;

}

/* ISR for watchdog warning naked will keep SP & LR */
void
IRQ_HANDLER(MCHP_IRQ_WDG)(void) __keep __attribute__((naked));
void IRQ_HANDLER(MCHP_IRQ_WDG)(void)
{
	/* Naked call so we can extract raw LR and SP */
	asm volatile("mov r0, lr\n"
			"mov r1, sp\n"
			/*
			 * Must push registers in pairs to keep 64-bit aligned
			 * stack for ARM EABI.  This also conveniently saves
			 * R0=LR so we can pass it to task_resched_if_needed.
			 */
			"push {r0, lr}\n"
			"bl watchdog_check\n"
			"pop {r0, lr}\n"
			"b task_resched_if_needed\n");
}

/* put the watchdog at the highest priority */
const struct irq_priority __keep IRQ_PRIORITY(MCHP_IRQ_WDG)
__attribute__((section(".rodata.irqprio")))
= {MCHP_IRQ_WDG, 0};

#else
/*
 * MEC1701 watchdog only resets. Use a 16-bit timer to fire in interrupt
 * for saving watchdog trace.
 */
#ifdef CONFIG_WATCHDOG_HELP
void __keep watchdog_check(uint32_t excep_lr, uint32_t excep_sp)
{
	/* Clear status */
	MCHP_TMR16_STS(0) |= 1;
	/* clear aggregator status */
	MCHP_INT_SOURCE(MCHP_TMR16_GIRQ) = MCHP_TMR16_GIRQ_BIT(0);

	watchdog_trace(excep_lr, excep_sp);
}

void
IRQ_HANDLER(MCHP_IRQ_TIMER16_0)(void) __keep __attribute__((naked));
void IRQ_HANDLER(MCHP_IRQ_TIMER16_0)(void)
{
	/* Naked call so we can extract raw LR and SP */
	asm volatile("mov r0, lr\n"
		     "mov r1, sp\n"
		     /*
		      * Must push registers in pairs to keep 64-bit aligned
		      * stack for ARM EABI.  This also conveniently saves
		      * R0=LR so we can pass it to task_resched_if_needed.
		      */
		     "push {r0, lr}\n"
		     "bl watchdog_check\n"
		     "pop {r0, lr}\n"
		     "b task_resched_if_needed\n");
}

/* Put the watchdog at the highest interrupt priority. */
const struct irq_priority __keep IRQ_PRIORITY(MCHP_IRQ_TIMER16_0)
	__attribute__((section(".rodata.irqprio")))
		= {MCHP_IRQ_TIMER16_0, 0};

#endif /* #ifdef CONFIG_WATCHDOG_HELP */
#endif /* #if defined(CHIP_FAMILY_MEC152X) || defined(CHIP_FAMILY_MEC172X) */
