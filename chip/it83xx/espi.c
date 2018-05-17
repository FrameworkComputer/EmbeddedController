/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ESPI module for Chrome EC */

#include "console.h"
#include "espi.h"
#include "hooks.h"
#include "port80.h"
#include "power.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

#define CHIP_ESPI_VW_INTERRUPT_NUM 8

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)

struct vw_channel_t {
	uint8_t  index;         /* VW index of signal */
	uint8_t  level_mask;    /* level bit of signal */
	uint8_t  valid_mask;    /* valid bit of signal */
};

/* VW settings at initialization */
#ifdef CONFIG_CHIPSET_GEMINILAKE
static const struct vw_channel_t vw_init_setting[] = {
	{ESPI_SYSTEM_EVENT_VW_IDX_4,
		VW_LEVEL_FIELD(0),
		VW_VALID_FIELD(VW_IDX_4_OOB_RST_ACK)},
	{ESPI_SYSTEM_EVENT_VW_IDX_5,
		VW_LEVEL_FIELD(VW_IDX_5_BTLD_STATUS_DONE),
		VW_VALID_FIELD(VW_IDX_5_BTLD_STATUS_DONE)},
};
#else
static const struct vw_channel_t vw_init_setting[] = {
	{ESPI_SYSTEM_EVENT_VW_IDX_4,
		VW_LEVEL_FIELD(0),
		VW_VALID_FIELD(VW_IDX_4_OOB_RST_ACK)},
	{ESPI_SYSTEM_EVENT_VW_IDX_5,
		VW_LEVEL_FIELD(VW_IDX_5_BTLD_STATUS_DONE),
		VW_VALID_FIELD(VW_IDX_5_BTLD_STATUS_DONE)},
	{ESPI_SYSTEM_EVENT_VW_IDX_40,
		VW_LEVEL_FIELD(0),
		VW_VALID_FIELD(VW_IDX_40_SUS_ACK)},
};
#endif

/* VW settings at host startup */
static const struct vw_channel_t vw_host_startup_setting[] = {
	{ESPI_SYSTEM_EVENT_VW_IDX_6,
		VW_LEVEL_FIELD(VW_IDX_6_SCI | VW_IDX_6_SMI |
				VW_IDX_6_RCIN | VW_IDX_6_HOST_RST_ACK),
		VW_VALID_FIELD(VW_IDX_6_SCI | VW_IDX_6_SMI |
				VW_IDX_6_RCIN | VW_IDX_6_HOST_RST_ACK)},
};

#define VW_CHAN(name, idx, level, valid) \
	[(name - VW_SIGNAL_BASE)] = {idx, level, valid}

/* VW signals used in eSPI (NOTE: must match order of enum espi_vw_signal). */
static const struct vw_channel_t vw_channel_list[] = {
	/* index 02h: master to slave. */
	VW_CHAN(VW_SLP_S3_L,
		ESPI_SYSTEM_EVENT_VW_IDX_2,
		VW_LEVEL_FIELD(VW_IDX_2_SLP_S3),
		VW_VALID_FIELD(VW_IDX_2_SLP_S3)),
	VW_CHAN(VW_SLP_S4_L,
		ESPI_SYSTEM_EVENT_VW_IDX_2,
		VW_LEVEL_FIELD(VW_IDX_2_SLP_S4),
		VW_VALID_FIELD(VW_IDX_2_SLP_S4)),
	VW_CHAN(VW_SLP_S5_L,
		ESPI_SYSTEM_EVENT_VW_IDX_2,
		VW_LEVEL_FIELD(VW_IDX_2_SLP_S5),
		VW_VALID_FIELD(VW_IDX_2_SLP_S5)),
	/* index 03h: master to slave. */
	VW_CHAN(VW_SUS_STAT_L,
		ESPI_SYSTEM_EVENT_VW_IDX_3,
		VW_LEVEL_FIELD(VW_IDX_3_SUS_STAT),
		VW_VALID_FIELD(VW_IDX_3_SUS_STAT)),
	VW_CHAN(VW_PLTRST_L,
		ESPI_SYSTEM_EVENT_VW_IDX_3,
		VW_LEVEL_FIELD(VW_IDX_3_PLTRST),
		VW_VALID_FIELD(VW_IDX_3_PLTRST)),
	VW_CHAN(VW_OOB_RST_WARN,
		ESPI_SYSTEM_EVENT_VW_IDX_3,
		VW_LEVEL_FIELD(VW_IDX_3_OOB_RST_WARN),
		VW_VALID_FIELD(VW_IDX_3_OOB_RST_WARN)),
	/* index 04h: slave to master. */
	VW_CHAN(VW_OOB_RST_ACK,
		ESPI_SYSTEM_EVENT_VW_IDX_4,
		VW_LEVEL_FIELD(VW_IDX_4_OOB_RST_ACK),
		VW_VALID_FIELD(VW_IDX_4_OOB_RST_ACK)),
	VW_CHAN(VW_WAKE_L,
		ESPI_SYSTEM_EVENT_VW_IDX_4,
		VW_LEVEL_FIELD(VW_IDX_4_WAKE),
		VW_VALID_FIELD(VW_IDX_4_WAKE)),
	VW_CHAN(VW_PME_L,
		ESPI_SYSTEM_EVENT_VW_IDX_4,
		VW_LEVEL_FIELD(VW_IDX_4_PME),
		VW_VALID_FIELD(VW_IDX_4_PME)),
	/* index 05h: slave to master. */
	VW_CHAN(VW_ERROR_FATAL,
		ESPI_SYSTEM_EVENT_VW_IDX_5,
		VW_LEVEL_FIELD(VW_IDX_5_FATAL),
		VW_VALID_FIELD(VW_IDX_5_FATAL)),
	VW_CHAN(VW_ERROR_NON_FATAL,
		ESPI_SYSTEM_EVENT_VW_IDX_5,
		VW_LEVEL_FIELD(VW_IDX_5_NON_FATAL),
		VW_VALID_FIELD(VW_IDX_5_NON_FATAL)),
	VW_CHAN(VW_SLAVE_BTLD_STATUS_DONE,
		ESPI_SYSTEM_EVENT_VW_IDX_5,
		VW_LEVEL_FIELD(VW_IDX_5_BTLD_STATUS_DONE),
		VW_VALID_FIELD(VW_IDX_5_BTLD_STATUS_DONE)),
	/* index 06h: slave to master. */
	VW_CHAN(VW_SCI_L,
		ESPI_SYSTEM_EVENT_VW_IDX_6,
		VW_LEVEL_FIELD(VW_IDX_6_SCI),
		VW_VALID_FIELD(VW_IDX_6_SCI)),
	VW_CHAN(VW_SMI_L,
		ESPI_SYSTEM_EVENT_VW_IDX_6,
		VW_LEVEL_FIELD(VW_IDX_6_SMI),
		VW_VALID_FIELD(VW_IDX_6_SMI)),
	VW_CHAN(VW_RCIN_L,
		ESPI_SYSTEM_EVENT_VW_IDX_6,
		VW_LEVEL_FIELD(VW_IDX_6_RCIN),
		VW_VALID_FIELD(VW_IDX_6_RCIN)),
	VW_CHAN(VW_HOST_RST_ACK,
		ESPI_SYSTEM_EVENT_VW_IDX_6,
		VW_LEVEL_FIELD(VW_IDX_6_HOST_RST_ACK),
		VW_VALID_FIELD(VW_IDX_6_HOST_RST_ACK)),
	/* index 07h: master to slave. */
	VW_CHAN(VW_HOST_RST_WARN,
		ESPI_SYSTEM_EVENT_VW_IDX_7,
		VW_LEVEL_FIELD(VW_IDX_7_HOST_RST_WARN),
		VW_VALID_FIELD(VW_IDX_7_HOST_RST_WARN)),
	/* index 40h: slave to master. */
	VW_CHAN(VW_SUS_ACK,
		ESPI_SYSTEM_EVENT_VW_IDX_40,
		VW_LEVEL_FIELD(VW_IDX_40_SUS_ACK),
		VW_VALID_FIELD(VW_IDX_40_SUS_ACK)),
	/* index 41h: master to slave. */
	VW_CHAN(VW_SUS_WARN_L,
		ESPI_SYSTEM_EVENT_VW_IDX_41,
		VW_LEVEL_FIELD(VW_IDX_41_SUS_WARN),
		VW_VALID_FIELD(VW_IDX_41_SUS_WARN)),
	VW_CHAN(VW_SUS_PWRDN_ACK_L,
		ESPI_SYSTEM_EVENT_VW_IDX_41,
		VW_LEVEL_FIELD(VW_IDX_41_SUS_PWRDN_ACK),
		VW_VALID_FIELD(VW_IDX_41_SUS_PWRDN_ACK)),
	VW_CHAN(VW_SLP_A_L,
		ESPI_SYSTEM_EVENT_VW_IDX_41,
		VW_LEVEL_FIELD(VW_IDX_41_SLP_A),
		VW_VALID_FIELD(VW_IDX_41_SLP_A)),
	/* index 42h: master to slave. */
	VW_CHAN(VW_SLP_LAN,
		ESPI_SYSTEM_EVENT_VW_IDX_42,
		VW_LEVEL_FIELD(VW_IDX_42_SLP_LAN),
		VW_VALID_FIELD(VW_IDX_42_SLP_LAN)),
	VW_CHAN(VW_SLP_WLAN,
		ESPI_SYSTEM_EVENT_VW_IDX_42,
		VW_LEVEL_FIELD(VW_IDX_42_SLP_WLAN),
		VW_VALID_FIELD(VW_IDX_42_SLP_WLAN)),
};
BUILD_ASSERT(ARRAY_SIZE(vw_channel_list) ==
		(VW_SIGNAL_BASE_END - VW_SIGNAL_BASE));

/* Get vw index & value information by signal */
static int espi_vw_get_signal_index(enum espi_vw_signal event)
{
	uint32_t i = event - VW_SIGNAL_BASE;

	return (i < ARRAY_SIZE(vw_channel_list)) ? i : -1;
}

/**
 * Set eSPI Virtual-Wire signal to Host
 *
 * @param signal vw signal needs to set
 * @param level  level of vw signal
 * @return EC_SUCCESS, or non-zero if error.
 */
int espi_vw_set_wire(enum espi_vw_signal signal, uint8_t level)
{
	/* Get index of vw signal list by signale name */
	int i = espi_vw_get_signal_index(signal);

	if (i < 0)
		return EC_ERROR_PARAM1;

	/* critical section with interrupts off */
	interrupt_disable();
	if (level)
		IT83XX_ESPI_VWIDX(vw_channel_list[i].index) |=
			vw_channel_list[i].level_mask;
	else
		IT83XX_ESPI_VWIDX(vw_channel_list[i].index) &=
			~vw_channel_list[i].level_mask;
	/* restore interrupts */
	interrupt_enable();

	return EC_SUCCESS;
}

/**
 * Get eSPI Virtual-Wire signal from host
 *
 * @param signal vw signal needs to get
 * @return      1: set by host, otherwise: no signal
 */
int espi_vw_get_wire(enum espi_vw_signal signal)
{
	/* Get index of vw signal list by signale name */
	int i = espi_vw_get_signal_index(signal);

	if (i < 0)
		return 0;

	/* Not valid */
	if (!(IT83XX_ESPI_VWIDX(vw_channel_list[i].index) &
		vw_channel_list[i].valid_mask))
		return 0;

	return !!(IT83XX_ESPI_VWIDX(vw_channel_list[i].index) &
		vw_channel_list[i].level_mask);
}

/**
 * Enable VW interrupt of power sequence signal
 *
 * @param signal vw signal needs to enable interrupt
 * @return EC_SUCCESS, or non-zero if error.
 */
int espi_vw_enable_wire_int(enum espi_vw_signal signal)
{
	/*
	 * Common code calls this function to enable VW interrupt of power
	 * sequence signal.
	 * IT83xx only use a bit (bit7@IT83XX_ESPI_VWCTRL0) to enable VW
	 * interrupt.
	 * VW interrupt will be triggerd with any updated VW index flag
	 * if this control bit is set.
	 * So we will always return success here.
	 */
	return EC_SUCCESS;
}

/**
 * Disable VW interrupt of power sequence signal
 *
 * @param signal vw signal needs to disable interrupt
 * @return EC_SUCCESS, or non-zero if error.
 */
int espi_vw_disable_wire_int(enum espi_vw_signal signal)
{
	/*
	 * We can't disable VW interrupt of power sequence signal
	 * individually.
	 */
	return EC_ERROR_UNIMPLEMENTED;
}

static void espi_vw_host_startup(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vw_host_startup_setting); i++)
		IT83XX_ESPI_VWIDX(vw_host_startup_setting[i].index) =
			(vw_host_startup_setting[i].level_mask |
			vw_host_startup_setting[i].valid_mask);
}

static void espi_vw_no_isr(uint8_t flag_changed)
{
}

#ifndef CONFIG_CHIPSET_GEMINILAKE
static void espi_vw_idx41_isr(uint8_t flag_changed)
{
	if (flag_changed & VW_LEVEL_FIELD(VW_IDX_41_SUS_WARN))
		espi_vw_set_wire(VW_SUS_ACK, espi_vw_get_wire(VW_SUS_WARN_L));
}
#endif

static void espi_vw_idx7_isr(uint8_t flag_changed)
{
	if (flag_changed & VW_LEVEL_FIELD(VW_IDX_7_HOST_RST_WARN))
		espi_vw_set_wire(VW_HOST_RST_ACK,
			espi_vw_get_wire(VW_HOST_RST_WARN));
}

static void espi_vw_idx3_isr(uint8_t flag_changed)
{
	if (flag_changed & VW_LEVEL_FIELD(VW_IDX_3_PLTRST)) {
		int pltrst = espi_vw_get_wire(VW_PLTRST_L);

		if (pltrst)
			espi_vw_host_startup();
		else
			/* Store port 80 reset event */
			port_80_write(PORT_80_EVENT_RESET);

		CPRINTS("PLTRST_L %sasserted", pltrst ? "de" : "");
	}

	if (flag_changed & VW_LEVEL_FIELD(VW_IDX_3_OOB_RST_WARN))
		espi_vw_set_wire(VW_OOB_RST_ACK,
			espi_vw_get_wire(VW_OOB_RST_WARN));
}

static void espi_vw_idx2_isr(uint8_t flag_changed)
{
	if (flag_changed & VW_LEVEL_FIELD(VW_IDX_2_SLP_S3))
		power_signal_interrupt(VW_SLP_S3_L);
	if (flag_changed & VW_LEVEL_FIELD(VW_IDX_2_SLP_S4))
		power_signal_interrupt(VW_SLP_S4_L);
	if (flag_changed & VW_LEVEL_FIELD(VW_IDX_2_SLP_S5))
		power_signal_interrupt(VW_SLP_S5_L);
}

struct vw_interrupt_t {
	void (*vw_isr)(uint8_t flag_changed);
	uint8_t vw_index;
};

#ifdef CONFIG_CHIPSET_GEMINILAKE
static const struct vw_interrupt_t vw_isr_list[CHIP_ESPI_VW_INTERRUPT_NUM] = {
	{espi_vw_idx2_isr,  ESPI_SYSTEM_EVENT_VW_IDX_2},
	{espi_vw_idx3_isr,  ESPI_SYSTEM_EVENT_VW_IDX_3},
	{espi_vw_idx7_isr,  ESPI_SYSTEM_EVENT_VW_IDX_7},
	{espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_41},
	{espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_42},
	{espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_43},
	{espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_44},
	{espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_47},
};
#else
static const struct vw_interrupt_t vw_isr_list[CHIP_ESPI_VW_INTERRUPT_NUM] = {
	{espi_vw_idx2_isr,  ESPI_SYSTEM_EVENT_VW_IDX_2},
	{espi_vw_idx3_isr,  ESPI_SYSTEM_EVENT_VW_IDX_3},
	{espi_vw_idx7_isr,  ESPI_SYSTEM_EVENT_VW_IDX_7},
	{espi_vw_idx41_isr, ESPI_SYSTEM_EVENT_VW_IDX_41},
	{espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_42},
	{espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_43},
	{espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_44},
	{espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_47},
};
#endif

/*
 * This is used to record the previous VW valid / level field state to discover
 * changes. Then do following sequence only when state is changed.
 */
static uint8_t vw_index_flag[CHIP_ESPI_VW_INTERRUPT_NUM];

void espi_vw_interrupt(void)
{
	int i;
	uint8_t vwidx_updated = IT83XX_ESPI_VWCTRL1;

	/*
	 * TODO(b:68918637): write-1 clear bug.
	 * for now, we have to write 0xff to clear pending bit.
	 */
#if 0
	IT83XX_ESPI_VWCTRL1 = vwidx_updated;
#else
	IT83XX_ESPI_VWCTRL1 = 0xff;
#endif
	task_clear_pending_irq(IT83XX_IRQ_ESPI_VW);

	for (i = 0; i < CHIP_ESPI_VW_INTERRUPT_NUM; i++) {
		if (vwidx_updated & (1 << i)) {
			uint8_t idx_flag;

			idx_flag = IT83XX_ESPI_VWIDX(vw_isr_list[i].vw_index);
			vw_isr_list[i].vw_isr(vw_index_flag[i] ^ idx_flag);
			vw_index_flag[i] = idx_flag;
		}
	}
}

void espi_interrupt(void)
{
}

void espi_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vw_init_setting); i++)
		IT83XX_ESPI_VWIDX(vw_init_setting[i].index) =
			(vw_init_setting[i].level_mask |
			vw_init_setting[i].valid_mask);

	for (i = 0; i < CHIP_ESPI_VW_INTERRUPT_NUM; i++)
		vw_index_flag[i] = IT83XX_ESPI_VWIDX(vw_isr_list[i].vw_index);

	/*
	 * bit[3]: The reset source of PNPCFG is RSTPNP bit in RSTCH
	 * register and WRST#.
	 */
	IT83XX_GCTRL_RSTS &= ~(1 << 3);
	task_clear_pending_irq(IT83XX_IRQ_ESPI_VW);
	/* bit7: VW interrupt enable */
	IT83XX_ESPI_VWCTRL0 |= (1 << 7);
	task_enable_irq(IT83XX_IRQ_ESPI_VW);
}
