/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
SECTIONS {
  .rodata.ec_sections : {
    /* Symbols defined here are declared in link_defs.h */
    __irqprio = .;
    *(.rodata.irqprio)
    __irqprio_end = .;

    . = ALIGN(8);
    __cmds = .;
    *(SORT(.rodata.cmds*))
    __cmds_end = .;

    . = ALIGN(8);
    __hcmds = .;
    *(.rodata.hcmds)
    __hcmds_end = .;

    . = ALIGN(8);
    __hooks_init = .;
    *(.rodata.HOOK_INIT)
    __hooks_init_end = .;

    __hooks_pre_freq_change = .;
    *(.rodata.HOOK_PRE_FREQ_CHANGE)
    __hooks_pre_freq_change_end = .;

    __hooks_freq_change = .;
    *(.rodata.HOOK_FREQ_CHANGE)
    __hooks_freq_change_end = .;

    __hooks_sysjump = .;
    *(.rodata.HOOK_SYSJUMP)
    __hooks_sysjump_end = .;

    __hooks_chipset_pre_init = .;
    *(.rodata.HOOK_CHIPSET_PRE_INIT)
    __hooks_chipset_pre_init_end = .;

    __hooks_chipset_startup = .;
    *(.rodata.HOOK_CHIPSET_STARTUP)
    __hooks_chipset_startup_end = .;

    __hooks_chipset_resume = .;
    *(.rodata.HOOK_CHIPSET_RESUME)
    __hooks_chipset_resume_end = .;

    __hooks_chipset_suspend = .;
    *(.rodata.HOOK_CHIPSET_SUSPEND)
    __hooks_chipset_suspend_end = .;

    __hooks_chipset_shutdown = .;
    *(.rodata.HOOK_CHIPSET_SHUTDOWN)
    __hooks_chipset_shutdown_end = .;

    __hooks_chipset_reset = .;
    *(.rodata.HOOK_CHIPSET_RESET)
    __hooks_chipset_reset_end = .;

    __hooks_ac_change = .;
    *(.rodata.HOOK_AC_CHANGE)
    __hooks_ac_change_end = .;

    __hooks_lid_change = .;
    *(.rodata.HOOK_LID_CHANGE)
    __hooks_lid_change_end = .;

    __hooks_pwrbtn_change = .;
    *(.rodata.HOOK_POWER_BUTTON_CHANGE)
    __hooks_pwrbtn_change_end = .;

    __hooks_charge_state_change = .;
    *(.rodata.HOOK_CHARGE_STATE_CHANGE)
    __hooks_charge_state_change_end = .;

    __hooks_battery_soc_change = .;
    *(.rodata.HOOK_BATTERY_SOC_CHANGE)
    __hooks_battery_soc_change_end = .;

    __hooks_tick = .;
    *(.rodata.HOOK_TICK)
    __hooks_tick_end = .;

    __hooks_second = .;
    *(.rodata.HOOK_SECOND)
    __hooks_second_end = .;

    __deferred_funcs = .;
    *(.rodata.deferred)
    __deferred_funcs_end = .;

    __test_i2c_xfer = .;
    *(.rodata.test_i2c.xfer)
    __test_i2c_xfer_end = .;
  }
}
INSERT BEFORE .rodata;
