/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ESPI module for Chrome EC */

#include "registers.h"
#include "system.h"
#include "task.h"
#include "chipset.h"
#include "console.h"
#include "uart.h"
#include "util.h"
#include "power.h"
#include "espi.h"
#include "lpc_chip.h"
#include "hooks.h"
#include "timer.h"

/* Console output macros */
#if !(DEBUG_ESPI)
#define CPUTS(...)
#define CPRINTS(...)
#else
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)
#endif

/* Default eSPI configuration for VW events */
struct vwevms_config_t {
	uint8_t idx;        /* VW index */
	uint8_t idx_en;     /* Index enable */
	uint8_t pltrst_en;  /* Enable reset by PLTRST assert */
	uint8_t espirst_en; /* Enable reset by eSPI_RST assert */
	uint8_t int_en;     /* Interrupt/Wake-up enable */
};

struct vwevsm_config_t {
	uint8_t idx;        /* VW index */
	uint8_t idx_en;     /* Index enable */
	uint8_t pltrst_en;  /* Enable reset by PLTRST assert */
	uint8_t cdrst_en;   /* Enable cold reset */
	uint8_t valid;      /* Valid VW mask */
};

/* Default MIWU configurations for VW events */
struct host_wui_item {
	uint16_t table : 2; /* MIWU table 0-2 */
	uint16_t group : 3; /* MIWU group 0-7 */
	uint16_t num   : 3; /* MIWU bit   0-7 */
	uint16_t edge  : 4; /* MIWU edge trigger type rising/falling/any */
};

/* Mapping item between VW signal, index and value */
struct vw_event_t {
	uint16_t name;     /* Name of signal */
	uint8_t  evt_idx;  /* VW index of signal */
	uint8_t  evt_val;  /* VW value of signal */
};

/* Default settings of VWEVMS registers (Please refer Table.43/44) */
static const struct vwevms_config_t espi_in_list[] = {
	/* IDX EN ENPL ENESP IE/WE          VW Event Bit 0 - 3 (M->S)         */
	{0x02,  1,  0,  0,  1},  /* SLP_S3#,   SLP_S4#,    SLP_S5#,   Reserve */
	{0x03,  1,  0,  1,  1},  /* SUS_STAT#, PLTRST#,    ORST_WARN, Reserve */
	{0x07,  1,  1,  1,  1},  /* HRST_WARN, SMIOUT#,    NMIOUT#,   Reserve */
	{0x41,  1,  0,  1,  1},  /* SUS_WARN#, SPWRDN_ACK, Reserve,   SLP_A#  */
	{0x42,  1,  0,  0,  1},  /* SLP_LAN#,  SLP_WAN#,   Reserve,   Reserve */
	{0x47,  1,  1,  1,  1},  /* HOST_C10,  Reserve,    Reserve,   Reserve */
};

/* Default settings of VWEVSM registers (Please refer Table.43/44) */
static const struct vwevsm_config_t espi_out_list[] = {
	/* IDX EN ENPL ENCDR VDMASK         VW Event Bit 0 - 3 (S->M)         */
	{0x04,  1,  0,  0, 0x0D}, /* ORST_ACK,   Reserve, WAKE#,   PME#       */
	{0x05,  1,  0,  0, 0x0F}, /* SLV_BL_DNE, ERR_F,   ERR_NF,  SLV_BL_STS */
#ifdef CONFIG_SCI_GPIO
	{0x06,  1,  1,  0, 0x0C}, /* SCI#,       SMI#,    RCIN#,   HRST_ACK   */
#else
	{0x06,  1,  1,  0, 0x0F}, /* SCI#,       SMI#,    RCIN#,   HRST_ACK   */
#endif
	{0x40,  1,  0,  0, 0x01}, /* SUS_ACK,    Reserve, Reserve, Reserve    */
};

/* eSPI interrupts used in MIWU */
static const struct host_wui_item espi_vw_int_list[] = {
	/* ESPI_RESET  */
	{MIWU_TABLE_0, MIWU_GROUP_5, 5, MIWU_EDGE_FALLING},
	/* SLP_S3 */
	{MIWU_TABLE_2, MIWU_GROUP_1, 0, MIWU_EDGE_ANYING},
	/* SLP_S4 */
	{MIWU_TABLE_2, MIWU_GROUP_1, 1, MIWU_EDGE_ANYING},
	/* SLP_S5 */
	{MIWU_TABLE_2, MIWU_GROUP_1, 2, MIWU_EDGE_ANYING},
	/* VW_WIRE_PLTRST */
	{MIWU_TABLE_2, MIWU_GROUP_1, 5, MIWU_EDGE_ANYING},
	/* VW_WIRE_OOB_RST_WARN */
	{MIWU_TABLE_2, MIWU_GROUP_1, 6, MIWU_EDGE_ANYING},
	/* VW_WIRE_HOST_RST_WARN */
	{MIWU_TABLE_2, MIWU_GROUP_2, 0, MIWU_EDGE_ANYING},
	/* VW_WIRE_SUS_WARN */
	{MIWU_TABLE_2, MIWU_GROUP_2, 4, MIWU_EDGE_ANYING},
};

/* VW signals used in eSPI */
static const struct vw_event_t vw_events_list[] = {
	{VW_SLP_S3_L,               0x02,   0x01},	/* index 02h (In)  */
	{VW_SLP_S4_L,               0x02,   0x02},
	{VW_SLP_S5_L,               0x02,   0x04},
	{VW_SUS_STAT_L,             0x03,   0x01},	/* index 03h (In)  */
	{VW_PLTRST_L,               0x03,   0x02},
	{VW_OOB_RST_WARN,           0x03,   0x04},
	{VW_OOB_RST_ACK,            0x04,   0x01},	/* index 04h (Out) */
	{VW_WAKE_L,                 0x04,   0x04},
	{VW_PME_L,                  0x04,   0x08},
	{VW_ERROR_FATAL,            0x05,   0x02},	/* index 05h (Out) */
	{VW_ERROR_NON_FATAL,        0x05,   0x04},
	{VW_SLAVE_BTLD_STATUS_DONE, 0x05,   0x09},
	{VW_SCI_L,                  0x06,   0x01},	/* index 06h (Out) */
	{VW_SMI_L,                  0x06,   0x02},
	{VW_RCIN_L,                 0x06,   0x04},
	{VW_HOST_RST_ACK,           0x06,   0x08},
	{VW_HOST_RST_WARN,          0x07,   0x01},	/* index 07h (In)  */
	{VW_SUS_ACK,                0x40,   0x01},	/* index 40h (Out) */
	{VW_SUS_WARN_L,             0x41,   0x01},	/* index 41h (In)  */
	{VW_SUS_PWRDN_ACK_L,        0x41,   0x02},
	{VW_SLP_A_L,                0x41,   0x08},
	{VW_SLP_LAN,                0x42,   0x01},	/* index 42h (In)  */
	{VW_SLP_WLAN,               0x42,   0x02},
};

/* Flag for SLAVE_BOOT_LOAD siganls */
static uint8_t boot_load_done;

/*****************************************************************************/
/* eSPI internal utilities */

/* Recovery utility for eSPI reset */
static void espi_reset_recovery(void)
{
	/* TODO: Put recovery stuff related to eSPI reset here */

	/* Clear boot load flag */
	boot_load_done = 0;
}

/* Configure Master-to-Slave virtual wire inputs */
static void espi_vw_config_in(const struct vwevms_config_t *config)
{
	uint32_t val;
	uint8_t  i, index;

	switch (VM_TYPE(config->idx)) {
	case ESPI_VW_TYPE_SYS_EV:
	case ESPI_VW_TYPE_PLT:
		for (i = 0; i < ESPI_VWEVMS_NUM; i++) {
			index = VWEVMS_IDX_GET(NPCX_VWEVMS(i));
			/* Set VW input register */
			if (index == config->idx) {
				/* Get Wire field */
				val = NPCX_VWEVMS(i) & 0x0F;
				val |= VWEVMS_FIELD(config->idx,
						config->idx_en,
						config->pltrst_en,
						config->int_en,
						config->espirst_en);
				NPCX_VWEVMS(i) = val;
				return;
			}
		}
		CPRINTS("No match index of all VWEVMSs");
		break;
	default:
		CPRINTS("No support type of VWEVMS");
		break;
	}
}

/* Configure Slave-to-Master virtual wire outputs */
static void espi_vw_config_out(const struct vwevsm_config_t *config)
{
	uint32_t val;
	uint8_t i, index;

	switch (VM_TYPE(config->idx)) {
	case ESPI_VW_TYPE_SYS_EV:
	case ESPI_VW_TYPE_PLT:
		for (i = 0; i < ESPI_VWEVSM_NUM; i++) {
			index = VWEVSM_IDX_GET(NPCX_VWEVSM(i));
			/* Set VW output register */
			if (index == config->idx) {
				/* Preserve WIRE(3-0) and HW_WIRE (27-24). */
				val = NPCX_VWEVSM(i) & 0x0F00000F;
				val |= VWEVSM_FIELD(config->idx,
						config->idx_en,
						config->valid,
						config->pltrst_en,
						config->cdrst_en);
				NPCX_VWEVSM(i) = val;
				return;
			}
		}
		CPRINTS("No match index of all VWEVSMs");
		break;
	default:
		CPRINTS("No support type of VWEVSM");
		break;
	}
}

/* Config Master-to-Slave VWire interrupt edge type and enable it */
static void espi_enable_vw_int(const struct host_wui_item *vwire_int)
{
	uint8_t table = vwire_int->table;
	uint8_t group = vwire_int->group;
	uint8_t num   = vwire_int->num;
	uint8_t edge  = vwire_int->edge;

	/* Set detection mode to edge */
	CLEAR_BIT(NPCX_WKMOD(table, group), num);

	if (edge != MIWU_EDGE_ANYING) {
		/* Disable Any Edge */
		CLEAR_BIT(NPCX_WKAEDG(table, group), num);
		/* Enable Rising Edge */
		if (edge == MIWU_EDGE_RISING)
			CLEAR_BIT(NPCX_WKEDG(table, group), num);
		/* Enable Falling Edge */
		else
			SET_BIT(NPCX_WKEDG(table, group), num);
	} else
		/* enable Any Edge */
		SET_BIT(NPCX_WKAEDG(table, group), num);

	/* Clear the pending bit */
	NPCX_WKPCL(table, group) = BIT(num);

	/* Enable wake-up input sources */
	SET_BIT(NPCX_WKEN(table, group), num);
}

/* Get vw index & value information by signal */
static int espi_vw_get_signal_index(enum espi_vw_signal event)
{
	int index;

	/* Find the vw index by signal name first */
	for (index = 0; index < ARRAY_SIZE(vw_events_list); index++) {
		if (vw_events_list[index].name == event)
			break;
	}
	/* Cannot find the index */
	if (index == ARRAY_SIZE(vw_events_list))
		return -1;

	return index;
}

/* The ISRs of VW signals which used for power sequences */
void espi_vw_power_signal_interrupt(enum espi_vw_signal signal)
{
	if (IS_ENABLED(CONFIG_HOST_ESPI_VW_POWER_SIGNAL))
		/* TODO: Add VW handler in power/common.c */
		power_signal_interrupt((enum gpio_signal) signal);
}

/*****************************************************************************/
/* IC specific low-level driver */

/**
 * Set eSPI Virtual-Wire signal to Host
 *
 * @param signal vw signal needs to set
 * @param level  level of vw signal
 * @return EC_SUCCESS, or non-zero if error.
 */
int espi_vw_set_wire(enum espi_vw_signal signal, uint8_t level)
{
	uint8_t offset, value;
	int sig_idx;

	/* Get index of vw signal list by signale name */
	sig_idx = espi_vw_get_signal_index(signal);

	/* Cannot find index by signal name */
	if (sig_idx < 0)
		return EC_ERROR_PARAM1;

	/* Find the output register offset by vw index */
	for (offset = 0; offset < ESPI_VWEVSM_NUM; offset++) {
		uint8_t vw_idx = VWEVSM_IDX_GET(NPCX_VWEVSM(offset));
		/* If index matches. break */
		if (vw_idx == vw_events_list[sig_idx].evt_idx)
			break;
	}

	/* Cannot match index */
	if (offset == ESPI_VWEVSM_NUM)
		return EC_ERROR_PARAM1;

	value = GET_FIELD(NPCX_VWEVSM(offset), NPCX_VWEVSM_WIRE);
	/* Set wire */
	if (level)
		value |= vw_events_list[sig_idx].evt_val;
	else /* Clear wire */
		value &= (~vw_events_list[sig_idx].evt_val);

	SET_FIELD(NPCX_VWEVSM(offset), NPCX_VWEVSM_WIRE, value);

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
	uint8_t offset, value;
	int sig_idx;

	/* Get index of vw signal list by signale name */
	sig_idx = espi_vw_get_signal_index(signal);

	/* Cannot find index by signal name */
	if (sig_idx < 0)
		return -1;

	/* Find the input register offset by vw index */
	for (offset = 0; offset < ESPI_VWEVMS_NUM; offset++) {
		uint8_t vw_idx = VWEVMS_IDX_GET(NPCX_VWEVMS(offset));
		/* If index matches. break */
		if (vw_idx == vw_events_list[sig_idx].evt_idx)
			break;
	}

	/* Cannot match index */
	if (offset == ESPI_VWEVMS_NUM)
		return -1;

	/* Get wire & check with valid bits */
	value = GET_FIELD(NPCX_VWEVMS(offset), NPCX_VWEVMS_WIRE);
	value &= GET_FIELD(NPCX_VWEVMS(offset), NPCX_VWEVMS_VALID);

	return !!(value & vw_events_list[sig_idx].evt_val);
}

/**
 * Enable VW interrupt of power sequence signal
 *
 * @param signal vw signal needs to enable interrupt
 * @return EC_SUCCESS, or non-zero if error.
 */
int espi_vw_enable_wire_int(enum espi_vw_signal signal)
{
	if (signal == VW_SLP_S3_L)
		SET_BIT(NPCX_WKEN(MIWU_TABLE_2, MIWU_GROUP_1), 0);
	else if (signal == VW_SLP_S4_L)
		SET_BIT(NPCX_WKEN(MIWU_TABLE_2, MIWU_GROUP_1), 1);
	else if (signal == VW_SLP_S5_L)
		SET_BIT(NPCX_WKEN(MIWU_TABLE_2, MIWU_GROUP_1), 2);
	else
		return EC_ERROR_PARAM1;

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
	if (signal == VW_SLP_S3_L)
		CLEAR_BIT(NPCX_WKEN(MIWU_TABLE_2, MIWU_GROUP_1), 0);
	else if (signal == VW_SLP_S4_L)
		CLEAR_BIT(NPCX_WKEN(MIWU_TABLE_2, MIWU_GROUP_1), 1);
	else if (signal == VW_SLP_S5_L)
		CLEAR_BIT(NPCX_WKEN(MIWU_TABLE_2, MIWU_GROUP_1), 2);
	else
		return EC_ERROR_PARAM1;

	return EC_SUCCESS;
}

/*****************************************************************************/
/* VW event handlers */

#ifdef CONFIG_CHIPSET_RESET_HOOK
static void espi_chipset_reset(void)
{
	hook_notify(HOOK_CHIPSET_RESET);
}
DECLARE_DEFERRED(espi_chipset_reset);
#endif

/* PLTRST# event handler */
void espi_vw_evt_pltrst(void)
{
	int pltrst = espi_vw_get_wire(VW_PLTRST_L);

	CPRINTS("VW PLTRST: %d", pltrst);

	if (pltrst) {
		/* PLTRST# deasserted */
		/* Disable eSPI peripheral channel support first */
		CLEAR_BIT(NPCX_ESPICFG, NPCX_ESPICFG_PCCHN_SUPP);

		/* Enable eSPI peripheral channel */
		SET_BIT(NPCX_ESPICFG, NPCX_ESPICFG_PCHANEN);

		/* Initialize host settings */
		host_register_init();

		/* Re-enable eSPI peripheral channel support */
		SET_BIT(NPCX_ESPICFG, NPCX_ESPICFG_PCCHN_SUPP);
	} else {
		/* PLTRST# asserted */
#ifdef CONFIG_CHIPSET_RESET_HOOK
		hook_call_deferred(&espi_chipset_reset_data, MSEC);
#endif
	}
}

/* SLP_Sx event handler */
void espi_vw_evt_slp_s3(void)
{
	CPRINTS("VW SLP_S3: %d", espi_vw_get_wire(VW_SLP_S3_L));
	espi_vw_power_signal_interrupt(VW_SLP_S3_L);
}

void espi_vw_evt_slp_s4(void)
{
	CPRINTS("VW SLP_S4: %d", espi_vw_get_wire(VW_SLP_S4_L));
	espi_vw_power_signal_interrupt(VW_SLP_S4_L);
}

void espi_vw_evt_slp_s5(void)
{
	CPRINTS("VW SLP_S5: %d", espi_vw_get_wire(VW_SLP_S5_L));
	espi_vw_power_signal_interrupt(VW_SLP_S5_L);
}

/* OOB Reset event handler */
void espi_vw_evt_oobrst(void)
{
	CPRINTS("VW OOB_RST: %d", espi_vw_get_wire(VW_OOB_RST_WARN));

	/* Send ACK to host by WARN#'s wire */
	espi_vw_set_wire(VW_OOB_RST_ACK, espi_vw_get_wire(VW_OOB_RST_WARN));
}

/* SUS_WARN# event handler */
void espi_vw_evt_sus_warn(void)
{
	CPRINTS("VW SUS_WARN#: %d", espi_vw_get_wire(VW_SUS_WARN_L));

	udelay(100);

	/* Send ACK to host by WARN#'s wire */
	espi_vw_set_wire(VW_SUS_ACK, espi_vw_get_wire(VW_SUS_WARN_L));
}

/* HOSTRST WARN event handler */
void espi_vw_evt_hostrst_warn(void)
{
	CPRINTS("VW HOST_RST_WARN#: %d", espi_vw_get_wire(VW_HOST_RST_WARN));

	/* Send ACK to host by WARN#'s wire */
	espi_vw_set_wire(VW_HOST_RST_ACK, espi_vw_get_wire(VW_HOST_RST_WARN));
}

/*****************************************************************************/
/* Interrupt handlers */

/* eSPI reset assert/de-assert interrupt */
void espi_espirst_handler(void)
{
	/* Clear pending bit of WUI */
	SET_BIT(NPCX_WKPCL(MIWU_TABLE_0, MIWU_GROUP_5), 5);

	CPRINTS("eSPI RST issued!");
}

/* Handle eSPI virtual wire interrupt 1 */
void __espi_wk2a_interrupt(void)
{
	uint8_t pending_bits = NPCX_WKPND(MIWU_TABLE_2, MIWU_GROUP_1);

	/* Clear pending bits of MIWU */
	NPCX_WKPCL(MIWU_TABLE_2, MIWU_GROUP_1) = pending_bits;

	/* Handle events of virtual-wire */
	if (IS_BIT_SET(pending_bits, 0))
		espi_vw_evt_slp_s3();
	if (IS_BIT_SET(pending_bits, 1))
		espi_vw_evt_slp_s4();
	if (IS_BIT_SET(pending_bits, 2))
		espi_vw_evt_slp_s5();
	if (IS_BIT_SET(pending_bits, 5))
		espi_vw_evt_pltrst();
	if (IS_BIT_SET(pending_bits, 6))
		espi_vw_evt_oobrst();
}
DECLARE_IRQ(NPCX_IRQ_WKINTA_2, __espi_wk2a_interrupt, 3);

/* Handle eSPI virtual wire interrupt 2 */
void __espi_wk2b_interrupt(void)
{
	uint8_t pending_bits = NPCX_WKPND(MIWU_TABLE_2, MIWU_GROUP_2);

	/* Clear pending bits of MIWU */
	NPCX_WKPCL(MIWU_TABLE_2, MIWU_GROUP_2) = pending_bits;

	/* Handle events of virtual-wire */
	if (IS_BIT_SET(pending_bits, 4))
		espi_vw_evt_sus_warn();
	if (IS_BIT_SET(pending_bits, 0))
		espi_vw_evt_hostrst_warn();
}
DECLARE_IRQ(NPCX_IRQ_WKINTB_2, __espi_wk2b_interrupt, 3);

/* Interrupt handler for eSPI status changed */
void espi_interrupt(void)
{
	int chan;
	uint32_t mask, status;

#if defined(CHIP_FAMILY_NPCX7)
	/*
	 * Bit 17 of ESPIIE is reserved. We need to set the same bit in mask
	 * in case bit 17 in ESPISTS of npcx7 is not cleared in ISR.
	 */
	mask = NPCX_ESPIIE | BIT(NPCX_ESPISTS_VWUPDW);
#else
	mask = NPCX_ESPIIE;
#endif
	status = NPCX_ESPISTS & mask;

	while (status) {
		/* Clear pending bits first */
		NPCX_ESPISTS = status;

		if (IS_BIT_SET(status, NPCX_ESPISTS_BERR))
			CPRINTS("eSPI Bus Error");

		/* eSPI inband reset(from VW) */
		if (IS_BIT_SET(status, NPCX_ESPISTS_IBRST)) {
			CPRINTS("eSPI RST inband RST");
			espi_reset_recovery();

		} /* eSPI reset (from eSPI_rst pin) */
		else if (IS_BIT_SET(status, NPCX_ESPISTS_ESPIRST)) {
			CPRINTS("eSPI RST");
			chipset_handle_espi_reset_assert();
			espi_reset_recovery();
		}

		/* eSPI configuration is updated */
		if (IS_BIT_SET(status, NPCX_ESPISTS_CFGUPD)) {
			/*
			 * If host enable/disable channel for VW/OOB/FLASH, EC
			 * should follow except Peripheral channel. It is
			 * handled by PLTRST separately.
			 */
			for (chan = NPCX_ESPI_CH_VW; chan < NPCX_ESPI_CH_COUNT;
					chan++) {
				if (!IS_SLAVE_CHAN_ENABLE(chan) &&
						IS_HOST_CHAN_EN(chan))
					ENABLE_ESPI_CHAN(chan);
				else if (IS_SLAVE_CHAN_ENABLE(chan) &&
						!IS_HOST_CHAN_EN(chan))
					DISABLE_ESPI_CHAN(chan);
			}

			/*
			 * Send SLAVE_BOOTLOAD_DONE and SLAVE_BOOTLOAD_STATUS
			 * events to host simultaneously. To indicate the
			 * completion of EC firmware code loading.
			 */
			if (boot_load_done == 0 &&
					IS_SLAVE_CHAN_ENABLE(NPCX_ESPI_CH_VW)) {

				espi_vw_set_wire(VW_SLAVE_BTLD_STATUS_DONE, 1);
				boot_load_done = 1;
			}
		}

		/* Any VW signal sent by Host - leave it, handle in MIWU ISR */
		if (IS_BIT_SET(status, NPCX_ESPISTS_VWUPD))
			CPRINTS("VW Updated INT");

		/* Get status again */
		status = NPCX_ESPISTS & mask;
	}
}
DECLARE_IRQ(NPCX_IRQ_ESPI, espi_interrupt, 4);

/*****************************************************************************/
/* eSPI Initialization functions */
void espi_init(void)
{
	int i;

	/* Support all channels */
	NPCX_ESPICFG |= ESPI_SUPP_CH_ALL;

	/* Support all I/O modes */
	SET_FIELD(NPCX_ESPICFG, NPCX_ESPICFG_IOMODE_FIELD,
		NPCX_ESPI_IO_MODE_ALL);

	/* Set eSPI speed to max supported */
	SET_FIELD(NPCX_ESPICFG, NPCX_ESPICFG_MAXFREQ_FIELD,
		  NPCX_ESPI_MAXFREQ_MAX);

	/* Configure Master-to-Slave Virtual Wire indexes (Inputs) */
	for (i = 0; i < ARRAY_SIZE(espi_in_list); i++)
		espi_vw_config_in(&espi_in_list[i]);

	/* Configure Slave-to-Master Virtual Wire indexes (Outputs) */
	for (i = 0; i < ARRAY_SIZE(espi_out_list); i++)
		espi_vw_config_out(&espi_out_list[i]);

	/* Configure MIWU for eSPI VW */
	for (i = 0; i < ARRAY_SIZE(espi_vw_int_list); i++)
		espi_enable_vw_int(&espi_vw_int_list[i]);
}

static int command_espi(int argc, char **argv)
{
	uint32_t chan;
	char *e;

	if (argc == 1) {
		return EC_ERROR_INVAL;
	/* Get value of eSPI registers */
	} else if (argc == 2) {
		int i;

		if (strcasecmp(argv[1], "cfg") == 0) {
			ccprintf("ESPICFG [0x%08x]\n", NPCX_ESPICFG);
		} else if (strcasecmp(argv[1], "vsm") == 0) {
			for (i = 0; i < ESPI_VWEVSM_NUM; i++) {
				uint32_t val = NPCX_VWEVSM(i);
				uint8_t  idx = VWEVSM_IDX_GET(val);

				ccprintf("VWEVSM%d: %02x [0x%08x]\n", i, idx,
						val);
			}
		} else if (strcasecmp(argv[1], "vms") == 0) {
			for (i = 0; i < ESPI_VWEVMS_NUM; i++) {
				uint32_t val = NPCX_VWEVMS(i);
				uint8_t  idx = VWEVMS_IDX_GET(val);

				ccprintf("VWEVMS%d: %02x [0x%08x]\n", i, idx,
						val);
			}
		}
	/* Enable/Disable the channels of eSPI */
	} else if (argc == 3) {
		uint32_t m = (uint32_t) strtoi(argv[2], &e, 0);

		if (*e)
			return EC_ERROR_PARAM2;
		if (m > 4)
			return EC_ERROR_PARAM2;
		else if (m == 4)
			chan = 0x0F;
		else
			chan = 0x01 << m;
		if (strcasecmp(argv[1], "en") == 0)
			NPCX_ESPICFG = NPCX_ESPICFG | chan;
		else if (strcasecmp(argv[1], "dis") == 0)
			NPCX_ESPICFG = NPCX_ESPICFG & ~chan;
		else
			return EC_ERROR_PARAM1;
		ccprintf("ESPICFG [0x%08x]\n", NPCX_ESPICFG);
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(espi, command_espi,
			"cfg/vms/vsm/en/dis [channel]",
			"eSPI configurations");
