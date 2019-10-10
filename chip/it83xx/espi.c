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

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)

struct vw_channel_t {
	uint8_t  index;         /* VW index of signal */
	uint8_t  level_mask;    /* level bit of signal */
	uint8_t  valid_mask;    /* valid bit of signal */
};

/* VW settings after the master enables the VW channel. */
static const struct vw_channel_t en_vw_setting[] = {
	/* EC sends SUS_ACK# = 1 VW to PCH. That does not apply to GLK SoC. */
#ifndef CONFIG_CHIPSET_GEMINILAKE
	{ESPI_SYSTEM_EVENT_VW_IDX_40,
		VW_LEVEL_FIELD(0),
		VW_VALID_FIELD(VW_IDX_40_SUS_ACK)},
#endif
};

/* VW settings after the master enables the OOB channel. */
static const struct vw_channel_t en_oob_setting[] = {
	{ESPI_SYSTEM_EVENT_VW_IDX_4,
		VW_LEVEL_FIELD(0),
		VW_VALID_FIELD(VW_IDX_4_OOB_RST_ACK)},
};

/* VW settings after the master enables the flash channel. */
static const struct vw_channel_t en_flash_setting[] = {
	{ESPI_SYSTEM_EVENT_VW_IDX_5,
		VW_LEVEL_FIELD(VW_IDX_5_BTLD_STATUS_DONE),
		VW_VALID_FIELD(VW_IDX_5_BTLD_STATUS_DONE)},
};

/* VW settings at host startup */
static const struct vw_channel_t vw_host_startup_setting[] = {
	{ESPI_SYSTEM_EVENT_VW_IDX_6,
		VW_LEVEL_FIELD(VW_IDX_6_SCI | VW_IDX_6_SMI |
				VW_IDX_6_RCIN | VW_IDX_6_HOST_RST_ACK),
		VW_VALID_FIELD(VW_IDX_6_SCI | VW_IDX_6_SMI |
				VW_IDX_6_RCIN | VW_IDX_6_HOST_RST_ACK)},
};

#define VW_CHAN(name, idx, level, valid) \
	[(name - VW_SIGNAL_START)] = {idx, level, valid}

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
BUILD_ASSERT(ARRAY_SIZE(vw_channel_list) == VW_SIGNAL_COUNT);

/* Get vw index & value information by signal */
static int espi_vw_get_signal_index(enum espi_vw_signal event)
{
	uint32_t i = event - VW_SIGNAL_START;

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

/* Configure virtual wire outputs */
static void espi_configure_vw(const struct vw_channel_t *settings,
						size_t entries)
{
	size_t i;

	for (i = 0; i < entries; i++)
		IT83XX_ESPI_VWIDX(settings[i].index) |=
			(settings[i].level_mask | settings[i].valid_mask);
}

static void espi_vw_host_startup(void)
{
	espi_configure_vw(vw_host_startup_setting,
				ARRAY_SIZE(vw_host_startup_setting));
}

static void espi_vw_no_isr(uint8_t flag_changed, uint8_t vw_evt)
{
	CPRINTS("espi VW interrupt event is ignored! (bit%d at VWCTRL1)",
								vw_evt);
}

#ifndef CONFIG_CHIPSET_GEMINILAKE
static void espi_vw_idx41_isr(uint8_t flag_changed, uint8_t vw_evt)
{
	if (flag_changed & VW_LEVEL_FIELD(VW_IDX_41_SUS_WARN))
		espi_vw_set_wire(VW_SUS_ACK, espi_vw_get_wire(VW_SUS_WARN_L));
}
#endif

static void espi_vw_idx7_isr(uint8_t flag_changed, uint8_t vw_evt)
{
	if (flag_changed & VW_LEVEL_FIELD(VW_IDX_7_HOST_RST_WARN))
		espi_vw_set_wire(VW_HOST_RST_ACK,
			espi_vw_get_wire(VW_HOST_RST_WARN));
}

#ifdef CONFIG_CHIPSET_RESET_HOOK
static void espi_chipset_reset(void)
{
	hook_notify(HOOK_CHIPSET_RESET);
}
DECLARE_DEFERRED(espi_chipset_reset);
#endif

static void espi_vw_idx3_isr(uint8_t flag_changed, uint8_t vw_evt)
{
	if (flag_changed & VW_LEVEL_FIELD(VW_IDX_3_PLTRST)) {
		int pltrst = espi_vw_get_wire(VW_PLTRST_L);

		if (pltrst) {
			espi_vw_host_startup();
		} else {
#ifdef CONFIG_CHIPSET_RESET_HOOK
			hook_call_deferred(&espi_chipset_reset_data, MSEC);
#endif
			/* Store port 80 reset event */
			port_80_write(PORT_80_EVENT_RESET);
		}

		CPRINTS("VW PLTRST_L %sasserted", pltrst ? "de" : "");
	}

	if (flag_changed & VW_LEVEL_FIELD(VW_IDX_3_OOB_RST_WARN))
		espi_vw_set_wire(VW_OOB_RST_ACK,
			espi_vw_get_wire(VW_OOB_RST_WARN));
}

static void espi_vw_idx2_isr(uint8_t flag_changed, uint8_t vw_evt)
{
	if (flag_changed & VW_LEVEL_FIELD(VW_IDX_2_SLP_S3))
		power_signal_interrupt(VW_SLP_S3_L);
	if (flag_changed & VW_LEVEL_FIELD(VW_IDX_2_SLP_S4))
		power_signal_interrupt(VW_SLP_S4_L);
	if (flag_changed & VW_LEVEL_FIELD(VW_IDX_2_SLP_S5))
		power_signal_interrupt(VW_SLP_S5_L);
}

struct vw_interrupt_t {
	void (*vw_isr)(uint8_t flag_changed, uint8_t vw_evt);
	uint8_t vw_index;
};

/*
 * The ISR of espi VW interrupt in array needs to match bit order in
 * IT83XX_ESPI_VWCTRL1 register.
 */
#ifdef CONFIG_CHIPSET_GEMINILAKE
static const struct vw_interrupt_t vw_isr_list[] = {
	[0] = {espi_vw_idx2_isr,  ESPI_SYSTEM_EVENT_VW_IDX_2},
	[1] = {espi_vw_idx3_isr,  ESPI_SYSTEM_EVENT_VW_IDX_3},
	[2] = {espi_vw_idx7_isr,  ESPI_SYSTEM_EVENT_VW_IDX_7},
	[3] = {espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_41},
	[4] = {espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_42},
	[5] = {espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_43},
	[6] = {espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_44},
	[7] = {espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_47},
};
#else
static const struct vw_interrupt_t vw_isr_list[] = {
	[0] = {espi_vw_idx2_isr,  ESPI_SYSTEM_EVENT_VW_IDX_2},
	[1] = {espi_vw_idx3_isr,  ESPI_SYSTEM_EVENT_VW_IDX_3},
	[2] = {espi_vw_idx7_isr,  ESPI_SYSTEM_EVENT_VW_IDX_7},
	[3] = {espi_vw_idx41_isr, ESPI_SYSTEM_EVENT_VW_IDX_41},
	[4] = {espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_42},
	[5] = {espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_43},
	[6] = {espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_44},
	[7] = {espi_vw_no_isr,    ESPI_SYSTEM_EVENT_VW_IDX_47},
};
#endif

/*
 * This is used to record the previous VW valid / level field state to discover
 * changes. Then do following sequence only when state is changed.
 */
static uint8_t vw_index_flag[ARRAY_SIZE(vw_isr_list)];

void espi_vw_interrupt(void)
{
	int i;
	uint8_t vwidx_updated = IT83XX_ESPI_VWCTRL1;

#ifdef IT83XX_ESPI_VWCTRL1_WRITE_FF_CLEAR
	/* For IT8320BX, we have to write 0xff to clear pending bit.*/
	IT83XX_ESPI_VWCTRL1 = 0xff;
#else
	/* write-1 to clear */
	IT83XX_ESPI_VWCTRL1 = vwidx_updated;
#endif
	task_clear_pending_irq(IT83XX_IRQ_ESPI_VW);

	for (i = 0; i < ARRAY_SIZE(vw_isr_list); i++) {
		if (vwidx_updated & BIT(i)) {
			uint8_t idx_flag;

			idx_flag = IT83XX_ESPI_VWIDX(vw_isr_list[i].vw_index);
			vw_isr_list[i].vw_isr(vw_index_flag[i] ^ idx_flag, i);
			vw_index_flag[i] = idx_flag;
		}
	}
}

static void espi_reset_vw_index_flags(void)
{
	int i;

	/* reset vw_index_flag */
	for (i = 0; i < ARRAY_SIZE(vw_isr_list); i++)
		vw_index_flag[i] = IT83XX_ESPI_VWIDX(vw_isr_list[i].vw_index);
}

#ifdef IT83XX_ESPI_RESET_MODULE_BY_FW
void __ram_code espi_fw_reset_module(void)
{
	/*
	 * (b/111480168): Force a reset of logic VCC domain in EC. This will
	 * reset both LPC and eSPI blocks. The IT8320DX spec describes the
	 * purpose of these bits as deciding whether VCC power status is used as
	 * an internal "power good" signal. However, toggling this field while
	 * VCC is applied results in resettig VCC domain logic in EC. This code
	 * must reside in SRAM to prevent DMA address corruption.
	 *
	 * bit[7-6]:
	 * 00b: The VCC power status is treated as power-off.
	 * 01b: The VCC power status is treated as power-on.
	 */
	IT83XX_GCTRL_RSTS = (IT83XX_GCTRL_RSTS & ~0xc0);
	IT83XX_GCTRL_RSTS = (IT83XX_GCTRL_RSTS & ~0xc0) | BIT(6);
}
#endif

void espi_reset_pin_asserted_interrupt(enum gpio_signal signal)
{
#ifdef IT83XX_ESPI_RESET_MODULE_BY_FW
	espi_fw_reset_module();
#endif
	/* reset vw_index_flag when espi_reset# asserted. */
	espi_reset_vw_index_flags();
}

static int espi_get_reset_enable_config(void)
{
	uint8_t config;
	const struct gpio_info *espi_rst = gpio_list + GPIO_ESPI_RESET_L;

	/*
	 * Determine if eSPI HW reset is connected to eiter B7 or D2.
	 * bit[2-1]:
	 * 00b: reserved.
	 * 01b: espi_reset# is enabled on GPB7.
	 * 10b: espi_reset# is enabled on GPD2.
	 * 11b: reset is disabled.
	 */
	if (espi_rst->port == GPIO_D && espi_rst->mask == BIT(2)) {
		config = IT83XX_GPIO_GCR_LPC_RST_D2;
	} else if (espi_rst->port == GPIO_B && espi_rst->mask == BIT(7)) {
		config = IT83XX_GPIO_GCR_LPC_RST_B7;
	} else {
		config = IT83XX_GPIO_GCR_LPC_RST_DISABLE;
		CPRINTS("EC's espi_reset pin is not enabled correctly");
	}

	return config;
}

static void espi_enable_reset(void)
{
	int config = espi_get_reset_enable_config();

#ifdef IT83XX_ESPI_RESET_MODULE_BY_FW
	/*
	 * Need to overwrite the config to ensure that eSPI HW reset is
	 * disabled. The reset function is instead handled by FW in the
	 * interrupt handler.
	 */
	config = IT83XX_GPIO_GCR_LPC_RST_DISABLE;
	CPRINTS("EC's espi_reset pin hw auto reset is disabled");

#endif
	IT83XX_GPIO_GCR = (IT83XX_GPIO_GCR & ~0x6) |
		(config << IT83XX_GPIO_GCR_LPC_RST_POS);

	/* enable interrupt of EC's espi_reset pin */
	gpio_clear_pending_interrupt(GPIO_ESPI_RESET_L);
	gpio_enable_interrupt(GPIO_ESPI_RESET_L);
}

/* Interrupt event of master enables the VW channel. */
static void espi_vw_en_asserted(uint8_t evt)
{
	/*
	 * Configure slave to master virtual wire outputs after receiving
	 * the event of master enables the VW channel.
	 */
	espi_configure_vw(en_vw_setting, ARRAY_SIZE(en_vw_setting));
}

/* Interrupt event of master enables the OOB channel. */
static void espi_oob_en_asserted(uint8_t evt)
{
	/*
	 * Configure slave to master virtual wire outputs after receiving
	 * the event of master enables the OOB channel.
	 */
	espi_configure_vw(en_oob_setting, ARRAY_SIZE(en_oob_setting));
}

/* Interrupt event of master enables the flash channel. */
static void espi_flash_en_asserted(uint8_t evt)
{
	/*
	 * Configure slave to master virtual wire outputs after receiving
	 * the event of master enables the flash channel.
	 */
	espi_configure_vw(en_flash_setting, ARRAY_SIZE(en_flash_setting));
}

static void espi_no_isr(uint8_t evt)
{
	CPRINTS("espi interrupt event is ignored! (bit%d at ESGCTRL0)", evt);
}

/*
 * The ISR of espi interrupt event in array need to be matched bit order in
 * IT83XX_ESPI_ESGCTRL0 register.
 */
static void (*espi_isr[])(uint8_t evt) = {
	[0] = espi_no_isr,
	[1] = espi_vw_en_asserted,
	[2] = espi_oob_en_asserted,
	[3] = espi_flash_en_asserted,
	[4] = espi_no_isr,
	[5] = espi_no_isr,
	[6] = espi_no_isr,
	[7] = espi_no_isr,
};

void espi_interrupt(void)
{
	int i;
	/* get espi interrupt events */
	uint8_t espi_event = IT83XX_ESPI_ESGCTRL0;

	/* write-1 to clear */
	IT83XX_ESPI_ESGCTRL0 = espi_event;
	/* process espi interrupt events */
	for (i = 0; i < ARRAY_SIZE(espi_isr); i++) {
		if (espi_event & BIT(i))
			espi_isr[i](i);
	}
	/*
	 * bit7: the slave has received a peripheral posted/completion.
	 * This bit indicates the slave has received a packet from eSPI
	 * peripheral channel. We can check cycle type (bit[3-0] at ESPCTRL0)
	 * and make corresponding modification if needed.
	 */
	if (IT83XX_ESPI_ESPCTRL0 & ESPI_INTERRUPT_EVENT_PUT_PC) {
		/* write-1-clear to release PC_FREE */
		IT83XX_ESPI_ESPCTRL0 = ESPI_INTERRUPT_EVENT_PUT_PC;
		CPRINTS("A packet from peripheral channel is ignored!");
	}

	task_clear_pending_irq(IT83XX_IRQ_ESPI);
}

#ifdef IT83XX_ESPI_INHIBIT_CS_BY_PAD_DISABLED
/* Enable/Disable eSPI pad */
void espi_enable_pad(int enable)
{
	if (enable)
		/* Enable eSPI pad. */
		IT83XX_ESPI_ESGCTRL2 &= ~BIT(6);
	else
		/* Disable eSPI pad. */
		IT83XX_ESPI_ESGCTRL2 |= BIT(6);
}
#endif

void espi_init(void)
{
	/*
	 * bit[2-0], the maximum frequency of operation supported by slave:
	 * 000b: 20MHz
	 * 001b: 25MHz
	 * 010b: 33MHz
	 * 011b: 50MHz
	 * 100b: 66MHz
	 */
#ifdef IT83XX_ESPI_SLAVE_MAX_FREQ_CONFIGURABLE
	IT83XX_ESPI_GCAC1 = (IT83XX_ESPI_GCAC1 & ~0x7) | BIT(2);
#endif
	/* reset vw_index_flag at initialization */
	espi_reset_vw_index_flags();

	/*
	 * bit[3]: The reset source of PNPCFG is RSTPNP bit in RSTCH
	 * register and WRST#.
	 */
	IT83XX_GCTRL_RSTS &= ~BIT(3);
	task_clear_pending_irq(IT83XX_IRQ_ESPI_VW);
	/* bit7: VW interrupt enable */
	IT83XX_ESPI_VWCTRL0 |= BIT(7);
	task_enable_irq(IT83XX_IRQ_ESPI_VW);

	/* bit7: eSPI interrupt enable */
	IT83XX_ESPI_ESGCTRL1 |= BIT(7);
	/* bit4: eSPI to WUC enable */
	IT83XX_ESPI_ESGCTRL2 |= BIT(4);
	task_enable_irq(IT83XX_IRQ_ESPI);

	/* enable interrupt and reset from eSPI_reset# */
	espi_enable_reset();
}
