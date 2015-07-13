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
driver-$(CONFIG_ACCELGYRO_BMI160)+=accelgyro_bmi160.o
driver-$(CONFIG_MAG_BMI160_BMM150)+=mag_bmm150.o

# ALS drivers
driver-$(CONFIG_ALS_ISL29035)+=als_isl29035.o
driver-$(CONFIG_ALS_OPT3001)+=als_opt3001.o

# Batteries
driver-$(CONFIG_BATTERY_BQ20Z453)+=battery/bq20z453.o
driver-$(CONFIG_BATTERY_BQ27541)+=battery/bq27541.o
driver-$(CONFIG_BATTERY_BQ27621)+=battery/bq27621_g1.o
driver-$(CONFIG_BATTERY_RYU)+=battery/ryu.o
driver-$(CONFIG_BATTERY_SAMUS)+=battery/samus.o
driver-$(CONFIG_BATTERY_SMART)+=battery/smart.o

# Battery charger ICs
driver-$(CONFIG_CHARGER_BQ24192)+=charger/bq24192.o
driver-$(CONFIG_CHARGER_BQ24707A)+=charger/bq24707a.o
driver-$(CONFIG_CHARGER_BQ24715)+=charger/bq24715.o
driver-$(CONFIG_CHARGER_BQ24725)+=charger/bq24725.o
driver-$(CONFIG_CHARGER_BQ24735)+=charger/bq24735.o
driver-$(CONFIG_CHARGER_BQ24738)+=charger/bq24738.o
driver-$(CONFIG_CHARGER_BQ24770)+=charger/bq24773.o
driver-$(CONFIG_CHARGER_BQ24773)+=charger/bq24773.o
driver-$(CONFIG_CHARGER_BQ25890)+=charger/bq2589x.o
driver-$(CONFIG_CHARGER_BQ25892)+=charger/bq2589x.o
driver-$(CONFIG_CHARGER_BQ25895)+=charger/bq2589x.o
driver-$(CONFIG_CHARGER_ISL9237)+=charger/isl9237.o

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

# Type-C port controller (TCPC) drivers
driver-$(CONFIG_USB_PD_TCPM_STUB)+=tcpm/stub.o
driver-$(CONFIG_USB_PD_TCPM_TCPCI)+=tcpm/tcpci.o

# USB switches
driver-$(CONFIG_USB_SWITCH_PI3USB9281)+=usb_switch_pi3usb9281.o
driver-$(CONFIG_USB_SWITCH_TSU6721)+=usb_switch_tsu6721.o

# USB mux high-level driver
driver-$(CONFIG_USBC_SS_MUX)+=usb_mux.o

# USB muxes
driver-$(CONFIG_USB_MUX_PI3USB30532)+=usb_mux_pi3usb30532.o
driver-$(CONFIG_USB_MUX_PS8740)+=usb_mux_ps8740.o

# Firmware Update
driver-$(CONFIG_SB_FIRMWARE_UPDATE)+=battery/sb_fw_update.o

# video converters
driver-$(CONFIG_MCDP28X0)+=mcdp28x0.o
