# -*- makefile -*-
# Copyright 2014 The ChromiumOS Authors
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
driver-$(CONFIG_ACCELGYRO_BMI160)+=accelgyro_bmi160.o accelgyro_bmi_common.o
driver-$(CONFIG_ACCELGYRO_BMI220)+=accelgyro_bmi260.o accelgyro_bmi_common.o
driver-$(CONFIG_ACCELGYRO_BMI260)+=accelgyro_bmi260.o accelgyro_bmi_common.o
driver-$(CONFIG_ACCELGYRO_BMI3XX)+=accelgyro_bmi3xx.o accelgyro_bmi_common.o
driver-$(CONFIG_ACCEL_BMA4XX)+=accel_bma4xx.o
driver-$(CONFIG_MAG_BMI_BMM150)+=mag_bmm150.o
driver-$(CONFIG_ACCELGYRO_LSM6DSM)+=accelgyro_lsm6dsm.o stm_mems_common.o
driver-$(CONFIG_ACCELGYRO_LSM6DSO)+=accelgyro_lsm6dso.o stm_mems_common.o
driver-$(CONFIG_ACCEL_LIS2D_COMMON)+=accel_lis2dh.o stm_mems_common.o
driver-$(CONFIG_MAG_LIS2MDL)+=mag_lis2mdl.o
driver-$(CONFIG_SENSORHUB_LSM6DSM)+=sensorhub_lsm6dsm.o
driver-$(CONFIG_SYNC)+=sync.o
driver-$(CONFIG_ACCEL_LIS2DW_COMMON)+=accel_lis2dw12.o stm_mems_common.o
driver-$(CONFIG_ACCEL_LIS2DS)+=accel_lis2ds.o stm_mems_common.o
driver-$(CONFIG_ACCELGYRO_ICM426XX)+=accelgyro_icm426xx.o accelgyro_icm_common.o
driver-$(CONFIG_ACCELGYRO_ICM42607)+=accelgyro_icm42607.o accelgyro_icm_common.o

# BC1.2 Charger Detection Devices
driver-$(CONFIG_BC12_DETECT_MAX14637)+=bc12/max14637.o
driver-$(CONFIG_BC12_DETECT_MT6360)+=bc12/mt6360.o
driver-$(CONFIG_BC12_DETECT_PI3USB9201)+=bc12/pi3usb9201.o
driver-$(CONFIG_BC12_DETECT_PI3USB9281)+=bc12/pi3usb9281.o
driver-$(CONFIG_BC12_DETECT_RT1718S)+=bc12/rt1718s.o

# Gyrometers
driver-$(CONFIG_GYRO_L3GD20H)+=gyro_l3gd20h.o

# ALS drivers
driver-$(CONFIG_ALS_AL3010)+=als_al3010.o
driver-$(CONFIG_ALS_ISL29035)+=als_isl29035.o
driver-$(CONFIG_ALS_OPT3001)+=als_opt3001.o
driver-$(CONFIG_ALS_SI114X)+=als_si114x.o
driver-$(CONFIG_ALS_BH1730)+=als_bh1730.o
driver-$(CONFIG_ALS_TCS3400)+=als_tcs3400.o
driver-$(CONFIG_ALS_CM32183)+=als_cm32183.o

# Barometers
driver-$(CONFIG_BARO_BMP280)+=baro_bmp280.o

# Batteries
driver-$(CONFIG_BATTERY_BQ20Z453)+=battery/bq20z453.o
driver-$(CONFIG_BATTERY_BQ27541)+=battery/bq27541.o
driver-$(CONFIG_BATTERY_BQ27621)+=battery/bq27621_g1.o
driver-$(CONFIG_BATTERY_MAX17055)+=battery/max17055.o
ifeq ($(HAS_MOCK_BATTERY),)
driver-$(CONFIG_BATTERY_SMART)+=battery/smart.o
endif
driver-$(CONFIG_BATTERY_BQ4050)+=battery/bq4050.o
driver-$(CONFIG_BATTERY_MM8013)+=battery/mm8013.o

# Battery charger ICs
driver-$(CONFIG_CHARGER_BD9995X)+=charger/bd9995x.o
driver-$(CONFIG_CHARGER_BQ24715)+=charger/bq24715.o
driver-$(CONFIG_CHARGER_BQ24770)+=charger/bq24773.o
driver-$(CONFIG_CHARGER_BQ24773)+=charger/bq24773.o
driver-$(CONFIG_CHARGER_BQ25710)+=charger/bq25710.o
driver-$(CONFIG_CHARGER_BQ25720)+=charger/bq25710.o
driver-$(CONFIG_CHARGER_ISL9237)+=charger/isl923x.o
driver-$(CONFIG_CHARGER_ISL9238)+=charger/isl923x.o
driver-$(CONFIG_CHARGER_ISL9238C)+=charger/isl923x.o
driver-$(CONFIG_CHARGER_ISL9241)+=charger/isl9241.o
driver-$(CONFIG_CHARGER_MT6370)+=charger/rt946x.o
driver-$(CONFIG_CHARGER_RAA489000)+=charger/isl923x.o
driver-$(CONFIG_CHARGER_RAA489110)+=charger/isl9241.o
driver-$(CONFIG_CHARGER_RT9466)+=charger/rt946x.o
driver-$(CONFIG_CHARGER_RT9467)+=charger/rt946x.o
driver-$(CONFIG_CHARGER_RT9490)+=charger/rt9490.o
driver-$(CONFIG_CHARGER_SY21612)+=charger/sy21612.o
driver-$(CONFIG_CHARGER_SM5803)+=charger/sm5803.o

# CEC drivers
driver-$(CONFIG_CEC_BITBANG)+=cec/bitbang.o
driver-$(CONFIG_CEC_IT83XX)+=cec/it83xx.o

# DP Redrivers
driver-$(CONFIG_DP_REDRIVER_TDP142)+=retimer/tdp142.o

# Fingerprint Sensors
include $(_driver_cur_dir)fingerprint/build.mk

# I/O expander
driver-$(CONFIG_IO_EXPANDER_CCGXXF)+=ioexpander/ccgxxf.o
driver-$(CONFIG_IO_EXPANDER_IT8801)+=ioexpander/it8801.o
driver-$(CONFIG_IO_EXPANDER_NCT38XX)+=ioexpander/ioexpander_nct38xx.o
driver-$(CONFIG_IO_EXPANDER_PCA9534)+=ioexpander/pca9534.o
driver-$(CONFIG_IO_EXPANDER_PCA9675)+=ioexpander/pca9675.o
driver-$(CONFIG_IO_EXPANDER_PCAL6408)+=ioexpander/pcal6408.o
driver-$(CONFIG_IO_EXPANDER_TCA64XXA)+=ioexpander/tca64xxa.o

driver-$(CONFIG_CTN730)+=nfc/ctn730.o

# Current/Power monitor
driver-$(CONFIG_INA219)$(CONFIG_INA231)+=ina2xx.o
driver-$(CONFIG_INA3221)+=ina3221.o

# LED drivers
driver-$(CONFIG_LED_DRIVER_DS2413)+=led/ds2413.o
driver-$(CONFIG_LED_DRIVER_LM3509)+=led/lm3509.o
driver-$(CONFIG_LED_DRIVER_LM3630A)+=led/lm3630a.o
driver-$(CONFIG_LED_DRIVER_LP5562)+=led/lp5562.o
driver-$(CONFIG_LED_DRIVER_MP3385)+=led/mp3385.o
driver-$(CONFIG_LED_DRIVER_OZ554)+=led/oz554.o
driver-$(CONFIG_LED_DRIVER_IS31FL3733B)+=led/is31fl3733b.o
driver-$(CONFIG_LED_DRIVER_IS31FL3743B)+=led/is31fl3743b.o
driver-$(CONFIG_LED_DRIVER_AW20198)+=led/aw20198.o
driver-$(CONFIG_LED_DRIVER_TLC59116F)+=led/tlc59116f.o

# 7-segment display
driver-$(CONFIG_MAX695X_SEVEN_SEGMENT_DISPLAY)+=led/max695x.o

# Nvidia GPU D-Notify driver
driver-$(CONFIG_GPU_NVIDIA)+=nvidia_gpu.o

# Voltage regulators
driver-$(CONFIG_REGULATOR_IR357X)+=regulator_ir357x.o

# Power Sourcing Equipment
driver-$(CONFIG_PSE_LTC4291)+=pse_ltc4291.o

# Temperature sensors
driver-$(CONFIG_TEMP_SENSOR_ADT7481)+=temp_sensor/adt7481.o
driver-$(CONFIG_TEMP_SENSOR_BD99992GW)+=temp_sensor/bd99992gw.o
driver-$(CONFIG_TEMP_SENSOR_EC_ADC)+=temp_sensor/ec_adc.o
driver-$(CONFIG_TEMP_SENSOR_F75303)+=temp_sensor/f75303.o
driver-$(CONFIG_TEMP_SENSOR_G753)+=temp_sensor/g753.o
driver-$(CONFIG_TEMP_SENSOR_G781)+=temp_sensor/g78x.o
driver-$(CONFIG_TEMP_SENSOR_G782)+=temp_sensor/g78x.o
driver-$(CONFIG_TEMP_SENSOR_OTI502)+=temp_sensor/oti502.o
driver-$(CONFIG_TEMP_SENSOR_PCT2075)+=temp_sensor/pct2075.o
driver-$(CONFIG_TEMP_SENSOR_SB_TSI)+=temp_sensor/sb_tsi.o
driver-$(CONFIG_TEMP_SENSOR_TMP006)+=temp_sensor/tmp006.o
driver-$(CONFIG_TEMP_SENSOR_TMP112)+=temp_sensor/tmp112.o
driver-$(CONFIG_TEMP_SENSOR_TMP411)+=temp_sensor/tmp411.o
driver-$(CONFIG_TEMP_SENSOR_TMP432)+=temp_sensor/tmp432.o
driver-$(CONFIG_TEMP_SENSOR_TMP468)+=temp_sensor/tmp468.o
driver-$(CONFIG_TEMP_SENSOR_AMD_R19ME4070)+=temp_sensor/amd_r19me4070.o

# Touchpads
driver-$(CONFIG_TOUCHPAD_GT7288)+=touchpad_gt7288.o
driver-$(CONFIG_TOUCHPAD_ELAN)+=touchpad_elan.o
driver-$(CONFIG_TOUCHPAD_ST)+=touchpad_st.o

# Thermistors
driver-$(CONFIG_THERMISTOR)+=temp_sensor/thermistor.o
driver-$(CONFIG_THERMISTOR_NCP15WB)+=temp_sensor/thermistor_ncp15wb.o

# Type-C port controller (TCPC) drivers
driver-$(CONFIG_USB_PD_TCPM_CCGXXF)+=tcpm/ccgxxf.o
driver-$(CONFIG_USB_PD_TCPM_STUB)+=tcpm/stub.o
driver-$(CONFIG_USB_PD_TCPM_TCPCI)+=tcpm/tcpci.o
driver-$(CONFIG_USB_PD_TCPM_FUSB302)+=tcpm/fusb302.o
driver-$(CONFIG_USB_PD_TCPM_MT6370)+=tcpm/mt6370.o
ifdef CONFIG_USB_PD_TCPM_ITE_ON_CHIP
driver-y +=tcpm/ite_pd_intc.o
driver-$(CONFIG_USB_PD_TCPM_DRIVER_IT83XX)+=tcpm/it83xx.o
driver-$(CONFIG_USB_PD_TCPM_DRIVER_IT8XXX2)+=tcpm/it8xxx2.o
endif
driver-$(CONFIG_USB_PD_TCPM_ANX7406)+=tcpm/anx7406.o
driver-$(CONFIG_USB_PD_TCPM_ANX74XX)+=tcpm/anx74xx.o
driver-$(CONFIG_USB_PD_TCPM_ANX7688)+=tcpm/anx7688.o
driver-$(CONFIG_USB_PD_TCPM_ANX7447)+=tcpm/anx7447.o
driver-$(CONFIG_USB_PD_TCPM_PS8745)+=tcpm/ps8xxx.o
driver-$(CONFIG_USB_PD_TCPM_PS8751)+=tcpm/ps8xxx.o
driver-$(CONFIG_USB_PD_TCPM_PS8755)+=tcpm/ps8xxx.o
driver-$(CONFIG_USB_PD_TCPM_PS8705)+=tcpm/ps8xxx.o
driver-$(CONFIG_USB_PD_TCPM_PS8805)+=tcpm/ps8xxx.o
driver-$(CONFIG_USB_PD_TCPM_PS8815)+=tcpm/ps8xxx.o
driver-$(CONFIG_USB_PD_TCPM_TUSB422)+=tcpm/tusb422.o
driver-$(CONFIG_USB_PD_TCPM_RT1715)+=tcpm/rt1715.o
driver-$(CONFIG_USB_PD_TCPM_RT1718S)+=tcpm/rt1718s.o
driver-$(CONFIG_USB_PD_TCPM_NCT38XX)+=tcpm/nct38xx.o
driver-$(CONFIG_USB_PD_TCPM_RAA489000)+=tcpm/raa489000.o
driver-$(CONFIG_USB_PD_TCPM_FUSB307)+=tcpm/fusb307.o
driver-$(CONFIG_USB_PD_TCPM_STM32GX)+=tcpm/stm32gx.o

# Type-C Retimer drivers
driver-$(CONFIG_USBC_RETIMER_ANX7483)+=retimer/anx7483.o
driver-$(CONFIG_USBC_RETIMER_ANX7452)+=retimer/anx7452.o
driver-$(CONFIG_USBC_RETIMER_INTEL_BB)+=retimer/bb_retimer.o
driver-$(CONFIG_USBC_RETIMER_KB800X)+=retimer/kb800x.o
driver-$(CONFIG_USBC_RETIMER_KB8010)+=retimer/kb8010.o
driver-$(CONFIG_USBC_RETIMER_NB7V904M)+=retimer/nb7v904m.o
driver-$(CONFIG_USBC_RETIMER_PI3DPX1207)+=retimer/pi3dpx1207.o
driver-$(CONFIG_USBC_RETIMER_PI3HDX1204)+=retimer/pi3hdx1204.o
driver-$(CONFIG_USBC_RETIMER_PS8802)+=retimer/ps8802.o
driver-$(CONFIG_USBC_RETIMER_PS8811)+=retimer/ps8811.o
driver-$(CONFIG_USBC_RETIMER_PS8818)+=retimer/ps8818.o
driver-$(CONFIG_USBC_RETIMER_TUSB544)+=retimer/tusb544.o

# USB mux high-level driver
driver-$(CONFIG_USBC_SS_MUX)+=usb_mux/usb_mux.o

# USB muxes
driver-$(CONFIG_USB_MUX_AMD_FP5)+=usb_mux/amd_fp5.o
driver-$(CONFIG_USB_MUX_AMD_FP6)+=usb_mux/amd_fp6.o
driver-$(CONFIG_USB_MUX_ANX3443)+=usb_mux/anx3443.o
driver-$(CONFIG_USB_MUX_ANX7440)+=usb_mux/anx7440.o
driver-$(CONFIG_USB_MUX_ANX7451)+=usb_mux/anx7451.o
driver-$(CONFIG_USB_MUX_IT5205)+=usb_mux/it5205.o
driver-$(CONFIG_USB_MUX_PI3USB30532)+=usb_mux/pi3usb3x532.o
driver-$(CONFIG_USB_MUX_PI3USB31532)+=usb_mux/pi3usb3x532.o
driver-$(CONFIG_USB_MUX_PS8740)+=usb_mux/ps8740.o
driver-$(CONFIG_USB_MUX_PS8742)+=usb_mux/ps8740.o
driver-$(CONFIG_USB_MUX_PS8743)+=usb_mux/ps8743.o
driver-$(CONFIG_USB_MUX_PS8822)+=usb_mux/ps8822.o
driver-$(CONFIG_USB_MUX_TUSB1044)+=usb_mux/tusb1064.o
driver-$(CONFIG_USB_MUX_TUSB1064)+=usb_mux/tusb1064.o
driver-$(CONFIG_USB_MUX_TUSB546)+=usb_mux/tusb1064.o
driver-$(CONFIG_USB_MUX_VIRTUAL)+=usb_mux/virtual.o

# USB Hub with I2C interface
driver-$(CONFIG_USB_HUB_GL3590)+=gl3590.o

# Type-C Power Path Controllers (PPC)
driver-$(CONFIG_USBC_PPC_AOZ1380)+=ppc/aoz1380.o
driver-$(CONFIG_USBC_PPC_RT1718S)+=ppc/rt1718s.o
driver-$(CONFIG_USBC_PPC_RT1739)+=ppc/rt1739.o
driver-$(CONFIG_USBC_PPC_SN5S330)+=ppc/sn5s330.o
ifeq ($(CONFIG_USBC_PPC_NX20P3481)$(CONFIG_USBC_PPC_NX20P3483),y)
driver-y += ppc/nx20p348x.o
endif
driver-$(CONFIG_USBC_PPC_SYV682X)+=ppc/syv682x.o
driver-$(CONFIG_USBC_PPC_NX20P3483)+=ppc/nx20p348x.o
driver-$(CONFIG_USBC_PPC_KTU1125)+=ppc/ktu1125.o
driver-$(CONFIG_USBC_PPC_TCPCI)+=ppc/tcpci_ppc.o

# Switchcap
driver-$(CONFIG_LN9310)+=ln9310.o

# video converters
driver-$(CONFIG_MCDP28X0)+=mcdp28x0.o

# Wireless Power Chargers
driver-$(CONFIG_CPS8100)+=wpc/cps8100.o
driver-$(HAS_TASK_WPC) += wpc/p9221.o

# Buck-Boost converters
driver-$(CONFIG_MP4245)+=mp4245.o

# Power Management ICs
driver-$(CONFIG_MP2964)+=mp2964.o

# SOC Interface
driver-$(CONFIG_AMD_SB_RMI)+=sb_rmi.o
driver-$(CONFIG_AMD_STT)+=amd_stt.o
