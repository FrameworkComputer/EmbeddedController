# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Common files build
#

common-y=main.o util.o console_output.o uart_buffering.o
common-y+=memory_commands.o shared_mem.o system.o hooks.o
common-y+=gpio.o version.o printf.o queue.o

common-$(CONFIG_ADC)+=adc.o
common-$(CONFIG_ALS)+=als.o
common-$(CONFIG_AP_HANG_DETECT)+=ap_hang_detect.o
common-$(CONFIG_BACKLIGHT_LID)+=backlight_lid.o
# TODO(crosbug.com/p/23821): Why do these include battery_common but
# the other batteries don't?  Perhaps should use CONFIG_CMD_BATTERY
# instead, since all that's in battery.c is the battery console
# command?
common-$(CONFIG_BATTERY_BQ27541)+=battery.o
common-$(CONFIG_BATTERY_SMART)+=battery.o
common-$(CONFIG_BUTTON_COUNT)+=button.o
common-$(CONFIG_CHARGER)+=charge_state.o charger.o
# TODO(crosbug.com/p/23815): This is really the charge state machine
# for ARM, not the charger driver for the tps65090.  Rename.
common-$(CONFIG_CHARGER_TPS65090)+=pmu_tps65090_charger.o
common-$(CONFIG_COMMON_PANIC_OUTPUT)+=panic_output.o
common-$(CONFIG_COMMON_TIMER)+=timer.o
common-$(CONFIG_PMU_POWERINFO)+=pmu_tps65090_powerinfo.o
common-$(CONFIG_PMU_TPS65090)+=pmu_tps65090.o
common-$(CONFIG_EOPTION)+=eoption.o
common-$(CONFIG_EXTPOWER_FALCO)+=extpower_falco.o
common-$(CONFIG_EXTPOWER_GPIO)+=extpower_gpio.o
common-$(CONFIG_EXTPOWER_SNOW)+=extpower_snow.o
common-$(CONFIG_EXTPOWER_SPRING)+=extpower_spring.o
common-$(CONFIG_FANS)+=fan.o
common-$(CONFIG_FLASH)+=flash.o
common-$(CONFIG_FMAP)+=fmap.o
common-$(CONFIG_I2C)+=i2c.o
common-$(CONFIG_I2C_ARBITRATION)+=i2c_arbitration.o
common-$(CONFIG_KEYBOARD_PROTOCOL_8042)+=keyboard_8042.o
common-$(CONFIG_KEYBOARD_PROTOCOL_MKBP)+=keyboard_mkbp.o
common-$(CONFIG_KEYBOARD_TEST)+=keyboard_test.o
common-$(CONFIG_LED_COMMON)+=led_common.o
common-$(CONFIG_LID_SWITCH)+=lid_switch.o
common-$(CONFIG_LPC)+=port80.o
common-$(CONFIG_ONEWIRE)+=onewire.o
common-$(CONFIG_POWER_BUTTON)+=power_button.o
common-$(CONFIG_POWER_BUTTON_X86)+=power_button_x86.o
common-$(CONFIG_PSTORE)+=pstore_commands.o
common-$(CONFIG_PWM)+=pwm.o
common-$(CONFIG_PWM_KBLIGHT)+=pwm_kblight.o
common-$(CONFIG_SWITCH)+=switch.o
common-$(CONFIG_TEMP_SENSOR)+=temp_sensor.o thermal.o
common-$(CONFIG_USB_PORT_POWER_DUMB)+=usb_port_power_dumb.o
common-$(CONFIG_USB_PORT_POWER_SMART)+=usb_port_power_smart.o
common-$(CONFIG_VBOOT_HASH)+=sha256.o vboot_hash.o
common-$(CONFIG_WIRELESS)+=wireless.o
common-$(HAS_TASK_CHIPSET)+=chipset.o throttle_ap.o
common-$(HAS_TASK_CONSOLE)+=console.o
common-$(HAS_TASK_HOSTCMD)+=acpi.o host_command.o host_event_commands.o
common-$(HAS_TASK_KEYSCAN)+=keyboard_scan.o
common-$(HAS_TASK_LIGHTBAR)+=lightbar.o
common-$(TEST_BUILD)+=test_util.o
