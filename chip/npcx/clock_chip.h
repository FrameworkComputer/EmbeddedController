/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific clock module for Chrome EC */

#ifndef __CROS_EC_CLOCK_CHIP_H
#define __CROS_EC_CLOCK_CHIP_H

/*
 * EC clock tree plan: (Default OSC_CLK is 40MHz.)
 *
 * Target OSC_CLK for NPCX7 is 90MHz, FMCLK is 45MHz, CPU and APBs is 15MHz.
 * Target OSC_CLK for NPCX5 is 30MHz, FMCLK is 30MHz, CPU and APBs is 15MHz.
 */
#if defined(CHIP_FAMILY_NPCX5)
/*
 * NPCX5 clock tree: (Please refer Figure 55. for more information.)
 *
 * Suggestion:
 * - OSC_CLK >= 30MHz, FPRED should be 1, else 0.
 *   (Keep FMCLK in 30-50 MHz possibly which is tested strictly.)
 */
/* Target OSC_CLK freq */
#define OSC_CLK 30000000
/* Core clock prescaler */
#if (OSC_CLK >= 30000000)
#define FPRED 1 /* CORE_CLK = OSC_CLK(FMCLK)/2 */
#else
#define FPRED 0 /* CORE_CLK = OSC_CLK(FMCLK) */
#endif
/* Core domain clock */
#define CORE_CLK (OSC_CLK / (FPRED + 1))
/* FMUL clock */
#define FMCLK OSC_CLK
/* APBs source clock */
#define APBSRC_CLK CORE_CLK
/* APB1 clock divider */
#define APB1DIV 3 /* Default APB1 clock = CORE_CLK/4 */
/* APB2 clock divider */
#define APB2DIV 0 /* Let APB2 = CORE_CLK since UART baudrate tolerance */
#elif NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
/*
 * NPCX7 clock tree: (Please refer Figure 58. for more information.)
 *
 * Suggestion:
 * - OSC_CLK >= 80MHz, XF_RANGE should be 1, else 0.
 * - CORE_CLK > 66MHz, AHB6DIV should be 1, else 0.
 * - CORE_CLK > 50MHz, FIUDIV should be 1, else 0.
 */
/* Target OSC_CLK freq */
#define OSC_CLK 90000000
/* Core clock prescaler */
#define FPRED 5 /* CORE_CLK = OSC_CLK/6 */
/* Core domain clock */
#define CORE_CLK (OSC_CLK / (FPRED + 1))
/* FMUL clock */
#if (OSC_CLK >= 80000000)
#define FMCLK (OSC_CLK / 2) /* FMUL clock = OSC_CLK/2 if OSC_CLK >= 80MHz */
#else
#define FMCLK OSC_CLK /* FMUL clock = OSC_CLK */
#endif
/* AHB6 clock */
#if (CORE_CLK > 66000000)
#define AHB6DIV 1 /* AHB6_CLK = CORE_CLK/2 if CORE_CLK > 66MHz */
#else
#define AHB6DIV 0 /* AHB6_CLK = CORE_CLK */
#endif
/* FIU clock divider */
#if (CORE_CLK > 50000000)
#define FIUDIV 1 /* FIU_CLK = CORE_CLK/2 */
#else
#define FIUDIV 0 /* FIU_CLK = CORE_CLK */
#endif
/* APBs source clock */
#define APBSRC_CLK OSC_CLK
/* APB1 clock divider */
#define APB1DIV 5 /* APB1 clock = OSC_CLK/6 */
/* APB2 clock divider */
#define APB2DIV 5 /* APB2 clock = OSC_CLK/6 */
/* APB3 clock divider */
#define APB3DIV 5 /* APB3 clock = OSC_CLK/6 */
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX9
/* APB4 clock divider */
#define APB4DIV 5 /* APB4 clock = OSC_CLK/6 */
#endif
#endif

/* Get APB clock freq */
#define NPCX_APB_CLOCK(no) (APBSRC_CLK / (APB##no##DIV + 1))

/*
 * Frequency multiplier M/N value definitions according to the requested
 * OSC_CLK (Unit:Hz).
 */
#if (OSC_CLK > 80000000)
#define HFCGN    0x82 /* Set XF_RANGE as 1 if OSC_CLK >= 80MHz */
#else
#define HFCGN    0x02
#endif
#if   (OSC_CLK == 100000000)
#define HFCGMH   0x0B
#define HFCGML   0xEC
#elif (OSC_CLK == 90000000)
#define HFCGMH   0x0A
#define HFCGML   0xBA
#elif (OSC_CLK == 80000000)
#define HFCGMH   0x09
#define HFCGML   0x89
#elif (OSC_CLK == 66000000)
#define HFCGMH   0x0F
#define HFCGML   0xBC
#elif (OSC_CLK == 50000000)
#define HFCGMH   0x0B
#define HFCGML   0xEC
#elif (OSC_CLK == 48000000)
#define HFCGMH   0x0B
#define HFCGML   0x72
#elif (OSC_CLK == 40000000)
#define HFCGMH   0x09
#define HFCGML   0x89
#elif (OSC_CLK == 33000000)
#define HFCGMH   0x07
#define HFCGML   0xDE
#elif (OSC_CLK == 30000000)
#define HFCGMH   0x07
#define HFCGML   0x27
#elif (OSC_CLK == 26000000)
#define HFCGMH   0x06
#define HFCGML   0x33
#else
#error "Unsupported OSC_CLK Frequency"
#endif

#if defined(CHIP_FAMILY_NPCX5)
#if (OSC_CLK > 50000000)
#error "Unsupported OSC_CLK on NPCX5 series!"
#endif
#elif NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
#if (OSC_CLK > 100000000)
#error "Unsupported OSC_CLK on NPCX series!"
#endif
#endif

/**
 * Return the current FMUL clock frequency in Hz.
 */
int clock_get_fm_freq(void);

/**
 * Return the current APB1 clock frequency in Hz.
 */
int clock_get_apb1_freq(void);

/**
 * Return the current APB2 clock frequency in Hz.
 */
int clock_get_apb2_freq(void);

/**
 * Return the current APB3 clock frequency in Hz.
 */
int clock_get_apb3_freq(void);

/**
 * Set the CPU clock to maximum freq for better performance.
 */
void clock_turbo(void);

/**
 * Set the CPU clock back to normal freq.
 */
void clock_turbo_disable(void);

#endif /* __CROS_EC_CLOCK_CHIP_H */
