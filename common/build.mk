# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Common files build
#

common-y=main.o util.o console_output.o uart_buffering.o
common-y+=memory_commands.o shared_mem.o system_common.o hooks.o
common-y+=gpio_common.o version.o printf.o queue.o
common-$(CONFIG_BATTERY_BQ20Z453)+=battery_bq20z453.o
common-$(CONFIG_BATTERY_LINK)+=battery_link.o charge_state.o battery_precharge.o
common-$(CONFIG_CHARGER_BQ24725)+=charger_bq24725.o
common-$(CONFIG_CHARGER_TPS65090)+=pmu_tps65090_charger.o
common-$(CONFIG_CHIPSET_GAIA)+=gaia_power.o
common-$(CONFIG_CHIPSET_X86)+=x86_power.o
common-$(CONFIG_PMU_TPS65090)+=pmu_tps65090.o
common-$(CONFIG_EOPTION)+=eoption.o
common-$(CONFIG_EXTPOWER_GPIO)+=extpower_gpio.o
common-$(CONFIG_EXTPOWER_SNOW)+=extpower_snow.o
common-$(CONFIG_EXTPOWER_USB)+=extpower_usb.o
common-$(CONFIG_FLASH)+=flash_common.o fmap.o
common-$(CONFIG_I2C)+=i2c_common.o
common-$(CONFIG_I2C_ARBITRATION)+=i2c_arbitration.o
common-$(CONFIG_IR357x)+=ir357x.o
common-$(CONFIG_KEYBOARD_PROTOCOL_8042)+=keyboard_8042.o
common-$(CONFIG_KEYBOARD_PROTOCOL_MKBP)+=keyboard_mkbp.o
common-$(CONFIG_KEYBOARD_TEST)+=keyboard_test.o
common-$(CONFIG_LID_SWITCH)+=lid_switch.o
common-$(CONFIG_LP5562)+=lp5562.o lp5562_battery_led.o
common-$(CONFIG_LPC)+=port80.o
common-$(CONFIG_ONEWIRE_LED)+=onewire_led.o
common-$(CONFIG_POWER_BUTTON)+=power_button.o
common-$(CONFIG_PSTORE)+=pstore_commands.o
common-$(CONFIG_SMART_BATTERY)+=smart_battery.o smart_battery_stub.o
common-$(HAS_TASK_CHIPSET)+=chipset.o
common-$(HAS_TASK_CONSOLE)+=console.o
common-$(HAS_TASK_HOSTCMD)+=host_command.o host_event_commands.o
common-$(HAS_TASK_KEYSCAN)+=keyboard_scan.o
common-$(HAS_TASK_LIGHTBAR)+=lightbar.o
common-$(HAS_TASK_THERMAL)+=thermal.o
common-$(HAS_TASK_VBOOTHASH)+=sha256.o vboot_hash.o
common-$(CONFIG_TEMP_SENSOR)+=temp_sensor.o
common-$(CONFIG_TMP006)+=tmp006.o
common-$(CONFIG_TSU6721)+=tsu6721.o
common-$(CONFIG_USB_CHARGE)+=usb_charge.o
