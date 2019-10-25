/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ESPI module for Chrome EC */

#include "common.h"
#include "acpi.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_protocol.h"
#include "port80.h"
#include "util.h"
#include "chipset.h"

#include "registers.h"
#include "espi.h"
#include "lpc.h"
#include "lpc_chip.h"
#include "system.h"
#include "task.h"
#include "console.h"
#include "uart.h"
#include "util.h"
#include "power.h"
#include "timer.h"
#include "tfdp_chip.h"

/* Console output macros */
#ifdef CONFIG_MCHP_ESPI_DEBUG
#ifdef CONFIG_MCHP_TFDP
#define CPUTS(...)
#define CPRINTS(...)
#else
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)
#endif
#else
#define CPUTS(...)
#define CPRINTS(...)
#endif

/*
 * eSPI slave to master virtual wire pulse timeout.
 */
#define ESPI_S2M_VW_PULSE_LOOP_CNT		50
#define ESPI_S2M_VW_PULSE_LOOP_DLY_US		10

/*
 * eSPI master enable virtual wire channel timeout.
 */
#define ESPI_CHAN_READY_TIMEOUT_US (100 * MSEC)
#define ESPI_CHAN_READY_POLL_INTERVAL_US 100

static uint32_t espi_channels_ready;

/*
 * eSPI Virtual Wire reset values
 * VWire name used by chip independent code.
 * Host eSPI Master VWire index containing signal
 * Reset value of VWire. Note, each Host VWire index may
 * have a different reset source:
 *	EC Power-on/chip reset
 *	ESPI_RESET# assertion by Host eSPI master
 *	eSPI Platform Reset assertion by Host eSPI master
 *		MEC1701H allows eSPI Platform reset to
 *		be a VWire or side band signal.
 *
 * NOTE MEC1701H Boot-ROM will restore VWires ... from
 * VBAT power register MCHP_VBAT_VWIRE_BACKUP.
 *	bits[3:0] = Master-to-Slave Index 02h SRC3:SRC0 values
 *		MSVW00 register
 *			SRC0 = SLP_S3#
 *			SRC1 = SLP_S4#
 *			SRC2 = SLP_S5#
 *			SRC3 = reserved
 *	bits[7:4] = Master-to-Slave Index 42h SRC3:SRC0 values
 *		MSVW04 register
 *			SRC0 = SLP_LAN#
 *			SRC1 = SLP_WLAN#
 *			SRC2 = reserved
 *			SRC3 = reserved
 *
 */
struct vw_info_t {
	uint16_t name;		/* signal name */
	uint8_t  host_idx;	/* Host VWire index of signal */
	uint8_t  reset_val;	/* reset value of VWire */
	uint8_t  flags;		/* b[0]=0(MSVW), =1(SMVW) */
	uint8_t  reg_idx;	/* MSVW or SMVW index */
	uint8_t  src_num;	/* SRC number */
	uint8_t  rsvd;
};


/* VW signals used in eSPI */
/*
 * MEC1701H VWire mapping based on eSPI Spec 1.0,
 * eSPI Compatibility spec 0.96,
 * MCHP HW defaults and ec/include/espi.h
 *
 * MSVW00 index=02h PORValue=00000000_04040404_00000102 reset=RESET_SYS
 *	SRC0 = VW_SLP_S3_L, IntrDis
 *	SRC1 = VW_SLP_S4_L, IntrDis
 *	SRC2 = VW_SLP_S5_L, IntrDis
 *	SRC3 = reserved, IntrDis
 * MSVW01 index=03h PORValue=00000000_04040404_00000003 reset=RESET_ESPI
 *	SRC0 = VW_SUS_STAT_L, IntrDis
 *	SRC1 = VW_PLTRST_L, IntrDis
 *	SRC2 = VW_OOB_RST_WARN, IntrDis
 *	SRC3 = reserved, IntrDis
 * MSVW02 index=07h PORValue=00000000_04040404_00000307 reset=PLTRST
 *	SRC0 = VW_HOST_RST_WARN
 *	SRC1 = 0 reserved
 *	SRC2 = 0 reserved
 *	SRC3 = 0 reserved
 * MSVW03 index=41h PORValue=00000000_04040404_00000041 reset=RESET_ESPI
 *	SRC0 = VW_SUS_WARN_L, IntrDis
 *	SRC1 = VW_SUS_PWRDN_ACK_L, IntrDis
 *	SRC2 = 0 reserved, IntrDis
 *	SRC3 = VW_SLP_A_L, IntrDis
 * MSVW04 index=42h PORValue=00000000_04040404_00000141 reset=RESET_SYS
 *	SRC0 = VW_SLP_LAN, IntrDis
 *	SRC1 = VW_SLP_WLAN, IntrDis
 *	SRC2 = reserved, IntrDis
 *	SRC3 = reserved, IntrDis
 *
 * SMVW00 index=04h PORValue=01010000_0000C004 STOM=1100 reset=RESET_ESPI
 *	SRC0 = VW_OOB_RST_ACK
 *	SRC1 = 0 reserved
 *	SRC2 = VW_WAKE_L
 *	SRC3 = VW_PME_L
 * SMVW01 index=05h PORValue=00000000_00000005 STOM=0000 reset=RESET_ESPI
 *	SRC0 = SLAVE_BOOT_LOAD_DONE   !!! NOTE: Google combines SRC0 & SRC3
 *	SRC1 = VW_ERROR_FATAL
 *	SRC2 = VW_ERROR_NON_FATAL
 *	SRC3 = SLAVE_BOOT_LOAD_STATUS !!! into VW_SLAVE_BTLD_STATUS_DONE
 * SMVW02 index=06h PORValue=00010101_00007306 STOM=0111 reset=PLTRST
 *	SRC0 = VW_SCI_L
 *	SRC1 = VW_SMI_L
 *	SRC2 = VW_RCIN_L
 *	SRC3 = VW_HOST_RST_ACK
 * SMVW03 index=40h PORValue=00000000_00000040 STOM=0000 reset=RESET_ESPI
 *	SRC0 = assign VW_SUS_ACK
 *	SRC1 = 0
 *	SRC2 = 0
 *	SRC3 = 0
 *
 * table of vwire structures
 * MSVW00 at 0x400F9C00 offset = 0x000
 * MSVW01 at 0x400F9C0C offset = 0x00C
 *
 * SMVW00 at 0x400F9E00 offset = 0x200
 * SMVW01 at 0x400F9E08 offset = 0x208
 *
 */

/*
 * Virtual Wire table
 * Each entry contains:
 *	Signal name from include/espi.h
 *	Host chipset VWire index number
 *      Reset value of VWire
 *      flags where bit[0]==0 Wire is Master-to-Slave or 1 Slave-to-Master
 *	MEC1701 register index into MSVW or SMVW register banks
 *	MEC1701 source number in MSVW or SMVW bank
 *      Reserved
 *	Pointer to name string for debug
 */
static const struct vw_info_t vw_info_tbl[] = {
	/* name				host  reset       reg   SRC
	 *				index value flags index num   rsvd
	 */
	/* MSVW00 Host index 02h (In) */
	{VW_SLP_S3_L,			0x02, 0x00, 0x00, 0x00, 0x00, 0x00},
	{VW_SLP_S4_L,			0x02, 0x00, 0x00, 0x00, 0x01, 0x00},
	{VW_SLP_S5_L,			0x02, 0x00, 0x10, 0x00, 0x02, 0x00},
	/* MSVW01 Host index 03h (In) */
	{VW_SUS_STAT_L,			0x03, 0x00, 0x10, 0x01, 0x00, 0x00},
	{VW_PLTRST_L,			0x03, 0x00, 0x10, 0x01, 0x01, 0x00},
	{VW_OOB_RST_WARN,		0x03, 0x00, 0x10, 0x01, 0x02, 0x00},
	/* SMVW00 Host Index 04h (Out) */
	{VW_OOB_RST_ACK,		0x04, 0x00, 0x01, 0x00, 0x00, 0x00},
	{VW_WAKE_L,			0x04, 0x01, 0x01, 0x00, 0x02, 0x00},
	{VW_PME_L,			0x04, 0x01, 0x01, 0x00, 0x03, 0x00},
	/* SMVW01 Host index 05h (Out) */
	{VW_ERROR_FATAL,		0x05, 0x00, 0x01, 0x01, 0x01, 0x00},
	{VW_ERROR_NON_FATAL,		0x05, 0x00, 0x01, 0x01, 0x02, 0x00},
	{VW_SLAVE_BTLD_STATUS_DONE,	0x05, 0x00, 0x01, 0x01, 0x30, 0x00},
	/* SMVW02 Host index 06h (Out) */
	{VW_SCI_L,			0x06, 0x01, 0x01, 0x02, 0x00, 0x00},
	{VW_SMI_L,			0x06, 0x01, 0x01, 0x02, 0x01, 0x00},
	{VW_RCIN_L,			0x06, 0x01, 0x01, 0x02, 0x02, 0x00},
	{VW_HOST_RST_ACK,		0x06, 0x00, 0x01, 0x02, 0x03, 0x00},
	/* MSVW02 Host index 07h (In) */
	{VW_HOST_RST_WARN,		0x07, 0x00, 0x10, 0x02, 0x00, 0x00},
	/* SMVW03 Host Index 40h (Out) */
	{VW_SUS_ACK,			0x40, 0x00, 0x01, 0x03, 0x00, 0x00},
	/* MSVW03 Host Index 41h (In) */
	{VW_SUS_WARN_L,			0x41, 0x00, 0x10, 0x03, 0x00, 0x00},
	{VW_SUS_PWRDN_ACK_L,		0x41, 0x00, 0x10, 0x03, 0x01, 0x00},
	{VW_SLP_A_L,			0x41, 0x00, 0x10, 0x03, 0x03, 0x00},
	/* MSVW04 Host index 42h (In) */
	{VW_SLP_LAN,			0x42, 0x00, 0x10, 0x04, 0x00, 0x00},
	{VW_SLP_WLAN,			0x42, 0x00, 0x10, 0x04, 0x01, 0x00}
};
BUILD_ASSERT(ARRAY_SIZE(vw_info_tbl) == VW_SIGNAL_COUNT);


/************************************************************************/
/* eSPI internal utilities */

static int espi_vw_get_signal_index(enum espi_vw_signal event)
{
	int i;

	/* Search table by signal name */
	for (i = 0; i < ARRAY_SIZE(vw_info_tbl); i++) {
		if (vw_info_tbl[i].name == event)
			return i;
	}

	return -1;
}


/*
 * Initialize eSPI hardware upon ESPI_RESET# de-assertion
 */
#ifdef CONFIG_MCHP_ESPI_RESET_DEASSERT_INIT
static void espi_reset_deassert_init(void)
{

}
#endif

/* Call this on entry to deepest sleep state with EC turned off.
 * May not be required in future host eSPI chipsets.
 *
 * Save Master-to-Slave VWire Index 02h & 42h before
 * entering a deep sleep state where EC power is shut off.
 * PCH requires we restore these VWires on wake.
 * SLP_S3#, SLP_S4#, SLP_S5# in index 02h
 * SLP_LAN#, SLP_WLAN# in index 42h
 * Current VWire states are saved to a battery backed 8-bit
 * register in MEC1701H.
 * If a VBAT POR occurs the value of this register = 0 which
 * is the default state of the above VWires on a hardware
 * POR.
 * VBAT byte bit definitions
 * Host Index 02h -> MSVW00
 * Host Index 42h -> MSVW04
 * 0 Host Index 02h SRC0
 * 1 Host Index 02h SRC1
 * 2 Host Index 02h SRC2
 * 3 Host Index 02h SRC3
 * 4 Host Index 42h SRC0
 * 5 Host Index 42h SRC1
 * 6 Host Index 42h SRC2
 * 7 Host Index 42h SRC3
 */
#ifdef CONFIG_MCHP_ESPI_VW_SAVE_ON_SLEEP
static void espi_vw_save(void)
{
	uint32_t i, r;
	uint8_t vb;

	vb = 0;
	r = MCHP_ESPI_VW_M2S_SRC_ALL(MSVW_H42);
	for (i = 0; i < 4; i++) {
		if (r & (1ul << (i << 3)))
			vb |= (1u << i);
	}

	vb <<= 4;
	r = MCHP_ESPI_VW_M2S_SRC_ALL(MSVW_H02);
	for (i = 0; i < 4; i++) {
		if (r & (1ul << (i << 3)))
			vb |= (1u << i);
	}

	r = MCHP_VBAT_RAM(MCHP_VBAT_VWIRE_BACKUP);
	r = (r & 0xFFFFFF00) | vb;
	MCHP_VBAT_RAM(MCHP_VBAT_VWIRE_BACKUP) = r;
}

/*
 * Update MEC1701H VBAT powered VWire backup values restored on
 * MCHP chip reset. MCHP Boot-ROM loads these values into
 * MSVW00 SRC[0:3](Index 02h) and MSVW04 SRC[0:3](Index 42h)
 * on chip reset(POR, WDT reset, chip reset, wake from EC off).
 * Always clear backup value after restore.
 */
static void espi_vw_restore(void)
{
	uint32_t i, r;
	uint8_t vb;

#ifdef EVB_NO_ESPI_TEST_MODE
	vb = 0xff; /* force SLP_Sx# signals to 1 */
#else
	vb = MCHP_VBAT_RAM(MCHP_VBAT_VWIRE_BACKUP) & 0xff;
#endif
	r = 0;
	for (i = 0; i < 4; i++) {
		if (vb & (1u << i))
			r |= (1ul << (i << 3));
	}
	MCHP_ESPI_VW_M2S_SRC_ALL(MSVW_H02) = r;
	CPRINTS("eSPI restore MSVW00(Index 02h) = 0x%08x", r);
	trace11(0, ESPI, 0, "eSPI restore MSVW00(Index 02h) = 0x%08x", r);

	vb >>= 4;
	r = 0;
	for (i = 0; i < 4; i++) {
		if (vb & (1u << i))
			r |= (1ul << (i << 3));
	}
	MCHP_ESPI_VW_M2S_SRC_ALL(MSVW_H42) = r;
	CPRINTS("eSPI restore MSVW00(Index 42h) = 0x%08x", r);
	trace11(0, ESPI, 0, "eSPI restore MSVW04(Index 42h) = 0x%08x", r);

	r = MCHP_VBAT_RAM(MCHP_VBAT_VWIRE_BACKUP);
	MCHP_VBAT_RAM(MCHP_VBAT_VWIRE_BACKUP) = r & 0xFFFFFF00;

}
#endif

static uint8_t __attribute__((unused)) espi_msvw_srcs_get(uint8_t msvw_id)
{
	uint8_t msvw;

	msvw = 0;
	if (msvw_id < MSVW_MAX) {
		uint32_t r = MCHP_ESPI_VW_M2S_SRC_ALL(msvw_id);

		msvw = (r & 0x01);
		msvw |= ((r >> 7) & 0x02);
		msvw |= ((r >> 14) & 0x04);
		msvw |= ((r >> 21) & 0x08);
	}

	return msvw;
}

static void __attribute__((unused)) espi_msvw_srcs_set(uint8_t msvw_id,
						       uint8_t src_bitmap)
{
	if (msvw_id < MSVW_MAX) {
		uint32_t r = (src_bitmap & 0x08) << 21;

		r |= (src_bitmap & 0x04) << 14;
		r |= (src_bitmap & 0x02) << 7;
		r |= (src_bitmap & 0x01);
		MCHP_ESPI_VW_M2S_SRC_ALL(msvw_id) = r;
	}
}

static uint8_t __attribute__((unused)) espi_smvw_srcs_get(uint8_t smvw_id)
{
	uint8_t smvw;

	smvw = 0;
	if (smvw_id < SMVW_MAX) {
		uint32_t r = MCHP_ESPI_VW_S2M_SRC_ALL(smvw_id);

		smvw = (r & 0x01);
		smvw |= ((r >> 7) & 0x02);
		smvw |= ((r >> 14) & 0x04);
		smvw |= ((r >> 21) & 0x08);
	}

	return smvw;
}

static void __attribute__((unused)) espi_smvw_srcs_set(uint8_t smvw_id,
						       uint8_t src_bitmap)
{
	if (smvw_id < SMVW_MAX) {
		uint32_t r = (src_bitmap & 0x08) << 21;

		r |= (src_bitmap & 0x04) << 14;
		r |= (src_bitmap & 0x02) << 7;
		r |= (src_bitmap & 0x01);
		MCHP_ESPI_VW_S2M_SRC_ALL(smvw_id) = r;
	}
}


/*
 * Called before releasing RSMRST#
 *	ESPI_RESET# is asserted
 *	PLATFORM_RESET# is asserted
 */
static void espi_bar_pre_init(void)
{
	/* Configuration IO BAR set to 0x2E/0x2F */
	MCHP_ESPI_IO_BAR_ADDR_LSB(MCHP_ESPI_IO_BAR_ID_CFG_PORT) = 0x2E;
	MCHP_ESPI_IO_BAR_ADDR_MSB(MCHP_ESPI_IO_BAR_ID_CFG_PORT) = 0x00;
	MCHP_ESPI_IO_BAR_VALID(MCHP_ESPI_IO_BAR_ID_CFG_PORT) = 1;
}

/*
 * Called before releasing RSMRST#
 *	ESPI_RESET# is asserted
 *	PLATFORM_RESET# is asserted
 * Set all MSVW to either edge interrupt
 *	IRQ_SELECT fields are reset on RESET_SYS not ESPI_RESET or PLTRST
 *
 */
static void espi_vw_pre_init(void)
{
	uint32_t i;

	CPRINTS("eSPI VW Pre-Init");
	trace0(0, ESPI, 0, "eSPI VW Pre-Init");

#ifdef CONFIG_MCHP_ESPI_VW_SAVE_ON_SLEEP
	espi_vw_restore();
#endif

	/* disable all */
	for (i = 0; i < MSVW_MAX; i++)
		MCHP_ESPI_VW_M2S_IRQSEL_ALL(i) = 0x0f0f0f0ful;

	/* clear spurious status */
	MCHP_INT_SOURCE(24) = 0xfffffffful;
	MCHP_INT_SOURCE(25) = 0xfffffffful;

	MCHP_ESPI_VW_M2S_IRQSEL_ALL(MSVW_H02) = 0x040f0f0ful;
	MCHP_ESPI_VW_M2S_IRQSEL_ALL(MSVW_H03) = 0x040f0f0ful;
	MCHP_ESPI_VW_M2S_IRQSEL_ALL(MSVW_H07) = 0x0404040ful;
	MCHP_ESPI_VW_M2S_IRQSEL_ALL(MSVW_H41) = 0x0f040f0ful;
	MCHP_ESPI_VW_M2S_IRQSEL_ALL(MSVW_H42) = 0x04040f0ful;
	MCHP_ESPI_VW_M2S_IRQSEL_ALL(MSVW_H47) = 0x0404040ful;

	MCHP_INT_ENABLE(24) = 0xfff3b177ul;
	MCHP_INT_ENABLE(25) = 0x01ul;

	MCHP_INT_SOURCE(24) = 0xfffffffful;
	MCHP_INT_SOURCE(25) = 0xfffffffful;

	MCHP_INT_BLK_EN = (1ul << 24) + (1ul << 25);

	task_enable_irq(MCHP_IRQ_GIRQ24);
	task_enable_irq(MCHP_IRQ_GIRQ25);

	CPRINTS("eSPI VW Pre-Init Done");
	trace0(0, ESPI, 0, "eSPI VW Pre-Init Done");
}


/*
 * If VWire, Flash, and OOB channels have been enabled
 * then set VWires SLAVE_BOOT_LOAD_STATUS = SLAVE_BOOT_LOAD_DONE = 1
 * SLAVE_BOOT_LOAD_STATUS = SRC3 of Slave-to-Master Index 05h
 * SLAVE_BOOT_LOAD_DONE = SRC0 of Slave-to-Master Index 05h
 * Note, if set individually then set status first then done.
 * We set both simultaneously. ESPI_ALERT# will assert only if one
 * or both bits change.
 * SRC0 is bit[32] of SMVW01
 * SRC3 is bit[56] of SMVW01
 */
static void espi_send_boot_load_done(void)
{
	/* First set SLAVE_BOOT_LOAD_STATUS = 1 */
	MCHP_ESPI_VW_S2M_SRC3(SMVW_H05) = 1;
	/* Next set SLAVE_BOOT_LOAD_DONE = 1 */
	MCHP_ESPI_VW_S2M_SRC0(SMVW_H05) = 1;

	CPRINTS("eSPI Send SLAVE_BOOT_LOAD_STATUS/DONE = 1");
	trace0(0, ESPI, 0, "VW SLAVE_BOOT_LOAD_STATUS/DONE = 1");
}


/*
 * Called when eSPI PLTRST# VWire de-asserts
 * Re-initialize any hardware that was reset while PLTRST# was
 * asserted.
 * Logical Device BAR's, etc.
 *   Each BAR requires address, mask, and valid bit
 *     mask = bit map of address[7:0] to mask out
 *	0 = no masking, match exact address
 *	0x01 = mask bit[0], match two consecutive addresses
 *	0xff = mask bits[7:0], match 256 consecutive bytes
 *     eSPI has two registers for each BAR
 *       Host visible register
 *		base address in bits[31:16]
 *		valid = bit[0]
 *       EC only register
 *		mask = bits[7:0]
 *		Logical device number = bits[13:8]
 *		Virtualized = bit[16] Not Implemented
 */
static void espi_host_init(void)
{
	CPRINTS("eSPI - espi_host_init");
	trace0(0, ESPI, 0, "eSPI Host Init");

	/* BAR's */

	/* Configuration IO BAR set to 0x2E/0x2F */
	MCHP_ESPI_IO_BAR_CTL_MASK(MCHP_ESPI_IO_BAR_ID_CFG_PORT) = 0x01;
	MCHP_ESPI_IO_BAR_ADDR_LSB(MCHP_ESPI_IO_BAR_ID_CFG_PORT) = 0x2E;
	MCHP_ESPI_IO_BAR_ADDR_MSB(MCHP_ESPI_IO_BAR_ID_CFG_PORT) = 0x00;
	MCHP_ESPI_IO_BAR_VALID(MCHP_ESPI_IO_BAR_ID_CFG_PORT) = 1;

	/* Set up ACPI0 for 0x62/0x66 */
	chip_acpi_ec_config(0, 0x62, 0x04);

	/* Set up ACPI1 for 0x200-0x203, 0x204-0x207 */
	chip_acpi_ec_config(1, 0x200, 0x07);

	/* Set up 8042 interface at 0x60/0x64 */
	chip_8042_config(0x60);

	/* EMI at 0x800 for accessing shared memory */
	chip_emi0_config(0x800);

	/* Setup Port80 Debug Hardware for I/O 80h */
	chip_port80_config(0x80);

	lpc_mem_mapped_init();

	MCHP_ESPI_PC_STATUS = 0xfffffffful;
	/* PC enable & Mastering enable changes */
	MCHP_ESPI_PC_IEN = (1ul << 25) + (1ul << 28);


	/* Sufficiently initialized */
	lpc_set_init_done(1);

	/* last set eSPI Peripheral Channel Ready = 1 */
	/* Done in ISR for PC Channel */
	MCHP_ESPI_IO_PC_READY = 1;

	/* Update host events now that we can copy them to memmap */
	/* NOTE: This routine may pulse SCI# and/or SMI#
	 * For eSPI these are virtual wires. VWire channel should be
	 * enabled before PLTRST# is de-asserted so its safe BUT has
	 * PC Channel(I/O) Enable occurred?
	 */
	lpc_update_host_event_status();

	CPRINTS("eSPI - espi_host_init Done");
	trace0(0, ESPI, 0, "eSPI Host Init Done");
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, espi_host_init, HOOK_PRIO_FIRST);


/*
 * Called in response to VWire OOB_RST_WARN==1 from
 * espi_vw_evt_oob_rst_warn.
 * Host chipset eSPI documentation states eSPI slave should
 * if necessary flush any OOB upstream (OOB TX) data before the slave
 * sends OOB_RST_ACK=1 to the Host.
 */
static void espi_oob_flush(void)
{
}


/*
 * Called in response to VWire HOST_RST_WARN==1 from
 * espi_vw_evt_host_rst_warn.
 * Host chipset eSPI documentation states assertion of HOST_RST_WARN
 * can be used if necessary to flush any Peripheral Channel data
 * before slave sends HOST_RST_ACK to Host.
 */
static void espi_pc_flush(void)
{
}

/* The ISRs of VW signals which used for power sequences */
void espi_vw_power_signal_interrupt(enum espi_vw_signal signal)
{
	CPRINTS("eSPI power signal interrupt for VW %d", signal);
	trace1(0, ESPI, 0, "eSPI pwr intr VW %d", (signal - VW_SIGNAL_START));
	power_signal_interrupt((enum gpio_signal) signal);
}

/************************************************************************/
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
	int tidx;
	uint8_t ridx, src_num;

	tidx = espi_vw_get_signal_index(signal);

	if (tidx < 0)
		return EC_ERROR_PARAM1;

	if (0 == (vw_info_tbl[tidx].flags & (1u << 0)))
		return EC_ERROR_PARAM1; /* signal is Master-to-Slave */

	ridx = vw_info_tbl[tidx].reg_idx;
	src_num = vw_info_tbl[tidx].src_num;

	if (level)
		level = 1;

	if (signal == VW_SLAVE_BTLD_STATUS_DONE) {
		/* SLAVE_BOOT_LOAD_STATUS */
		MCHP_ESPI_VW_S2M_SRC3(ridx) = level;
		/* SLAVE_BOOT_LOAD_DONE after status */
		MCHP_ESPI_VW_S2M_SRC0(ridx) = level;
	} else {
		MCHP_ESPI_VW_S2M_SRC(ridx, src_num) = level;
	}

#ifdef CONFIG_MCHP_ESPI_DEBUG
	CPRINTS("eSPI VW Set Wire %s = %d",
		espi_vw_get_wire_name(signal), level);
	trace2(0, ESPI, 0, "VW SetWire[%d] = %d",
	       ((uint32_t)signal - VW_SIGNAL_START), level);
#endif

	return EC_SUCCESS;
}

/*
 * Set Slave to Master virtual wire to level and wait for hardware
 * to process virtual wire.
 * If virtual wire written to same value then hardware change bit
 * is 0 and routine returns success.
 * If virtual wire written to different value then hardware change bit
 * goes to 1 until bit is transmitted upstream to the master. This may
 * happen quickly is bus is idle. Poll for hardware clearing change bit
 * until timeout.
 */
static int espi_vw_s2m_set_w4m(uint32_t ridx, uint32_t src_num,
		uint8_t level)
{
	uint32_t i;

	MCHP_ESPI_VW_S2M_SRC(ridx, src_num) = level & 0x01;

	for (i = 0; i < ESPI_S2M_VW_PULSE_LOOP_CNT; i++) {
		if ((MCHP_ESPI_VW_S2M_CHANGE(ridx) &
		     (1u << src_num)) == 0)
			return EC_SUCCESS;
		udelay(ESPI_S2M_VW_PULSE_LOOP_DLY_US);
	}

	return EC_ERROR_TIMEOUT;
}

/*
 * Create a pulse on a Slave-to-Master VWire
 * Use case is generate low pulse on SCI# virtual wire.
 * Should a timeout mechanism be added because we are
 * waiting on Host eSPI Master to respond to eSPI Alert and
 * then read the VWires. If the eSPI Master is OK the maximum
 * time will still be variable depending upon link frequency and
 * other activity on the link. Other activity is currently bounded by
 * Host chipset eSPI maximum payload length of 64 bytes + packet overhead.
 * Lowest eSPI transfer rate is 1x at 20 MHz, assume 30% packet overhead.
 * (64 * 1.3) * 8 = 666 bits is roughly 34 us. Pad to 100 us.
 */
int espi_vw_pulse_wire(enum espi_vw_signal signal, int pulse_level)
{
	int rc, tidx;
	uint8_t ridx, src_num, level;

	tidx = espi_vw_get_signal_index(signal);

	if (tidx < 0)
		return EC_ERROR_PARAM1;

	if (0 == (vw_info_tbl[tidx].flags & (1u << 0)))
		return EC_ERROR_PARAM1; /* signal is Master-to-Slave */

	ridx = vw_info_tbl[tidx].reg_idx;
	src_num = vw_info_tbl[tidx].src_num;

	level = 0;
	if (pulse_level)
		level = 1;

#ifdef CONFIG_MCHP_ESPI_DEBUG
	CPRINTS("eSPI VW Pulse Wire %s to %d",
		espi_vw_get_wire_name(signal), level);
	trace2(0, ESPI, 0, "eSPI pulse VW[%d] = %d", signal, level);
	trace2(0, ESPI, 0, " S2M index=%d src=%d", ridx, src_num);
#endif

	/* set requested inactive state */
	rc = espi_vw_s2m_set_w4m(ridx, src_num, ~level);
	if (rc != EC_SUCCESS)
		return rc;

	/* drive to requested active state */
	rc = espi_vw_s2m_set_w4m(ridx, src_num, level);
	if (rc != EC_SUCCESS)
		return rc;

	/* set to requested inactive state */
	rc = espi_vw_s2m_set_w4m(ridx, src_num, ~level);

	return rc;
}

/**
 * Get eSPI Virtual-Wire signal from host
 *
 * @param signal vw signal needs to get
 * @return      1: set by host, otherwise: no signal
 */
int espi_vw_get_wire(enum espi_vw_signal signal)
{
	int vw, tidx;
	uint8_t ridx, src_num;

	vw = 0;
	tidx = espi_vw_get_signal_index(signal);

	if (tidx >= 0 && (0 == (vw_info_tbl[tidx].flags & (1u << 0)))) {
		ridx = vw_info_tbl[tidx].reg_idx;
		src_num = vw_info_tbl[tidx].src_num;
		vw = MCHP_ESPI_VW_M2S_SRC(ridx, src_num) & 0x01;
#ifdef CONFIG_MCHP_ESPI_DEBUG
		CPRINTS("VW GetWire %s = %d",
			espi_vw_get_wire_name(signal), vw);
		trace2(0, ESPI, 0, "VW GetWire[%d] = %d",
		       ((uint32_t)signal - VW_SIGNAL_START), vw);
#endif
	}

	return vw;
}

/**
 * Enable VW interrupt of power sequence signal
 *
 * @param signal vw signal needs to enable interrupt
 * @return EC_SUCCESS, or non-zero if error.
 */
int espi_vw_enable_wire_int(enum espi_vw_signal signal)
{
	int tidx;
	uint8_t ridx, src_num, girq_num, bpos;

	tidx = espi_vw_get_signal_index(signal);

	if (tidx < 0)
		return EC_ERROR_PARAM1;

	if (0 != (vw_info_tbl[tidx].flags & (1u << 0)))
		return EC_ERROR_PARAM1; /* signal is Slave-to-Master */

#ifdef CONFIG_MCHP_ESPI_DEBUG
	CPRINTS("VW IntrEn for VW[%s]",
		espi_vw_get_wire_name(signal));
	trace1(0, ESPI, 0, "VW IntrEn for VW[%d]",
	       ((uint32_t)signal - VW_SIGNAL_START));
#endif

	ridx = vw_info_tbl[tidx].reg_idx;
	src_num = vw_info_tbl[tidx].src_num;

	/*
	 * Set SRCn_IRQ_SELECT field for VWire to either edge
	 * Write enable set bit in GIRQ24 or GIRQ25
	 * GIRQ24 MSVW00[0:3] through MSVW06[0:3] (bits[0:27])
	 * GIRQ25 MSVW07[0:3] through MSVW10[0:3] (bits[0:25])
	 */
	MCHP_ESPI_VW_M2S_IRQSEL(ridx, src_num) =
			MCHP_ESPI_MSVW_IRQSEL_BOTH_EDGES;

	girq_num = 24;
	if (ridx > 6) {
		girq_num++;
		ridx -= 7;
	}
	bpos = (ridx << 2) + src_num;

	MCHP_INT_SOURCE(girq_num) = (1ul << bpos);
	MCHP_INT_ENABLE(girq_num) = (1ul << bpos);

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
	int tidx;
	uint8_t ridx, src_num, bpos;

	tidx = espi_vw_get_signal_index(signal);

	if (tidx < 0)
		return EC_ERROR_PARAM1;

	if (0 != (vw_info_tbl[tidx].flags & (1u << 0)))
		return EC_ERROR_PARAM1; /* signal is Slave-to-Master */

#ifdef CONFIG_MCHP_ESPI_DEBUG
	CPRINTS("VW IntrDis for VW[%s]",
		espi_vw_get_wire_name(signal));
	trace1(0, ESPI, 0, "VW IntrDis for VW[%d]",
	       (signal - VW_SIGNAL_START));
#endif

	ridx = vw_info_tbl[tidx].reg_idx;
	src_num = vw_info_tbl[tidx].src_num;

	/*
	 * Set SRCn_IRQ_SELECT field for VWire to disabled
	 * Write enable set bit in GIRQ24 or GIRQ25
	 * GIRQ24 MSVW00[0:3] through MSVW06[0:3] (bits[0:27])
	 * GIRQ25 MSVW07[0:3] through MSVW10[0:3] (bits[0:25])
	 */
	MCHP_ESPI_VW_M2S_IRQSEL(ridx, src_num) =
			MCHP_ESPI_MSVW_IRQSEL_DISABLED;

	if (ridx < 7) {
		bpos = (ridx << 2) + src_num;
		MCHP_INT_DISABLE(24) = (1ul << bpos);

	} else {
		bpos = ((ridx - 7) << 2) + src_num;
		MCHP_INT_DISABLE(25) = (1ul << bpos);
	}

	return EC_SUCCESS;
}

/************************************************************************/
/* VW event handlers */

#ifdef CONFIG_CHIPSET_RESET_HOOK
static void espi_chipset_reset(void)
{
	hook_notify(HOOK_CHIPSET_RESET);
}
DECLARE_DEFERRED(espi_chipset_reset);
#endif


/* SLP_Sx event handler */
void espi_vw_evt_slp_s3_n(uint32_t wire_state, uint32_t bpos)
{
	CPRINTS("VW SLP_S3: %d", wire_state);
	trace1(0, ESPI, 0, "VW_SLP_S3_L change to %d", wire_state);
	espi_vw_power_signal_interrupt(VW_SLP_S3_L);
}

void espi_vw_evt_slp_s4_n(uint32_t wire_state, uint32_t bpos)
{
	CPRINTS("VW SLP_S4: %d", wire_state);
	trace1(0, ESPI, 0, "VW_SLP_S4_L change to %d", wire_state);
	espi_vw_power_signal_interrupt(VW_SLP_S4_L);
}

void espi_vw_evt_slp_s5_n(uint32_t wire_state, uint32_t bpos)
{
	CPRINTS("VW SLP_S5: %d", wire_state);
	trace1(0, ESPI, 0, "VW_SLP_S5_L change to %d", wire_state);
	espi_vw_power_signal_interrupt(VW_SLP_S5_L);
}

void espi_vw_evt_sus_stat_n(uint32_t wire_state, uint32_t bpos)
{
	CPRINTS("VW SUS_STAT: %d", wire_state);
	trace1(0, ESPI, 0, "VW_SUS_STAT change to %d", wire_state);
	espi_vw_power_signal_interrupt(VW_SUS_STAT_L);
}

/* PLTRST# event handler */
void espi_vw_evt_pltrst_n(uint32_t wire_state, uint32_t bpos)
{
	CPRINTS("VW PLTRST#: %d", wire_state);
	trace1(0, ESPI, 0, "VW_PLTRST# change to %d", wire_state);

	if (wire_state) /* Platform Reset de-assertion */
		espi_host_init();
	else /* assertion */
#ifdef CONFIG_CHIPSET_RESET_HOOK
		hook_call_deferred(&espi_chipset_reset_data, MSEC);
#endif

}

/* OOB Reset Warn event handler */
void espi_vw_evt_oob_rst_warn(uint32_t wire_state, uint32_t bpos)
{
	CPRINTS("VW OOB_RST_WARN: %d", wire_state);
	trace1(0, ESPI, 0, "VW_OOB_RST_WARN change to %d", wire_state);

	espi_oob_flush();

	espi_vw_set_wire(VW_OOB_RST_ACK, wire_state);
}

/* SUS_WARN# event handler */
void espi_vw_evt_sus_warn_n(uint32_t wire_state, uint32_t bpos)
{
	CPRINTS("VW SUS_WARN#: %d", wire_state);
	trace1(0, ESPI, 0, "VW_SUS_WARN# change to %d", wire_state);

	udelay(100);

	/*
	 *  Add any Deep Sx prep here
	 * NOTE: we could schedule a deferred function and have
	 * it send ACK to host after preparing for Deep Sx
	 */
#ifdef CONFIG_MCHP_ESPI_VW_SAVE_ON_SLEEP
	espi_vw_save();
#endif
	/* Send ACK to host by WARN#'s wire */
	espi_vw_set_wire(VW_SUS_ACK, wire_state);
}

/*
 * SUS_PWRDN_ACK
 * PCH is informing us it does not need suspend power well.
 * if SUS_PWRDN_ACK == 1 we can turn off suspend power well assuming
 * hardware design allow.
 */
void espi_vw_evt_sus_pwrdn_ack(uint32_t wire_state, uint32_t bpos)
{
	trace1(0, ESPI, 0, "VW_SUS_PWRDN_ACK change to %d", wire_state);
	CPRINTS("VW SUS_PWRDN_ACK: %d", wire_state);
}

/* SLP_A#(SLP_M#) */
void espi_vw_evt_slp_a_n(uint32_t wire_state, uint32_t bpos)
{
	CPRINTS("VW SLP_A: %d", wire_state);
	trace1(0, ESPI, 0, "VW_SLP_A# change to %d", wire_state);

	/* Put handling of ASW well devices here, if any */
}

/* HOST_RST WARN event handler */
void espi_vw_evt_host_rst_warn(uint32_t wire_state, uint32_t bpos)
{
	CPRINTS("VW HOST_RST_WARN: %d", wire_state);
	trace1(0, ESPI, 0, "VW_HOST_RST_WARN change to %d", wire_state);

	espi_pc_flush();

	/* Send HOST_RST_ACK to host */
	espi_vw_set_wire(VW_HOST_RST_ACK, wire_state);
}

/* SLP_LAN# */
void espi_vw_evt_slp_lan_n(uint32_t wire_state, uint32_t bpos)
{
	CPRINTS("VW SLP_LAN: %d", wire_state);
	trace1(0, ESPI, 0, "VW_SLP_LAN# change to %d", wire_state);
}

/* SLP_WLAN# */
void espi_vw_evt_slp_wlan_n(uint32_t wire_state, uint32_t bpos)
{
	CPRINTS("VW SLP_WLAN: %d", wire_state);
	trace1(0, ESPI, 0, "VW_SLP_WLAN# change to %d", wire_state);
}

void espi_vw_evt_host_c10(uint32_t wire_state, uint32_t bpos)
{
	CPRINTS("VW HOST_C10: %d", wire_state);
	trace1(0, ESPI, 0, "VW_HOST_C10 change to %d", wire_state);
}

void espi_vw_evt1_dflt(uint32_t wire_state, uint32_t bpos)
{
	CPRINTS("Unknown M2S VW: state=%d GIRQ24 bitpos=%d", wire_state, bpos);
	MCHP_INT_DISABLE(24) = (1ul << bpos);
}

void espi_vw_evt2_dflt(uint32_t wire_state, uint32_t bpos)
{
	CPRINTS("Unknown M2S VW: state=%d GIRQ25 bitpos=%d", wire_state, bpos);
	MCHP_INT_DISABLE(25) = (1ul << bpos);
}

/************************************************************************/
/* Interrupt handlers */

/* MEC1701H
 * GIRQ19 all direct connect capable, none wake capable
 *	b[0] = Peripheral Channel (PC)
 *	b[1] = Bus Master 1 (BM1)
 *	b[2] = Bus Master 2 (BM2)
 *	b[3] = LTR
 *	b[4] = OOB_UP
 *	b[5] = OOB_DN
 *	b[6] = Flash Channel (FC)
 *	b[7] = ESPI_RESET# change
 *	b[8] = VWire Channel (VW) enable assertion
 *	b[9:31] = 0 reserved
 *
 * GIRQ22 b[9]=ESPI interface wake peripheral logic only, not EC.
 *	Not direct connect capable
 *
 * GIRQ24
 *	b[0:3]   = MSVW00_SRC[0:3]
 *	b[4:7]   = MSVW01_SRC[0:3]
 *	b[8:11]  = MSVW02_SRC[0:3]
 *	b[12:15] = MSVW03_SRC[0:3]
 *	b[16:19] = MSVW04_SRC[0:3]
 *	b[20:23] = MSVW05_SRC[0:3]
 *	b[24:27] = MSVW06_SRC[0:3]
 *	b[28:31] = 0 reserved
 *
 * GIRQ25
 *	b[0:3]   = MSVW07_SRC[0:3]
 *	b[4:7]   = MSVW08_SRC[0:3]
 *	b[8:11]  = MSVW09_SRC[0:3]
 *	b[12:15] = MSVW10_SRC[0:3]
 *	b[16:31] = 0 reserved
 *
 */

typedef void (*FPVW)(uint32_t, uint32_t);

#define MCHP_GIRQ24_NUM_M2S	(7 * 4)
const FPVW girq24_vw_handlers[MCHP_GIRQ24_NUM_M2S] = {
	espi_vw_evt_slp_s3_n,	/* MSVW00, Host M2S 02h */
	espi_vw_evt_slp_s4_n,
	espi_vw_evt_slp_s5_n,
	espi_vw_evt1_dflt,
	espi_vw_evt_sus_stat_n,	/* MSVW01, Host M2S 03h */
	espi_vw_evt_pltrst_n,
	espi_vw_evt_oob_rst_warn,
	espi_vw_evt1_dflt,
	espi_vw_evt_host_rst_warn, /* MSVW02, Host M2S 07h */
	espi_vw_evt1_dflt,
	espi_vw_evt1_dflt,
	espi_vw_evt1_dflt,
	espi_vw_evt_sus_warn_n,	/* MSVW03, Host M2S 41h */
	espi_vw_evt_sus_pwrdn_ack,
	espi_vw_evt1_dflt,
	espi_vw_evt_slp_a_n,
	espi_vw_evt_slp_lan_n,	/* MSVW04, Host M2S 42h */
	espi_vw_evt_slp_wlan_n,
	espi_vw_evt1_dflt,
	espi_vw_evt1_dflt,
	espi_vw_evt1_dflt,	/* MSVW05, Host M2S 43h */
	espi_vw_evt1_dflt,
	espi_vw_evt1_dflt,
	espi_vw_evt1_dflt,
	espi_vw_evt1_dflt,	/* MSVW06, Host M2S 44h */
	espi_vw_evt1_dflt,
	espi_vw_evt1_dflt,
	espi_vw_evt1_dflt
};

#define MCHP_GIRQ25_NUM_M2S	(4 * 4)
const FPVW girq25_vw_handlers[MCHP_GIRQ25_NUM_M2S] = {
	espi_vw_evt_host_c10,	/* MSVW07, Host M2S 47h */
	espi_vw_evt2_dflt,
	espi_vw_evt2_dflt,
	espi_vw_evt2_dflt,
	espi_vw_evt2_dflt,	/* MSVW08 unassigned */
	espi_vw_evt2_dflt,
	espi_vw_evt2_dflt,
	espi_vw_evt2_dflt,
	espi_vw_evt2_dflt,	/* MSVW09 unassigned */
	espi_vw_evt2_dflt,
	espi_vw_evt2_dflt,
	espi_vw_evt2_dflt,
	espi_vw_evt2_dflt,	/* MSVW10 unassigned */
	espi_vw_evt2_dflt,
	espi_vw_evt2_dflt,
	espi_vw_evt2_dflt,
};

/* Interrupt handler for eSPI virtual wires in MSVW00 - MSVW01 */
void espi_mswv1_interrupt(void)
{
	uint32_t d, girq24_result, bpos;

	d = MCHP_INT_ENABLE(24);
	girq24_result = MCHP_INT_RESULT(24);
	MCHP_INT_SOURCE(24) = girq24_result;

	bpos = __builtin_ctz(girq24_result); /* rbit, clz sequence */
	while (bpos != 32) {
		d = *(uint8_t *)(MCHP_ESPI_MSVW_BASE + 8 +
				(12 * (bpos >> 2)) + (bpos & 0x03)) & 0x01;
		(girq24_vw_handlers[bpos])(d, bpos);
		girq24_result &= ~(1ul << bpos);
		bpos = __builtin_ctz(girq24_result);
	}
}
DECLARE_IRQ(MCHP_IRQ_GIRQ24, espi_mswv1_interrupt, 2);


/* Interrupt handler for eSPI virtual wires in MSVW07 - MSVW10 */
void espi_msvw2_interrupt(void)
{
	uint32_t d, girq25_result, bpos;

	d = MCHP_INT_ENABLE(25);
	girq25_result = MCHP_INT_RESULT(25);
	MCHP_INT_SOURCE(25) = girq25_result;

	bpos = __builtin_ctz(girq25_result); /* rbit, clz sequence */
	while (bpos != 32) {
		d = *(uint8_t *)(MCHP_ESPI_MSVW_BASE + (12 * 7) + 8 +
				(12 * (bpos >> 2)) + (bpos & 0x03)) & 0x01;
		(girq25_vw_handlers[bpos])(d, bpos);
		girq25_result &= ~(1ul << bpos);
		bpos = __builtin_ctz(girq25_result);
	}
}
DECLARE_IRQ(MCHP_IRQ_GIRQ25, espi_msvw2_interrupt, 2);



/*
 * NOTES:
 * While ESPI_RESET# is asserted, all eSPI blocks are held in reset and
 * their registers can't be programmed. All channel Enable and Ready bits
 * are cleared. The only operational logic is the ESPI_RESET# change
 * detection logic.
 * Once ESPI_RESET# de-asserts, firmware can enable interrupts on all
 * other eSPI channels/components.
 * Implications are:
 * ESPI_RESET# assertion -
 *	All channel ready bits are cleared stopping all outstanding
 *	transactions and clearing registers and internal FIFO's.
 * ESPI_RESET# de-assertion -
 *	All channels/components can now be programmed and can detect
 *	reception of channel enable messages from the eSPI Master.
 */

/*
 * eSPI Reset change handler
 * Multiple scenarios must be handled.
 * eSPI Link initialization from de-assertion of RSMRST#
 *	Upon RSMRST# de-assertion, the PCH may drive ESPI_RESET# low
 *	and then back high. If the platform has a pull-down on ESPI_RESET#
 *	then we will not see both edges. We must handle the scenario where
 *	ESPI_RESET# has only a rising edge or is pulsed low once RSMRST#
 *	has been released.
 * eSPI Link is operational and PCH asserts ESPI_RESET# due to
 * global reset event or some other system problem.
 *	eSPI link is operational and the system generates a global reset
 *	event to the PCH. EC is unaware of global reset and sees PCH
 *	activate ESPI_RESET#.
 *
 * ESPI_RESET# assertion will disable all MCHP eSPI channel ready
 * bits and place all channels is reset state. Any hardware affected by
 * ESPI_RESET# must be re-initialized after ESPI_RESET# de-asserts.
 *
 * Note ESPI_RESET# is not equivalent to LPC LRESET#. LRESET# is
 * equivalent to eSPI Platform Reset.
 *
 */
void espi_reset_isr(void)
{
	uint8_t erst;

	erst = MCHP_ESPI_IO_RESET_STATUS;
	MCHP_ESPI_IO_RESET_STATUS = erst;
	MCHP_INT_SOURCE(MCHP_ESPI_GIRQ) = MCHP_ESPI_RESET_GIRQ_BIT;
	if (erst & (1ul << 1)) { /* rising edge - reset de-asserted */
		MCHP_INT_ENABLE(MCHP_ESPI_GIRQ) = (
				MCHP_ESPI_PC_GIRQ_BIT +
				MCHP_ESPI_OOB_TX_GIRQ_BIT +
				MCHP_ESPI_FC_GIRQ_BIT +
				MCHP_ESPI_VW_EN_GIRQ_BIT);
		MCHP_ESPI_OOB_TX_IEN = (1ul << 1);
		MCHP_ESPI_FC_IEN = (1ul << 1);
		MCHP_ESPI_PC_IEN = (1ul << 25);
		CPRINTS("eSPI Reset de-assert");
		trace0(0, ESPI, 0, "eSPI Reset de-assert");

	} else { /* falling edge - reset asserted */
		MCHP_INT_SOURCE(MCHP_ESPI_GIRQ) = (
					MCHP_ESPI_PC_GIRQ_BIT +
					MCHP_ESPI_OOB_TX_GIRQ_BIT +
					MCHP_ESPI_FC_GIRQ_BIT +
					MCHP_ESPI_VW_EN_GIRQ_BIT);
		MCHP_INT_DISABLE(MCHP_ESPI_GIRQ) = (
					MCHP_ESPI_PC_GIRQ_BIT +
					MCHP_ESPI_OOB_TX_GIRQ_BIT +
					MCHP_ESPI_FC_GIRQ_BIT +
					MCHP_ESPI_VW_EN_GIRQ_BIT);
		espi_channels_ready = 0;

		chipset_handle_espi_reset_assert();

		CPRINTS("eSPI Reset assert");
		trace0(0, ESPI, 0, "eSPI Reset assert");
	}
}
DECLARE_IRQ(MCHP_IRQ_ESPI_RESET, espi_reset_isr, 3);

/*
 * eSPI Virtual Wire channel enable handler
 * Must disable once VW Enable is set by eSPI Master
 */
void espi_vw_en_isr(void)
{
	MCHP_INT_DISABLE(MCHP_ESPI_GIRQ) = MCHP_ESPI_VW_EN_GIRQ_BIT;
	MCHP_INT_SOURCE(MCHP_ESPI_GIRQ) = MCHP_ESPI_VW_EN_GIRQ_BIT;

	MCHP_ESPI_IO_VW_READY = 1;

	espi_channels_ready |= (1ul << 0);

	CPRINTS("eSPI VW Enable received, set VW Ready");
	trace0(0, ESPI, 0, "VW Enable. Set VW Ready");

	if (0x03 == (espi_channels_ready & 0x03))
		espi_send_boot_load_done();
}
DECLARE_IRQ(MCHP_IRQ_ESPI_VW_EN, espi_vw_en_isr, 2);


/*
 * eSPI OOB TX and OOB channel enable change interrupt handler
 */
void espi_oob_tx_isr(void)
{
	uint32_t sts;

	sts = MCHP_ESPI_OOB_TX_STATUS;
	MCHP_ESPI_OOB_TX_STATUS = sts;
	MCHP_INT_SOURCE(MCHP_ESPI_GIRQ) = MCHP_ESPI_OOB_TX_GIRQ_BIT;
	if (sts & (1ul << 1)) {
		/* Channel Enable change */
		if (sts & (1ul << 9)) { /* enable? */
			MCHP_ESPI_OOB_RX_LEN = 73;
			MCHP_ESPI_IO_OOB_READY = 1;
			espi_channels_ready |= (1ul << 2);
			CPRINTS("eSPI OOB_UP ISR: OOB Channel Enable");
			trace0(0, ESPI, 0, "OOB_TX OOB Enable");
		} else { /* no, disabled by Master */
			espi_channels_ready &= ~(1ul << 2);
			CPRINTS("eSPI OOB_UP ISR: OOB Channel Disable");
			trace0(0, ESPI, 0, "eSPI OOB_TX OOB Disable");
		}
	} else {
		/* Handle OOB Up transmit status: done and/or errors, here */
		CPRINTS("eSPI OOB_UP status = 0x%x", sts);
		trace11(0, ESPI, 0, "eSPI OOB_TX Status = 0x%08x", sts);
	}
}
DECLARE_IRQ(MCHP_IRQ_ESPI_OOB_UP, espi_oob_tx_isr, 2);


/* eSPI OOB RX interrupt handler */
void espi_oob_rx_isr(void)
{
	uint32_t sts;

	sts = MCHP_ESPI_OOB_RX_STATUS;
	MCHP_ESPI_OOB_RX_STATUS = sts;
	MCHP_INT_SOURCE(MCHP_ESPI_GIRQ) = MCHP_ESPI_OOB_RX_GIRQ_BIT;
	/* Handle OOB Up transmit status: done and/or errors, if any */
	CPRINTS("eSPI OOB_DN status = 0x%x", sts);
	trace11(0, ESPI, 0, "eSPI OOB_RX Status = 0x%08x", sts);
}
DECLARE_IRQ(MCHP_IRQ_ESPI_OOB_DN, espi_oob_rx_isr, 2);


/*
 * eSPI Flash Channel enable change and data transfer
 * interrupt handler
 */
void espi_fc_isr(void)
{
	uint32_t sts;

	sts = MCHP_ESPI_FC_STATUS;
	MCHP_ESPI_FC_STATUS = sts;
	MCHP_INT_SOURCE(MCHP_ESPI_GIRQ) = MCHP_ESPI_FC_GIRQ_BIT;
	if (sts & (1ul << 1)) {
		/* Channel Enable change */
		if (sts & (1ul << 0)) { /* enable? */
			MCHP_ESPI_IO_FC_READY = 1;
			espi_channels_ready |= (1ul << 1);
			CPRINTS("eSPI FC ISR: Enable");
			trace0(0, ESPI, 0, "eSPI FC Enable");
			if (0x03 == (espi_channels_ready & 0x03))
				espi_send_boot_load_done();
		} else { /* no, disabled by Master */
			espi_channels_ready &= ~(1ul << 1);
			CPRINTS("eSPI FC ISR: Disable");
			trace0(0, ESPI, 0, "eSPI FC Disable");
		}
	} else {
		/* Handle FC command status: done and/or errors */
		CPRINTS("eSPI FC status = 0x%x", sts);
		trace11(0, ESPI, 0, "eSPI FC Status = 0x%08x", sts);
	}
}
DECLARE_IRQ(MCHP_IRQ_ESPI_FC, espi_fc_isr, 2);


/* eSPI Peripheral Channel interrupt handler */
void espi_pc_isr(void)
{
	uint32_t sts;

	sts = MCHP_ESPI_PC_STATUS;
	MCHP_ESPI_PC_STATUS = sts;
	MCHP_INT_SOURCE(MCHP_ESPI_GIRQ) = MCHP_ESPI_PC_GIRQ_BIT;
	if (sts & (1ul << 25)) {
		if (sts & (1ul << 24)) {
			MCHP_ESPI_IO_PC_READY = 1;
			espi_channels_ready |= (1ul << 3);
			CPRINTS("eSPI PC Channel Enable");
			trace0(0, ESPI, 0, "eSPI PC Enable");
		} else {
			espi_channels_ready &= ~(1ul << 3);
			CPRINTS("eSPI PC Channel Disable");
			trace0(0, ESPI, 0, "eSPI PC Disable");
		}

	} else {
		/* Handler PC channel errors here */
		CPRINTS("eSPI PC status = 0x%x", sts);
		trace11(0, ESPI, 0, "eSPI PC Status = 0x%08x", sts);
	}
}
DECLARE_IRQ(MCHP_IRQ_ESPI_PC, espi_pc_isr, 2);


/************************************************************************/

/*
 * Enable/disable direct mode interrupt for ESPI_RESET# change.
 * Optionally clear status before enable or after disable.
 */
static void espi_reset_ictrl(int enable, int clr_status)
{
	if (enable) {
		if (clr_status) {
			MCHP_ESPI_IO_RESET_STATUS =
					MCHP_ESPI_RST_CHG_STS;
			MCHP_INT_SOURCE(MCHP_ESPI_GIRQ) =
					MCHP_ESPI_RESET_GIRQ_BIT;
		}
		MCHP_ESPI_IO_RESET_IEN |= MCHP_ESPI_RST_IEN;
		MCHP_INT_ENABLE(MCHP_ESPI_GIRQ) =
				MCHP_ESPI_RESET_GIRQ_BIT;
		task_enable_irq(MCHP_IRQ_ESPI_RESET);
	} else {
		task_disable_irq(MCHP_IRQ_ESPI_RESET);
		MCHP_INT_DISABLE(MCHP_ESPI_GIRQ) =
				MCHP_ESPI_RESET_GIRQ_BIT;
		MCHP_ESPI_IO_RESET_IEN &= ~(MCHP_ESPI_RST_IEN);
		if (clr_status) {
			MCHP_ESPI_IO_RESET_STATUS =
					MCHP_ESPI_RST_CHG_STS;
			MCHP_INT_SOURCE(MCHP_ESPI_GIRQ) =
					MCHP_ESPI_RESET_GIRQ_BIT;
		}
	}
}

/* eSPI Initialization functions */

/* MEC1701H */
void espi_init(void)
{
	espi_channels_ready = 0;

	CPRINTS("eSPI - espi_init");
	trace0(0, ESPI, 0, "eSPI Init");

	/* Clear PCR eSPI sleep enable */
	MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_ESPI);

	/*
	 * b[8]=0(eSPI PLTRST# VWire is platform reset), b[0]=0
	 * VCC_PWRGD is asserted when PLTRST# VWire is 1(inactive)
	 */
	MCHP_PCR_PWR_RST_CTL = 0;

	/*
	 * There is no MODULE_ESPI in include/module_id.h
	 * eSPI pins marked as MODULE_LPC in board/myboard/board.h
	 * eSPI pins are on VTR3.
	 * Make sure VTR3 chip knows VTR3 is 1.8V
	 * This is done in system_pre_init()
	 */
	gpio_config_module(MODULE_LPC, 1);

	/* Override Boot-ROM configuration */
#ifdef CONFIG_HOSTCMD_ESPI_EC_CHAN_BITMAP
	MCHP_ESPI_IO_CAP0 = CONFIG_HOSTCMD_ESPI_EC_CHAN_BITMAP;
#endif

#ifdef CONFIG_HOSTCMD_ESPI_EC_MAX_FREQ
	MCHP_ESPI_IO_CAP1 &= ~(MCHP_ESPI_CAP1_MAX_FREQ_MASK);
#if CONFIG_HOSTCMD_ESPI_EC_MAX_FREQ == 25
	MCHP_ESPI_IO_CAP1 |= MCHP_ESPI_CAP1_MAX_FREQ_25M;
#elif CONFIG_HOSTCMD_ESPI_EC_MAX_FREQ == 33
	MCHP_ESPI_IO_CAP1 |= MCHP_ESPI_CAP1_MAX_FREQ_33M;
#elif CONFIG_HOSTCMD_ESPI_EC_MAX_FREQ == 50
	MCHP_ESPI_IO_CAP1 |= MCHP_ESPI_CAP1_MAX_FREQ_50M;
#elif CONFIG_HOSTCMD_ESPI_EC_MAX_FREQ == 66
	MCHP_ESPI_IO_CAP1 |= MCHP_ESPI_CAP1_MAX_FREQ_66M;
#else
	MCHP_ESPI_IO_CAP1 |= MCHP_ESPI_CAP1_MAX_FREQ_20M;
#endif
#endif

#ifdef CONFIG_HOSTCMD_ESPI_EC_MODE
	MCHP_ESPI_IO_CAP1 &= ~(MCHP_ESPI_CAP1_IO_MASK);
	MCHP_ESPI_IO_CAP1 |= ((CONFIG_HOSTCMD_ESPI_EC_MODE)
		<< MCHP_ESPI_CAP1_IO_BITPOS);
#endif

#ifdef CONFIG_HOSTCMD_ESPI
	MCHP_ESPI_IO_PLTRST_SRC = MCHP_ESPI_PLTRST_SRC_VW;
#else
	MCHP_ESPI_IO_PLTRST_SRC = MCHP_ESPI_PLTRST_SRC_PIN;
#endif

	MCHP_PCR_PWR_RST_CTL &=
		~(1ul << MCHP_PCR_PWR_HOST_RST_SEL_BITPOS);

	MCHP_ESPI_ACTIVATE = 1;

	espi_bar_pre_init();

	/*
	 * VWires are configured to be reset by different events.
	 * Default configuration has:
	 * RESET_SYS (chip reset) MSVW00, MSVW04
	 * RESET_ESPI MSVW01, MSVW03, SMVW00, SMVW01
	 * PLTRST MSVW02, SMVW02
	 */
	espi_vw_pre_init();

	/*
	 * Configure MSVW00 & MSVW04
	 * Any change to default values (SRCn bits)
	 * Any change to interrupt enable, SRCn_IRQ_SELECT bit fields
	 *   Should interrupt bits in MSVWyx and GIRQ24/25 be touched
	 *   before ESPI_RESET# de-asserts?
	 */

	MCHP_ESPI_PC_STATUS = 0xfffffffful;
	MCHP_ESPI_OOB_RX_STATUS = 0xfffffffful;
	MCHP_ESPI_FC_STATUS = 0xfffffffful;
	MCHP_INT_DISABLE(MCHP_ESPI_GIRQ) = 0x1FFul;
	MCHP_INT_SOURCE(MCHP_ESPI_GIRQ) = 0x1FFul;

	task_enable_irq(MCHP_IRQ_ESPI_PC);
	task_enable_irq(MCHP_IRQ_ESPI_OOB_UP);
	task_enable_irq(MCHP_IRQ_ESPI_OOB_DN);
	task_enable_irq(MCHP_IRQ_ESPI_FC);
	task_enable_irq(MCHP_IRQ_ESPI_VW_EN);

	/* Enable eSPI Master-to-Slave Virtual wire NVIC inputs
	 * VWire block interrupts are all disabled by default
	 * and will be controlled by espi_vw_enable/disable_wire_in
	 */
	CPRINTS("eSPI - enable ESPI_RESET# interrupt");
	trace0(0, ESPI, 0, "Enable ESPI_RESET# interrupt");

	/* Enable ESPI_RESET# interrupt and clear status */
	espi_reset_ictrl(1, 1);

	CPRINTS("eSPI - espi_init - done");
	trace0(0, ESPI, 0, "eSPI Init Done");

}


#ifdef CONFIG_MCHP_ESPI_EC_CMD
/* TODO */
static int command_espi(int argc, char **argv)
{
	uint32_t chan, w0, w1, w2;
	char *e;

	if (argc == 1) {
		return EC_ERROR_INVAL;
	/* Get value of eSPI registers */
	} else if (argc == 2) {
		int i;

		if (strcasecmp(argv[1], "cfg") == 0) {
			ccprintf("eSPI Reg32A [0x%08x]\n",
				 MCHP_ESPI_IO_REG32_A);
			ccprintf("eSPI Reg32B [0x%08x]\n",
				 MCHP_ESPI_IO_REG32_B);
			ccprintf("eSPI Reg32C [0x%08x]\n",
				 MCHP_ESPI_IO_REG32_C);
			ccprintf("eSPI Reg32D [0x%08x]\n",
				 MCHP_ESPI_IO_REG32_D);
		} else if (strcasecmp(argv[1], "vsm") == 0) {
			for (i = 0; i < MSVW_MAX; i++) {
				w0 = MSVW(i, 0);
				w1 = MSVW(i, 1);
				w2 = MSVW(i, 2);
				ccprintf("MSVW%d: 0x%08x:%08x:%08x\n", i,
					w2, w1, w0);
			}
		} else if (strcasecmp(argv[1], "vms") == 0) {
			for (i = 0; i < SMVW_MAX; i++) {
				w0 = SMVW(i, 0);
				w1 = SMVW(i, 1);
				ccprintf("SMVW%d: 0x%08x:%08x\n", i, w1, w0);
			}
		}
	/* Enable/Disable the channels of eSPI */
	} else if (argc == 3) {
		uint32_t m = (uint32_t) strtoi(argv[2], &e, 0);

		if (*e)
			return EC_ERROR_PARAM2;
		if (m < 0 || m > 4)
			return EC_ERROR_PARAM2;
		else if (m == 4)
			chan = 0x0F;
		else
			chan = 0x01 << m;
		if (strcasecmp(argv[1], "en") == 0)
			MCHP_ESPI_IO_CAP0 |= chan;
		else if (strcasecmp(argv[1], "dis") == 0)
			MCHP_ESPI_IO_CAP0 &= ~chan;
		else
			return EC_ERROR_PARAM1;
		ccprintf("eSPI IO Cap0 [0x%02x]\n", MCHP_ESPI_IO_CAP0);
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(espi, command_espi,
			"cfg/vms/vsm/en/dis [channel]",
			"eSPI configurations");
#endif
