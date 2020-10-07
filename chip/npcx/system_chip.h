/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific SIB module for Chrome EC */

#ifndef __CROS_EC_SYSTEM_CHIP_H
#define __CROS_EC_SYSTEM_CHIP_H

/* Flags for BBRM_DATA_INDEX_WAKE */
#define HIBERNATE_WAKE_MTC        BIT(0)  /* MTC alarm */
#define HIBERNATE_WAKE_PIN        BIT(1)  /* Wake pin */
#define HIBERNATE_WAKE_LCT        BIT(2)  /* LCT alarm */
/*
 * Indicate that EC enters hibernation via PSL. When EC wakes up from
 * hibernation and this flag is set, it will check the related status bit to
 * know the actual wake up source. (From LCT or physical wakeup pins)
 */
#define HIBERNATE_WAKE_PSL        BIT(3)

/* Indices for battery-backed ram (BBRAM) data position */
enum bbram_data_index {
	BBRM_DATA_INDEX_SCRATCHPAD = 0,        /* General-purpose scratchpad */
	BBRM_DATA_INDEX_SAVED_RESET_FLAGS = 4, /* Saved reset flags */
	BBRM_DATA_INDEX_WAKE = 8,	       /* Wake reasons for hibernate */
	BBRM_DATA_INDEX_PD0 = 12,	       /* USB-PD saved port0 state */
	BBRM_DATA_INDEX_PD1 = 13,	       /* USB-PD saved port1 state */
	BBRM_DATA_INDEX_TRY_SLOT = 14,         /* Vboot EC try slot */
	BBRM_DATA_INDEX_PD2 = 15,	       /* USB-PD saved port2 state */
	BBRM_DATA_INDEX_VBNVCNTXT = 16,	       /* VbNvContext for ARM arch */
	BBRM_DATA_INDEX_RAMLOG = 32,	       /* RAM log for Booter */
	BBRM_DATA_INDEX_PANIC_FLAGS = 35,      /* Flag to indicate validity of
						* panic data starting at index
						* 36.
						*/
	BBRM_DATA_INDEX_PANIC_BKUP = 36,       /* Panic data (index 35-63)*/
};

enum psl_pin_t {
	PSL_IN1,
	PSL_IN2,
	PSL_IN3,
	PSL_IN4,
	PSL_NONE,
};

/* Issue a watchdog reset */
void system_watchdog_reset(void);

/* Stops the watchdog timer and unlocks configuration. */
void watchdog_stop_and_unlock(void);

/*
 * Configure the specific memory addresses in the the MPU
 * (Memory Protection Unit) for Nuvoton different chip series.
 */
void system_mpu_config(void);

/* Hibernate function for different Nuvoton chip series. */
void __hibernate_npcx_series(void);

/* Check and clear BBRAM status on power-on reset */
void system_check_bbram_on_reset(void);

/* The utilities and variables depend on npcx chip family */
#if defined(CHIP_FAMILY_NPCX5) || defined(CONFIG_WORKAROUND_FLASH_DOWNLOAD_API)
/* Bypass for GMDA issue of ROM api utilities only on npcx5 series */
void system_download_from_flash(uint32_t srcAddr, uint32_t dstAddr,
		uint32_t size, uint32_t exeAddr);

/* Begin address for hibernate utility; defined in linker script */
extern unsigned int __flash_lpfw_start;

/* End address for hibernate utility; defined in linker script */
extern unsigned int __flash_lpfw_end;

/* Begin address for little FW; defined in linker script */
extern unsigned int __flash_lplfw_start;

/* End address for little FW; defined in linker script */
extern unsigned int __flash_lplfw_end;
#endif

#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
/* Configure PSL mode setting for the wake-up pins. */
int system_config_psl_mode(enum gpio_signal signal);

/* Configure PSL pins and enter PSL mode. */
void system_enter_psl_mode(void);

/* End address for hibernate utility; defined in linker script */
extern unsigned int __after_init_end;

#endif

#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX9
void system_set_psl_gpo(int level);
#endif

#endif /* __CROS_EC_SYSTEM_CHIP_H */
