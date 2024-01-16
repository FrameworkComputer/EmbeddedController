/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Specific register map for NPCX5 family of chips.
 *
 * Support chip variant:
 * - npcx5m5g
 * - npcx5m6g
 *
 * This header file should not be included directly.
 * Please include registers.h instead.
 */

#ifndef __CROS_EC_REGISTERS_H
#error "This header file should not be included directly."
#endif

/* NPCX-IRQ numbers */
#define NPCX_IRQ0_NOUSED NPCX_IRQ_0
#define NPCX_IRQ1_NOUSED NPCX_IRQ_1
#define NPCX_IRQ_KBSCAN NPCX_IRQ_2
#define NPCX_IRQ_PM_CHAN_OBE NPCX_IRQ_3
#define NPCX_IRQ_PECI NPCX_IRQ_4
#define NPCX_IRQ5_NOUSED NPCX_IRQ_5
#define NPCX_IRQ_PORT80 NPCX_IRQ_6
#define NPCX_IRQ_MTC_WKINTAD_0 NPCX_IRQ_7
#define NPCX_IRQ_MTC NPCX_IRQ_MTC_WKINTAD_0
#define NPCX_IRQ8_NOUSED NPCX_IRQ_8
#define NPCX_IRQ_MFT_1 NPCX_IRQ_9
#define NPCX_IRQ_ADC NPCX_IRQ_10
#define NPCX_IRQ_WKINTEFGH_0 NPCX_IRQ_11
#define NPCX_IRQ_GDMA NPCX_IRQ_12
#define NPCX_IRQ_SMB1 NPCX_IRQ_13
#define NPCX_IRQ_SMB2 NPCX_IRQ_14
#define NPCX_IRQ_WKINTC_0 NPCX_IRQ_15
#define NPCX_IRQ16_NOUSED NPCX_IRQ_16
#define NPCX_IRQ_ITIM16_3 NPCX_IRQ_17
#define NPCX_IRQ_SHI NPCX_IRQ_18
#define NPCX_IRQ_ESPI NPCX_IRQ_18
#define NPCX_IRQ19_NOUSED NPCX_IRQ_19
#define NPCX_IRQ20_NOUSED NPCX_IRQ_20
#define NPCX_IRQ_PS2 NPCX_IRQ_21
#define NPCX_IRQ22_NOUSED NPCX_IRQ_22
#define NPCX_IRQ_MFT_2 NPCX_IRQ_23
#define NPCX_IRQ_SHM NPCX_IRQ_24
#define NPCX_IRQ_KBC_IBF NPCX_IRQ_25
#define NPCX_IRQ_PM_CHAN_IBF NPCX_IRQ_26
#define NPCX_IRQ_ITIM16_2 NPCX_IRQ_27
#define NPCX_IRQ_ITIM16_1 NPCX_IRQ_28
#define NPCX_IRQ29_NOUSED NPCX_IRQ_29
#define NPCX_IRQ30_NOUSED NPCX_IRQ_30
#define NPCX_IRQ_TWD_WKINTB_0 NPCX_IRQ_31
#define NPCX_IRQ32_NOUSED NPCX_IRQ_32
#define NPCX_IRQ_UART NPCX_IRQ_33
#define NPCX_IRQ34_NOUSED NPCX_IRQ_34
#define NPCX_IRQ35_NOUSED NPCX_IRQ_35
#define NPCX_IRQ_SMB3 NPCX_IRQ_36
#define NPCX_IRQ_SMB4 NPCX_IRQ_37
#define NPCX_IRQ38_NOUSED NPCX_IRQ_38
#define NPCX_IRQ39_NOUSED NPCX_IRQ_39
#define NPCX_IRQ40_NOUSED NPCX_IRQ_40
#define NPCX_IRQ_MFT_3 NPCX_IRQ_41
#define NPCX_IRQ42_NOUSED NPCX_IRQ_42
#define NPCX_IRQ_ITIM16_4 NPCX_IRQ_43
#define NPCX_IRQ_ITIM16_5 NPCX_IRQ_44
#define NPCX_IRQ_ITIM16_6 NPCX_IRQ_45
#define NPCX_IRQ_ITIM32 NPCX_IRQ_46
#define NPCX_IRQ_WKINTA_1 NPCX_IRQ_47
#define NPCX_IRQ_WKINTB_1 NPCX_IRQ_48
#define NPCX_IRQ_KSI_WKINTC_1 NPCX_IRQ_49
#define NPCX_IRQ_WKINTD_1 NPCX_IRQ_50
#define NPCX_IRQ_WKINTE_1 NPCX_IRQ_51
#define NPCX_IRQ_WKINTF_1 NPCX_IRQ_52
#define NPCX_IRQ_WKINTG_1 NPCX_IRQ_53
#define NPCX_IRQ_WKINTH_1 NPCX_IRQ_54
#define NPCX_IRQ55_NOUSED NPCX_IRQ_55
#define NPCX_IRQ_KBC_OBE NPCX_IRQ_56
#define NPCX_IRQ_SPI NPCX_IRQ_57
#define NPCX_IRQ58_NOUSED NPCX_IRQ_58
#define NPCX_IRQ_WKINTFG_2 NPCX_IRQ_59
#define NPCX_IRQ_WKINTA_2 NPCX_IRQ_60
#define NPCX_IRQ_WKINTB_2 NPCX_IRQ_61
#define NPCX_IRQ_WKINTC_2 NPCX_IRQ_62
#define NPCX_IRQ_WKINTD_2 NPCX_IRQ_63

/* Modules Map */

/* Miscellaneous Device Control (MDC) registers */
#define NPCX_FWCTRL REG8(NPCX_MDC_BASE_ADDR + 0x007)

/* MDC register fields */
#define NPCX_FWCTRL_RO_REGION 0
#define NPCX_FWCTRL_FW_SLOT 1

#define NPCX_ITIM32_BASE_ADDR 0x400BC000
#define NPCX_CR_UART_BASE_ADDR(mdl) (0x400C4000 + ((mdl) * 0x2000L))
#define NPCX_SMB_BASE_ADDR(mdl)                           \
	(((mdl) < 2) ? (0x40009000 + ((mdl) * 0x2000L)) : \
		       (0x400C0000 + (((mdl)-2) * 0x2000L)))

enum {
	NPCX_UART_PORT0 = 0, /* UART port 0 */
	NPCX_UART_COUNT
};

/* System Configuration (SCFG) Registers */

/* SCFG enumeration */
enum {
	ALT_GROUP_0,
	ALT_GROUP_1,
	ALT_GROUP_2,
	ALT_GROUP_3,
	ALT_GROUP_4,
	ALT_GROUP_5,
	ALT_GROUP_6,
	ALT_GROUP_7,
	ALT_GROUP_8,
	ALT_GROUP_9,
	ALT_GROUP_A,
	ALT_GROUP_B,
	ALT_GROUP_C,
	ALT_GROUP_D,
	ALT_GROUP_E,
	ALT_GROUP_F,
	ALT_GROUP_COUNT
};

#define NPCX_DEVALT(n) REG8(NPCX_SCFG_BASE_ADDR + 0x010 + (n))

#define NPCX_LV_GPIO_CTL(n) REG8(NPCX_SCFG_BASE_ADDR + 0x02A + (n))

/* pin-mux for JTAG */
#define NPCX_DEVALT5_NJEN1_EN 1
#define NPCX_DEVALT5_NJEN0_EN 2

/* pin-mux for I2C */
#define NPCX_DEVALT2_I2C0_0_SL 0
#define NPCX_DEVALT2_I2C0_1_SL 1
#define NPCX_DEVALT2_I2C1_0_SL 2
#define NPCX_DEVALT2_I2C2_0_SL 4
#define NPCX_DEVALT2_I2C3_0_SL 6

/* pin-mux for UART */
#define NPCX_DEVALTA_UART_SL1 7
#define NPCX_DEVALTC_UART_SL2 0

/* pin-mux for Misc. */
/* External 32KHz crytal osc. input support */
#define NPCX_DEVALTA_32KCLKIN_SL 3

/* SMBus register fields */
#define NPCX_SMBSEL_SMB0SEL 0

/* SMB enumeration: I2C port definitions. */
enum {
	NPCX_I2C_PORT0_0 = 0, /* I2C port 0, bus 0 */
	NPCX_I2C_PORT0_1, /* I2C port 0, bus 1 */
	NPCX_I2C_PORT1, /* I2C port 1 */
	NPCX_I2C_PORT2, /* I2C port 2 */
	NPCX_I2C_PORT3, /* I2C port 3 */
	NPCX_I2C_COUNT,
};

/*
 * PMC enumeration:
 * Offsets from CGC_BASE registers for each peripheral.
 */
enum {
	CGC_OFFSET_KBS = 0,
	CGC_OFFSET_UART = 0,
	CGC_OFFSET_FAN = 0,
	CGC_OFFSET_FIU = 0,
	CGC_OFFSET_PS2 = 0,
	CGC_OFFSET_PWM = 1,
	CGC_OFFSET_I2C = 2,
	CGC_OFFSET_ADC = 3,
	CGC_OFFSET_PECI = 3,
	CGC_OFFSET_SPI = 3,
	CGC_OFFSET_TIMER = 3,
	CGC_OFFSET_LPC = 4,
	CGC_OFFSET_ESPI = 5,
};

enum NPCX_PMC_PWDWN_CTL_T {
	NPCX_PMC_PWDWN_1 = 0,
	NPCX_PMC_PWDWN_2 = 1,
	NPCX_PMC_PWDWN_3 = 2,
	NPCX_PMC_PWDWN_4 = 3,
	NPCX_PMC_PWDWN_5 = 4,
	NPCX_PMC_PWDWN_6 = 5,
	NPCX_PMC_PWDWN_CNT,
};

#define CGC_I2C_MASK                                                   \
	(BIT(NPCX_PWDWN_CTL3_SMB0_PD) | BIT(NPCX_PWDWN_CTL3_SMB1_PD) | \
	 BIT(NPCX_PWDWN_CTL3_SMB2_PD) | BIT(NPCX_PWDWN_CTL3_SMB3_PD))

/* BBRAM register fields */
#define NPCX_BKUP_STS_ALL_MASK BIT(NPCX_BKUP_STS_IBBR)
#define NPCX_BBRAM_SIZE 64 /* Size of BBRAM */

/* ITIM registers */
#define NPCX_ITCNT8(n) REG8(NPCX_ITIM_BASE_ADDR(n) + 0x000)
#define NPCX_ITCNT16(n) REG16(NPCX_ITIM_BASE_ADDR(n) + 0x002)
/* ITIM32 registers */
#define NPCX_ITCNT32 REG32(NPCX_ITIM32_BASE_ADDR + 0x008)

/* Timer counter register used for 1 micro-second system tick */
#define NPCX_ITCNT_SYSTEM NPCX_ITCNT32
/* Timer counter register used for others */
#define NPCX_ITCNT NPCX_ITCNT16

/* ITIM module No. used for event */
#define ITIM_EVENT_NO ITIM16_1
/* ITIM module No. used for watchdog */
#define ITIM_WDG_NO ITIM16_5
/* ITIM module No. used for 1 micro-second system tick */
#define ITIM_SYSTEM_NO ITIM32

/* ITIM enumeration */
enum ITIM_MODULE_T {
	ITIM16_1,
	ITIM16_2,
	ITIM16_3,
	ITIM16_4,
	ITIM16_5,
	ITIM16_6,
	ITIM32,
	ITIM_MODULE_COUNT,
};

/* Serial Host Interface (SHI) Registers */
#define NPCX_OBUF(n) REG8(NPCX_SHI_BASE_ADDR + 0x020 + (n))
#define NPCX_IBUF(n) REG8(NPCX_SHI_BASE_ADDR + 0x060 + (n))

/* Bit field manipulation for VWEVMS Value */
#define VWEVMS_INTWK_EN VWEVMS_INT_EN

/* eSPI max supported frequency */
enum {
	NPCX_ESPI_MAXFREQ_20 = 0,
	NPCX_ESPI_MAXFREQ_25 = 1,
	NPCX_ESPI_MAXFREQ_33 = 2,
	NPCX_ESPI_MAXFREQ_50 = 3,
	NPCX_ESPI_MAXFREQ_66 = 4,
	NPCX_ESPI_MAXFREQ_NONE = 0xFF
};

/* eSPI max frequency support per FMCLK */
#if (FMCLK <= 33000000)
#define NPCX_ESPI_MAXFREQ_MAX NPCX_ESPI_MAXFREQ_33
#elif (FMCLK <= 48000000)
#define NPCX_ESPI_MAXFREQ_MAX NPCX_ESPI_MAXFREQ_50
#else
#define NPCX_ESPI_MAXFREQ_MAX NPCX_ESPI_MAXFREQ_66
#endif

/* MIWU registers */
#define NPCX_WKEDG_ADDR(port, n) \
	(NPCX_MIWU_BASE_ADDR(port) + 0x00 + ((n) * 2L) + ((n) < 5 ? 0 : 0x1E))
#define NPCX_WKAEDG_ADDR(port, n) \
	(NPCX_MIWU_BASE_ADDR(port) + 0x01 + ((n) * 2L) + ((n) < 5 ? 0 : 0x1E))
#define NPCX_WKPND_ADDR(port, n) \
	(NPCX_MIWU_BASE_ADDR(port) + 0x0A + ((n) * 4L) + ((n) < 5 ? 0 : 0x10))
#define NPCX_WKPCL_ADDR(port, n) \
	(NPCX_MIWU_BASE_ADDR(port) + 0x0C + ((n) * 4L) + ((n) < 5 ? 0 : 0x10))
#define NPCX_WKEN_ADDR(port, n) \
	(NPCX_MIWU_BASE_ADDR(port) + 0x1E + ((n) * 2L) + ((n) < 5 ? 0 : 0x12))
#define NPCX_WKINEN_ADDR(port, n) \
	(NPCX_MIWU_BASE_ADDR(port) + 0x1F + ((n) * 2L) + ((n) < 5 ? 0 : 0x12))
#define NPCX_WKMOD_ADDR(port, n) (NPCX_MIWU_BASE_ADDR(port) + 0x70 + (n))

#define NPCX_WKEDG(port, n) REG8(NPCX_WKEDG_ADDR(port, n))
#define NPCX_WKAEDG(port, n) REG8(NPCX_WKAEDG_ADDR(port, n))
#define NPCX_WKPND(port, n) REG8(NPCX_WKPND_ADDR(port, n))
#define NPCX_WKPCL(port, n) REG8(NPCX_WKPCL_ADDR(port, n))
#define NPCX_WKEN(port, n) REG8(NPCX_WKEN_ADDR(port, n))
#define NPCX_WKINEN(port, n) REG8(NPCX_WKINEN_ADDR(port, n))
#define NPCX_WKMOD(port, n) REG8(NPCX_WKMOD_ADDR(port, n))

/* UART registers and functions */
#if NPCX_UART_MODULE2
/*
 * To be used as 2nd parameter to NPCX_WK*() macro, table (1st parameter) is
 * always 1 == MIWU_TABLE_1.
 */
#define NPCX_UART_WK_GROUP 6
#define NPCX_UART_WK_BIT 4
#define NPCX_UART_MIWU_IRQ NPCX_IRQ_WKINTG_1
#define NPCX_UART_DEVALT NPCX_DEVALT(0x0C)
#define NPCX_UART_DEVALT_SL NPCX_DEVALTC_UART_SL2
#define NPCX_UART_ALT_DEVALT NPCX_DEVALT(0x0A)
#define NPCX_UART_ALT_DEVALT_SL NPCX_DEVALTA_UART_SL1
#else /* !NPCX_UART_MODULE2 */

#define NPCX_UART_WK_GROUP 1
#define NPCX_UART_WK_BIT 0
#define NPCX_UART_MIWU_IRQ NPCX_IRQ_WKINTB_1
#define NPCX_UART_DEVALT NPCX_DEVALT(0x0A)
#define NPCX_UART_DEVALT_SL NPCX_DEVALTA_UART_SL1
#define NPCX_UART_ALT_DEVALT NPCX_DEVALT(0x0C)
#define NPCX_UART_ALT_DEVALT_SL NPCX_DEVALTC_UART_SL2
#endif /* NPCX_UART_MODULE2 */

/* This routine checks pending bit of GPIO wake-up functionality */
static inline int uart_is_wakeup_from_gpio(void)
{
	return IS_BIT_SET(NPCX_WKPND(1, NPCX_UART_WK_GROUP), NPCX_UART_WK_BIT);
}

/* This routine checks wake-up functionality from GPIO is enabled or not */
static inline int uart_is_enable_wakeup(void)
{
	return IS_BIT_SET(NPCX_WKEN(1, NPCX_UART_WK_GROUP), NPCX_UART_WK_BIT);
}

/* This routine clears the pending wake-up from GPIO on UART rx pin */
static inline void uart_clear_pending_wakeup(void)
{
	SET_BIT(NPCX_WKPCL(1, NPCX_UART_WK_GROUP), NPCX_UART_WK_BIT);
}

/* This routine enables wake-up functionality from GPIO on UART rx pin */
static inline void uart_enable_wakeup(int enable)
{
	UPDATE_BIT(NPCX_WKEN(1, NPCX_UART_WK_GROUP), NPCX_UART_WK_BIT, enable);
}

/* This routine checks functionality is UART rx or not */
static inline int npcx_is_uart(void)
{
	return IS_BIT_SET(NPCX_UART_DEVALT, NPCX_UART_DEVALT_SL);
}

/* ADC Registers */
#define NPCX_ADCSTS REG16(NPCX_ADC_BASE_ADDR + 0x000)
#define NPCX_ADCCNF REG16(NPCX_ADC_BASE_ADDR + 0x002)
#define NPCX_ATCTL REG16(NPCX_ADC_BASE_ADDR + 0x004)
#define NPCX_ASCADD REG16(NPCX_ADC_BASE_ADDR + 0x006)
#define NPCX_ADCCS REG16(NPCX_ADC_BASE_ADDR + 0x008)
/* NOTE: These are 1-based for the threshold detectors. */
#define NPCX_THRCTL(n) REG16(NPCX_ADC_BASE_ADDR + 0x012 + (2L * (n)))
#define NPCX_THRCTS REG16(NPCX_ADC_BASE_ADDR + 0x01A)
#define NPCX_THR_DCTL(n) REG16(NPCX_ADC_BASE_ADDR + 0x038 + (2L * (n)))
/* NOTE: This is 0-based for the ADC channels. */
#define NPCX_CHNDAT(n) REG16(NPCX_ADC_BASE_ADDR + 0x040 + (2L * (n)))
#define NPCX_ADCCNF2 REG16(NPCX_ADC_BASE_ADDR + 0x020)
#define NPCX_GENDLY REG16(NPCX_ADC_BASE_ADDR + 0x022)
#define NPCX_MEAST REG16(NPCX_ADC_BASE_ADDR + 0x026)

/* ADC register fields */
#define NPCX_ATCTL_SCLKDIV_FIELD FIELD(0, 6)
#define NPCX_ATCTL_DLY_FIELD FIELD(8, 3)
#define NPCX_ASCADD_SADDR_FIELD FIELD(0, 5)
#define NPCX_ADCSTS_EOCEV 0
#define NPCX_ADCCNF_ADCMD_FIELD FIELD(1, 2)
#define NPCX_ADCCNF_ADCRPTC 3
#define NPCX_ADCCNF_INTECEN 6
#define NPCX_ADCCNF_START 4
#define NPCX_ADCCNF_ADCEN 0
#define NPCX_ADCCNF_STOP 11
#define NPCX_CHNDAT_CHDAT_FIELD FIELD(0, 10)
#define NPCX_CHNDAT_NEW 15
#define NPCX_THRCTL_THEN 15
#define NPCX_THRCTL_L_H 14
#define NPCX_THRCTL_CHNSEL FIELD(10, 4)
#define NPCX_THRCTL_THRVAL FIELD(0, 10)
#define NPCX_THRCTS_ADC_WKEN 15
#define NPCX_THRCTS_THR3_IEN 10
#define NPCX_THRCTS_THR2_IEN 9
#define NPCX_THRCTS_THR1_IEN 8
#define NPCX_THRCTS_ADC_EVENT 7
#define NPCX_THRCTS_THR3_STS 2
#define NPCX_THRCTS_THR2_STS 1
#define NPCX_THRCTS_THR1_STS 0
#define NPCX_THR_DCTL_THRD_EN 15
#define NPCX_THR_DCTL_THR_DVAL FIELD(0, 10)

#define NPCX_ADC_THRESH1 1
#define NPCX_ADC_THRESH2 2
#define NPCX_ADC_THRESH3 3
#define NPCX_ADC_THRESH_CNT 3
