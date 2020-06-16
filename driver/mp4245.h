/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* MPS MP4245 Buck-Boost converter driver definitions */

/* I2C addresses */
#define MP4245_I2C_ADDR_0_FLAGS 0x61  /* R1 -> GND */
#define MP4245_I2C_ADDR_1_FLAGS 0x62  /* R1 -> 15.0k */
#define MP4245_I2C_ADDR_2_FLAGS 0x63  /* R1 -> 25.5k */
#define MP4245_I2C_ADDR_3_FLAGS 0x64  /* R1 -> 35.7k */
#define MP4245_I2C_ADDR_4_FLAGS 0x65  /* R1 -> 45.3k */
#define MP4245_I2C_ADDR_5_FLAGS 0x66  /* R1 -> 56.0k */
#define MP4245_I2C_ADDR_6_FLAGS 0x67  /* R1 -> VCC */


/* MP4245 CMD Offsets */
#define MP4245_CMD_OPERATION         0x01
#define MP4245_CMD_CLEAR_FAULTS      0x03
#define MP4245_CMD_WRITE_PROTECT     0x10
#define MP4245_CMD_STORE_USER_ALL    0x15
#define MP4245_CMD_RESTORE_USER_ALL  0x16
#define MP4245_CMD_VOUT_MODE         0x20
#define MP4245_CMD_VOUT_COMMAND      0x21
#define MP4245_CMD_VOUT_SCALE_LOOP   0x29
#define MP4245_CMD_STATUS_BYTE       0x78
#define MP4245_CMD_STATUS_WORD       0x79
#define MP4245_CMD_STATUS_VOUT       0x7A
#define MP4245_CMD_STATUS_INPUT      0x7C
#define MP4245_CMD_STATUS_TEMP       0x7D
#define MP4245_CMD_STATUS_CML        0x7E
#define MP4245_CMD_READ_VIN          0x88
#define MP4245_CMD_READ_VOUT         0x8B
#define MP4245_CMD_READ_IOUT         0x8C
#define MP4245_CMD_READ_TEMP         0x8D
#define MP4245_CMD_MFR_MODE_CTRL     0xD0
#define MP4245_CMD_MFR_CURRENT_LIM   0xD1
#define MP4245_CMD_MFR_LINE_DROP     0xD2
#define MP4245_CMD_MFR_OT_FAULT_LIM  0xD3
#define MP4245_CMD_MFR_OT_WARN_LIM   0xD4
#define MP4245_CMD_MFR_CRC_ERROR     0xD5
#define MP4245_CMD_MFF_MTP_CFG_CODE  0xD6
#define MP4245_CMD_MFR_MTP_REV_NUM   0xD7
#define MP4245_CMD_MFR_STATUS_MASK   0xD8

#define MP4245_CMD_OPERATION_ON      BIT(7)

#define MP4245_VOUT_1V               BIT(10)
#define MP4245_VOUT_FROM_MV          (MP4245_VOUT_1V * MP4245_VOUT_1V / 1000)
#define MP4245_VOUT_TO_MV(v)         ((v * 1000) / MP4245_VOUT_1V)
#define MP4245_IOUT_TO_MA(i)         (((i & 0x7ff) * 1000) / BIT(6))
#define MP4245_ILIM_STEP_MA          50
#define MP4245_VOUT_5V_DELAY_MS      10


#define MP4245_MFR_STATUS_MASK_VOUT        BIT(7)
#define MP4245_MFR_STATUS_MASK_IOUT        BIT(6)
#define MP4245_MFR_STATUS_MASK_INPUT       BIT(5)
#define MP4245_MFR_STATUS_MASK_TEMP        BIT(4)
#define MP4245_MFR_STATUS_MASK_PG_STATUS   BIT(3)
#define MP4245_MFR_STATUS_MASK_PG_ALT_EDGE BIT(2)
#define MP4245_MFR_STATUS_MASK_OTHER       BIT(1)
#define MP4245_MFR_STATUS_MASK_UNKNOWN     BIT(0)

/**
 * MP4245 set output voltage level
 *
 * @param desired_mv -> voltage level in mV
 * @return i2c write result
 */
int mp4245_set_voltage_out(int desired_mv);

/**
 * MP4245 set output current limit
 *
 * @param desired_ma -> current limit in mA
 * @return i2c write result
 */
int mp4245_set_current_lim(int desired_ma);

/**
 * MP4245 set output current limit
 *
 * @param enable -> 0 = off, else on
 * @return i2c write result
 */
int mp4245_votlage_out_enable(int enable);

/**
 * MP4245 get Vbus voltage/current values
 *
 * @param *mv -> vbus voltage in mV
 * @param *ma -> vbus current in mA
 * @return i2c read results
 */
int mp3245_get_vbus(int *mv, int *ma);
