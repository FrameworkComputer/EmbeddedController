# -*- makefile -*-
# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Common files build
#

common-y=main.o util.o console_output.o uart_buffering.o
common-y+=memory_commands.o shared_mem.o system_common.o hooks.o
common-y+=gpio_commands.o version.o printf.o queue.o
common-$(CONFIG_BATTERY_LINK)+=battery_link.o
common-$(CONFIG_CHARGER_BQ24725)+=charger_bq24725.o
common-$(CONFIG_PMU_TPS65090)+=pmu_tps65090.o pmu_tps65090_charger.o
common-$(CONFIG_EOPTION)+=eoption.o
common-$(CONFIG_FLASH)+=flash_common.o fmap.o
common-$(CONFIG_I2C)+=i2c_commands.o
common-$(CONFIG_IR357x)+=ir357x.o
common-$(CONFIG_LPC)+=port80.o
common-$(CONFIG_POWER_LED)+=power_led.o
common-$(CONFIG_PSTORE)+=pstore_commands.o
common-$(CONFIG_SMART_BATTERY)+=smart_battery.o smart_battery_stub.o
common-$(CONFIG_TASK_CONSOLE)+=console.o
common-$(CONFIG_TASK_GAIAPOWER)+=gaia_power.o
common-$(CONFIG_TASK_HOSTCMD)+=host_command.o host_event_commands.o
common-$(CONFIG_TASK_I8042CMD)+=i8042.o keyboard.o
common-$(CONFIG_TASK_LIGHTBAR)+=lightbar.o
common-$(CONFIG_TASK_POWERSTATE)+=charge_state.o battery_precharge.o
common-$(CONFIG_TASK_PWM)+=pwm_commands.o
common-$(CONFIG_TASK_TEMPSENSOR)+=temp_sensor.o temp_sensor_commands.o
common-$(CONFIG_TASK_THERMAL)+=thermal.o thermal_commands.o
common-$(CONFIG_TASK_X86POWER)+=x86_power.o
common-$(CONFIG_TMP006)+=tmp006.o
common-$(CONFIG_USB_CHARGE)+=usb_charge.o usb_charge_commands.o

# verified boot stuff
VBOOT_SOURCE?=/usr/src/vboot
VBOOT_DEVKEYS?=/usr/share/vboot/devkeys

CFLAGS_$(CONFIG_VBOOT)+= -DCHROMEOS_ENVIRONMENT -DCHROMEOS_EC
# CFLAGS_$(CONFIG_VBOOT)+= -DVBOOT_DEBUG

common-$(CONFIG_VBOOT)+=vboot_stub.o vboot_hash.o

includes-$(CONFIG_VBOOT)+= \
	$(VBOOT_SOURCE)/include \
	$(VBOOT_SOURCE)/lib/include \
	$(VBOOT_SOURCE)/lib/cryptolib/include

dirs-$(CONFIG_VBOOT)+=vboot/lib vboot/lib/cryptolib

vboot-$(CONFIG_VBOOT)+= \
	lib/cryptolib/padding.o \
	lib/cryptolib/sha256.o
