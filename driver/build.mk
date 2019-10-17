# -*- makefile -*-
# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Drivers for off-chip devices
#

# Note that this variable includes the trailing "/"
_driver_cur_dir:=$(dir $(lastword $(MAKEFILE_LIST)))

# Accelerometers
driver-$(CONFIG_ACCEL_BMA255)+=accel_bma2x2.o
driver-$(CONFIG_ACCEL_KXCJ9)+=accel_kionix.o
driver-$(CONFIG_ACCEL_KX022)+=accel_kionix.o
driver-$(CONFIG_ACCELGYRO_LSM6DS0)+=accelgyro_lsm6ds0.o
driver-$(CONFIG_ACCELGYRO_BMI160)+=accelgyro_bmi160.o
driver-$(CONFIG_MAG_BMI160_BMM150)+=mag_bmm150.o
driver-$(CONFIG_ACCELGYRO_LSM6DSM)+=accelgyro_lsm6dsm.o stm_mems_common.o
driver-$(CONFIG_ACCELGYRO_LSM6DSO)+=accelgyro_lsm6dso.o stm_mems_common.o
driver-$(CONFIG_ACCEL_LIS2D_COMMON)+=accel_lis2dh.o stm_mems_common.o
driver-$(CONFIG_MAG_LIS2MDL)+=mag_lis2mdl.o
driver-$(CONFIG_SENSORHUB_LSM6DSM)+=sensorhub_lsm6dsm.o
driver-$(CONFIG_SYNC)+=sync.o
driver-$(CONFIG_ACCEL_LIS2DW_COMMON)+=accel_lis2dw12.o stm_mems_common.o

# BC1.2 Charger Detection Devices
driver-$(CONFIG_BC12_DETECT_MAX14637)+=bc12/max14637.o
driver-$(CONFIG_BC12_DETECT_PI3USB9201)+=bc12/pi3usb9201.o
driver-$(CONFIG_BC12_DETECT_PI3USB9281)+=bc12/pi3usb9281.o

# Gyrometers
driver-$(CONFIG_GYRO_L3GD20H)+=gyro_l3gd20h.o

# ALS drivers
driver-$(CONFIG_ALS_AL3010)+=als_al3010.o
driver-$(CONFIG_ALS_ISL29035)+=als_isl29035.o
driver-$(CONFIG_ALS_OPT3001)+=als_opt3001.o
driver-$(CONFIG_ALS_SI114X)+=als_si114x.o
driver-$(CONFIG_ALS_BH1730)+=als_bh1730.o
driver-$(CONFIG_ALS_TCS3400)+=als_tcs3400.o

# Barometers
driver-$(CONFIG_BARO_BMP280)+=baro_bmp280.o

# Batteries
driver-$(CONFIG_BATTERY_BQ20Z453)+=battery/bq20z453.o
driver-$(CONFIG_BATTERY_BQ27541)+=battery/bq27541.o
driver-$(CONFIG_BATTERY_BQ27621)+=battery/bq27621_g1.o
driver-$(CONFIG_BATTERY_MAX17055)+=battery/max17055.o
driver-$(CONFIG_BATTERY_SMART)+=battery/smart.o
driver-$(CONFIG_BATTERY_BQ4050)+=battery/bq4050.o
driver-$(CONFIG_BATTERY_MM8013)+=battery/mm8013.o

# Battery charger ICs
driver-$(CONFIG_CHARGER_BD9995X)+=charger/bd9995x.o
driver-$(CONFIG_CHARGER_BQ24192)+=charger/bq24192.o
driver-$(CONFIG_CHARGER_BQ24707A)+=charger/bq24707a.o
driver-$(CONFIG_CHARGER_BQ24715)+=charger/bq24715.o
driver-$(CONFIG_CHARGER_BQ24725)+=charger/bq24725.o
driver-$(CONFIG_CHARGER_BQ24735)+=charger/bq24735.o
driver-$(CONFIG_CHARGER_BQ24738)+=charger/bq24738.o
driver-$(CONFIG_CHARGER_BQ24770)+=charger/bq24773.o
driver-$(CONFIG_CHARGER_BQ24773)+=charger/bq24773.o
driver-$(CONFIG_CHARGER_BQ25703)+=charger/bq25703.o
driver-$(CONFIG_CHARGER_BQ25710)+=charger/bq25710.o
driver-$(CONFIG_CHARGER_BQ25890)+=charger/bq2589x.o
driver-$(CONFIG_CHARGER_BQ25892)+=charger/bq2589x.o
driver-$(CONFIG_CHARGER_BQ25895)+=charger/bq2589x.o
driver-$(CONFIG_CHARGER_ISL9237)+=charger/isl923x.o
driver-$(CONFIG_CHARGER_ISL9238)+=charger/isl923x.o
driver-$(CONFIG_CHARGER_ISL9241)+=charger/isl9241.o
driver-$(CONFIG_CHARGER_MT6370)+=charger/rt946x.o
driver-$(CONFIG_CHARGER_RT9466)+=charger/rt946x.o
driver-$(CONFIG_CHARGER_RT9467)+=charger/rt946x.o
driver-$(CONFIG_CHARGER_SY21612)+=charger/sy21612.o

# Fingerprint Sensors
include $(_driver_cur_dir)fingerprint/build.mk

# I/O expander
driver-$(CONFIG_IO_EXPANDER_PCA9534)+=ioexpander_pca9534.o
driver-$(CONFIG_IO_EXPANDER_NCT38XX)+=ioexpander_nct38xx.o


# Current/Power monitor
driver-$(CONFIG_INA219)$(CONFIG_INA231)+=ina2xx.o

# LED drivers
driver-$(CONFIG_LED_DRIVER_DS2413)+=led/ds2413.o
driver-$(CONFIG_LED_DRIVER_LM3509)+=led/lm3509.o
driver-$(CONFIG_LED_DRIVER_LM3630A)+=led/lm3630a.o
driver-$(CONFIG_LED_DRIVER_LP5562)+=led/lp5562.o
driver-$(CONFIG_LED_DRIVER_OZ554)+=led/oz554.o

# 7-segment display
driver-$(CONFIG_MAX695X_SEVEN_SEGMENT_DISPLAY)+=led/max695x.o

# Voltage regulators
driver-$(CONFIG_REGULATOR_IR357X)+=regulator_ir357x.o

# Temperature sensors
driver-$(CONFIG_TEMP_SENSOR_ADT7481)+=temp_sensor/adt7481.o
driver-$(CONFIG_TEMP_SENSOR_BD99992GW)+=temp_sensor/bd99992gw.o
driver-$(CONFIG_TEMP_SENSOR_EC_ADC)+=temp_sensor/ec_adc.o
driver-$(CONFIG_TEMP_SENSOR_F75303)+=temp_sensor/f75303.o
driver-$(CONFIG_TEMP_SENSOR_G753)+=temp_sensor/g753.o
driver-$(CONFIG_TEMP_SENSOR_G781)+=temp_sensor/g78x.o
driver-$(CONFIG_TEMP_SENSOR_G782)+=temp_sensor/g78x.o
driver-$(CONFIG_TEMP_SENSOR_SB_TSI)+=temp_sensor/sb_tsi.o
driver-$(CONFIG_TEMP_SENSOR_TMP006)+=temp_sensor/tmp006.o
driver-$(CONFIG_TEMP_SENSOR_TMP112)+=temp_sensor/tmp112.o
driver-$(CONFIG_TEMP_SENSOR_TMP411)+=temp_sensor/tmp411.o
driver-$(CONFIG_TEMP_SENSOR_TMP432)+=temp_sensor/tmp432.o
driver-$(CONFIG_TEMP_SENSOR_TMP468)+=temp_sensor/tmp468.o

# Touchpads
driver-$(CONFIG_TOUCHPAD_GT7288)+=touchpad_gt7288.o
driver-$(CONFIG_TOUCHPAD_ELAN)+=touchpad_elan.o
driver-$(CONFIG_TOUCHPAD_ST)+=touchpad_st.o

# Thermistors
driver-$(CONFIG_THERMISTOR)+=temp_sensor/thermistor.o
driver-$(CONFIG_THERMISTOR_NCP15WB)+=temp_sensor/thermistor_ncp15wb.o

# Type-C port controller (TCPC) drivers
driver-$(CONFIG_USB_PD_TCPM_STUB)+=tcpm/stub.o
driver-$(CONFIG_USB_PD_TCPM_TCPCI)+=tcpm/tcpci.o
driver-$(CONFIG_USB_PD_TCPM_FUSB302)+=tcpm/fusb302.o
driver-$(CONFIG_USB_PD_TCPM_MT6370)+=tcpm/mt6370.o
driver-$(CONFIG_USB_PD_TCPM_ITE83XX)+=tcpm/it83xx.o
driver-$(CONFIG_USB_PD_TCPM_ANX74XX)+=tcpm/anx74xx.o
driver-$(CONFIG_USB_PD_TCPM_ANX7688)+=tcpm/anx7688.o
driver-$(CONFIG_USB_PD_TCPM_ANX7447)+=tcpm/anx7447.o
driver-$(CONFIG_USB_PD_TCPM_PS8751)+=tcpm/ps8xxx.o
driver-$(CONFIG_USB_PD_TCPM_PS8805)+=tcpm/ps8xxx.o
driver-$(CONFIG_USB_PD_TCPM_TUSB422)+=tcpm/tusb422.o
driver-$(CONFIG_USB_PD_TCPM_NCT38XX)+=tcpm/nct38xx.o

# Type-C Retimer drivers
driver-$(CONFIG_USB_PD_RETIMER_INTEL_BB)+=retimer/bb_retimer.o

# USB mux high-level driver
driver-$(CONFIG_USBC_SS_MUX)+=usb_mux/usb_mux.o

# USB muxes
driver-$(CONFIG_USB_MUX_AMD_FP5)+=usb_mux/amd_fp5.o
driver-$(CONFIG_USB_MUX_IT5205)+=usb_mux/it5205.o
driver-$(CONFIG_USB_MUX_PI3USB30532)+=usb_mux/pi3usb30532.o
driver-$(CONFIG_USB_MUX_PS8740)+=usb_mux/ps874x.o
driver-$(CONFIG_USB_MUX_PS8743)+=usb_mux/ps874x.o
driver-$(CONFIG_USB_MUX_VIRTUAL)+=usb_mux/virtual.o

# Type-C Power Path Controllers (PPC)
driver-$(CONFIG_USBC_PPC_AOZ1380)+=ppc/aoz1380.o
driver-$(CONFIG_USBC_PPC_SN5S330)+=ppc/sn5s330.o
ifeq ($(CONFIG_USBC_PPC_NX20P3481)$(CONFIG_USBC_PPC_NX20P3483),y)
driver-y += ppc/nx20p348x.o
endif
driver-$(CONFIG_USBC_PPC_SYV682X)+=ppc/syv682x.o
driver-$(CONFIG_USBC_PPC_NX20P3483)+=ppc/nx20p348x.o

# video converters
driver-$(CONFIG_MCDP28X0)+=mcdp28x0.o

# Wireless Power Chargers
driver-$(HAS_TASK_WPC) += wpc/p9221.o
