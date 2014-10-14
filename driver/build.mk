# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Drivers for off-chip devices
#

# Accelerometers
driver-$(CONFIG_ACCEL_KXCJ9)+=accel_kxcj9.o
driver-$(CONFIG_ACCELGYRO_LSM6DS0)+=accelgyro_lsm6ds0.o

# ALS drivers
driver-$(CONFIG_ALS_ISL29035)+=als_isl29035.o

# Batteries
driver-$(CONFIG_BATTERY_BQ20Z453)+=battery/bq20z453.o
driver-$(CONFIG_BATTERY_BQ27541)+=battery/bq27541.o
driver-$(CONFIG_BATTERY_LINK)+=battery/link.o
driver-$(CONFIG_BATTERY_SAMUS)+=battery/samus.o
driver-$(CONFIG_BATTERY_SMART)+=battery/smart.o

# Battery charger ICs
driver-$(CONFIG_CHARGER_BQ24192)+=charger/bq24192.o
driver-$(CONFIG_CHARGER_BQ24707A)+=charger/bq24707a.o
driver-$(CONFIG_CHARGER_BQ24715)+=charger/bq24715.o
driver-$(CONFIG_CHARGER_BQ24725)+=charger/bq24725.o
driver-$(CONFIG_CHARGER_BQ24735)+=charger/bq24735.o
driver-$(CONFIG_CHARGER_BQ24738)+=charger/bq24738.o
driver-$(CONFIG_CHARGER_BQ24773)+=charger/bq24773.o

# I/O expander
driver-$(CONFIG_IO_EXPANDER_PCA9534)+=ioexpander_pca9534.o

# Current/Power monitor
driver-$(CONFIG_INA219)$(CONFIG_INA231)+=ina2xx.o

# LED drivers
driver-$(CONFIG_LED_DRIVER_DS2413)+=led/ds2413.o
driver-$(CONFIG_LED_DRIVER_LP5562)+=led/lp5562.o

# Voltage regulators
driver-$(CONFIG_REGULATOR_IR357X)+=regulator_ir357x.o

# Temperature sensors
driver-$(CONFIG_TEMP_SENSOR_G781)+=temp_sensor/g781.o
driver-$(CONFIG_TEMP_SENSOR_TMP006)+=temp_sensor/tmp006.o
driver-$(CONFIG_TEMP_SENSOR_TMP432)+=temp_sensor/tmp432.o

# USB switches
driver-$(CONFIG_USB_SWITCH_PI3USB9281)+=usb_switch_pi3usb9281.o
driver-$(CONFIG_USB_SWITCH_TSU6721)+=usb_switch_tsu6721.o

# Firmware Update
driver-$(CONFIG_SB_FIRMWARE_UPDATE)+=battery/sb_fw_update.o
