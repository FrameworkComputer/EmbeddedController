/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Symbols from linker definitions
 */

#ifndef __CROS_EC_LINK_DEFS_H
#define __CROS_EC_LINK_DEFS_H

#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "task.h"
#include "test_util.h"

#ifdef CONFIG_ZEPHYR
#include <linker.h>
#endif

/* Console commands */
extern const struct console_command __cmds[];
extern const struct console_command __cmds_end[];

/* Extension commands. */
extern const void *__extension_cmds;
extern const void *__extension_cmds_end;

/* Hooks */
extern const struct hook_data __hooks_init[];
extern const struct hook_data __hooks_init_end[];
extern const struct hook_data __hooks_init_early[];
extern const struct hook_data __hooks_init_early_end[];
extern const struct hook_data __hooks_pre_freq_change[];
extern const struct hook_data __hooks_pre_freq_change_end[];
extern const struct hook_data __hooks_freq_change[];
extern const struct hook_data __hooks_freq_change_end[];
extern const struct hook_data __hooks_sysjump[];
extern const struct hook_data __hooks_sysjump_end[];
extern const struct hook_data __hooks_chipset_pre_init[];
extern const struct hook_data __hooks_chipset_pre_init_end[];
extern const struct hook_data __hooks_chipset_startup[];
extern const struct hook_data __hooks_chipset_startup_end[];
extern const struct hook_data __hooks_chipset_resume[];
extern const struct hook_data __hooks_chipset_resume_end[];
extern const struct hook_data __hooks_chipset_suspend[];
extern const struct hook_data __hooks_chipset_suspend_end[];
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
extern const struct hook_data __hooks_chipset_resume_init[];
extern const struct hook_data __hooks_chipset_resume_init_end[];
extern const struct hook_data __hooks_chipset_suspend_complete[];
extern const struct hook_data __hooks_chipset_suspend_complete_end[];
#endif
extern const struct hook_data __hooks_chipset_shutdown[];
extern const struct hook_data __hooks_chipset_shutdown_end[];
extern const struct hook_data __hooks_chipset_shutdown_complete[];
extern const struct hook_data __hooks_chipset_shutdown_complete_end[];
extern const struct hook_data __hooks_chipset_hard_off[];
extern const struct hook_data __hooks_chipset_hard_off_end[];
extern const struct hook_data __hooks_chipset_reset[];
extern const struct hook_data __hooks_chipset_reset_end[];
extern const struct hook_data __hooks_ac_change[];
extern const struct hook_data __hooks_ac_change_end[];
extern const struct hook_data __hooks_lid_change[];
extern const struct hook_data __hooks_lid_change_end[];
extern const struct hook_data __hooks_tablet_mode_change[];
extern const struct hook_data __hooks_tablet_mode_change_end[];
#ifdef CONFIG_BODY_DETECTION
extern const struct hook_data __hooks_body_detect_change[];
extern const struct hook_data __hooks_body_detect_change_end[];
#endif
extern const struct hook_data __hooks_base_attached_change[];
extern const struct hook_data __hooks_base_attached_change_end[];
extern const struct hook_data __hooks_pwrbtn_change[];
extern const struct hook_data __hooks_pwrbtn_change_end[];
extern const struct hook_data __hooks_battery_soc_change[];
extern const struct hook_data __hooks_battery_soc_change_end[];
#ifdef CONFIG_USB_SUSPEND
extern const struct hook_data __hooks_usb_change[];
extern const struct hook_data __hooks_usb_change_end[];
#endif
extern const struct hook_data __hooks_tick[];
extern const struct hook_data __hooks_tick_end[];
extern const struct hook_data __hooks_second[];
extern const struct hook_data __hooks_second_end[];
extern const struct hook_data __hooks_usb_pd_disconnect[];
extern const struct hook_data __hooks_usb_pd_disconnect_end[];
extern const struct hook_data __hooks_usb_pd_connect[];
extern const struct hook_data __hooks_usb_pd_connect_end[];
extern const struct hook_data __hooks_power_supply_change[];
extern const struct hook_data __hooks_power_supply_change_end[];

/* Deferrable functions and firing times*/
extern const struct deferred_data __deferred_funcs[];
extern const struct deferred_data __deferred_funcs_end[];
extern uint64_t __deferred_until[];
extern uint64_t __deferred_until_end[];

/* I2C fake devices for unit testing */
extern const struct test_i2c_xfer __test_i2c_xfer[];
extern const struct test_i2c_xfer __test_i2c_xfer_end[];

/* Host commands */
extern const struct host_command __hcmds[];
extern const struct host_command __hcmds_end[];

/* MKBP events */
extern const struct mkbp_event_source __mkbp_evt_srcs[];
extern const struct mkbp_event_source __mkbp_evt_srcs_end[];

/* IRQs (interrupt handlers) */
extern const struct irq_priority __irqprio[];
extern const struct irq_priority __irqprio_end[];
extern const void *__irqhandler[];
extern const struct irq_def __irq_data[], __irq_data_end[];

/* Shared memory buffer.  Use via shared_mem.h interface. */
extern char __shared_mem_buf[];

/* Image sections used by the TPM2 library */
extern uint8_t *__bss_libtpm2_start;
extern uint8_t *__bss_libtpm2_end;
extern uint8_t *__data_libtpm2_start;
extern uint8_t *__data_libtpm2_end;

/* Image sections. */
extern const void *__data_lma_start;
extern const void *__data_start;
extern const void *__data_end;

/* DRAM image sections. */
extern const void *__dram_data_lma_start;
extern void *__dram_data_start;
extern void *__dram_data_end;
extern void *__dram_bss_start;
extern void *__dram_bss_end;

/* Helper for special chip-specific memory sections */
#if defined(CONFIG_CHIP_MEMORY_REGIONS) || defined(CONFIG_DRAM_BASE)
#define __SECTION(name) __attribute__((section("." STRINGIFY(name) ".50_auto")))
#define __SECTION_KEEP(name) \
	__keep __attribute__((section("." STRINGIFY(name) ".keep.50_auto")))
#else
#define __SECTION(name)
#define __SECTION_KEEP(name)
#endif /* CONFIG_MEMORY_REGIONS */
#ifdef CONFIG_CHIP_UNCACHED_REGION
#define __uncached __SECTION(CONFIG_CHIP_UNCACHED_REGION)
#else
#define __uncached
#endif

#endif /* __CROS_EC_LINK_DEFS_H */

#ifdef CONFIG_PRESERVE_LOGS
#define __preserved_logs(name) \
	__attribute__((section(".preserved_logs." STRINGIFY(name))))
/* preserved_logs section. */
extern const char __preserved_logs_start[];
extern const char __preserved_logs_size[];
#else
#define __preserved_logs(name)
#endif
