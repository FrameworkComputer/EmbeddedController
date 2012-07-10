/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host communication command constants for Chrome EC */

#ifndef __CROS_EC_COMMANDS_H
#define __CROS_EC_COMMANDS_H

/*
 * Protocol overview
 *
 * request:  CMD [ P0 P1 P2 ... Pn S ]
 * response: ERR [ P0 P1 P2 ... Pn S ]
 *
 * where the bytes are defined as follow :
 *      - CMD is the command code. (defined by EC_CMD_ constants)
 *      - ERR is the error code. (defined by EC_RES_ constants)
 *      - Px is the optional payload.
 *        it is not sent if the error code is not success.
 *        (defined by ec_params_ and ec_response_ structures)
 *      - S is the checksum which is the sum of all payload bytes.
 *
 * On LPC, CMD and ERR are sent/received at EC_LPC_ADDR_KERNEL|USER_CMD
 * and the payloads are sent/received at EC_LPC_ADDR_KERNEL|USER_PARAM.
 * On I2C, all bytes are sent serially in the same message.
 */

/* Current version of this protocol */
#define EC_PROTO_VERSION          0x00000002

/* I/O addresses for LPC commands */
#define EC_LPC_ADDR_KERNEL_DATA   0x62
#define EC_LPC_ADDR_KERNEL_CMD    0x66
#define EC_LPC_ADDR_KERNEL_PARAM 0x800
#define EC_LPC_ADDR_USER_DATA    0x200
#define EC_LPC_ADDR_USER_CMD     0x204
#define EC_LPC_ADDR_USER_PARAM   0x880
#define EC_PARAM_SIZE          128  /* Size of each param area in bytes */

/* EC command register bit functions */
#define EC_LPC_CMDR_DATA	(1 << 0)
#define EC_LPC_CMDR_PENDING	(1 << 1)
#define EC_LPC_CMDR_BUSY	(1 << 2)
#define EC_LPC_CMDR_CMD		(1 << 3)
#define EC_LPC_CMDR_ACPI_BRST	(1 << 4)
#define EC_LPC_CMDR_SCI		(1 << 5)
#define EC_LPC_CMDR_SMI		(1 << 6)

#define EC_LPC_ADDR_MEMMAP       0x900
#define EC_MEMMAP_SIZE         255 /* ACPI IO buffer max is 255 bytes */
#define EC_MEMMAP_TEXT_MAX     8   /* Size of a string in the memory map */

/* The offset address of each type of data in mapped memory. */
#define EC_MEMMAP_TEMP_SENSOR 0x00
#define EC_MEMMAP_FAN         0x10
#define EC_MEMMAP_SWITCHES    0x30
#define EC_MEMMAP_HOST_EVENTS 0x34
#define EC_MEMMAP_BATT_VOLT   0x40 /* Battery Present Voltage */
#define EC_MEMMAP_BATT_RATE   0x44 /* Battery Present Rate */
#define EC_MEMMAP_BATT_CAP    0x48 /* Battery Remaining Capacity */
#define EC_MEMMAP_BATT_FLAG   0x4c /* Battery State, defined below */
#define EC_MEMMAP_BATT_DCAP   0x50 /* Battery Design Capacity */
#define EC_MEMMAP_BATT_DVLT   0x54 /* Battery Design Voltage */
#define EC_MEMMAP_BATT_LFCC   0x58 /* Battery Last Full Charge Capacity */
#define EC_MEMMAP_BATT_CCNT   0x5c /* Battery Cycle Count */
#define EC_MEMMAP_BATT_MFGR   0x60 /* Battery Manufacturer String */
#define EC_MEMMAP_BATT_MODEL  0x68 /* Battery Model Number String */
#define EC_MEMMAP_BATT_SERIAL 0x70 /* Battery Serial Number String */
#define EC_MEMMAP_BATT_TYPE   0x78 /* Battery Type String */

/* Battery bit flags at EC_MEMMAP_BATT_FLAG. */
#define EC_BATT_FLAG_AC_PRESENT   0x01
#define EC_BATT_FLAG_BATT_PRESENT 0x02
#define EC_BATT_FLAG_DISCHARGING  0x04
#define EC_BATT_FLAG_CHARGING     0x08
#define EC_BATT_FLAG_LEVEL_CRITICAL 0x10

/* Switch flags at EC_MEMMAP_SWITCHES */
#define EC_SWITCH_LID_OPEN               0x01
#define EC_SWITCH_POWER_BUTTON_PRESSED   0x02
#define EC_SWITCH_WRITE_PROTECT_DISABLED 0x04
/* Recovery requested via keyboard */
#define EC_SWITCH_KEYBOARD_RECOVERY      0x08
/* Recovery requested via dedicated signal (from servo board) */
#define EC_SWITCH_DEDICATED_RECOVERY     0x10
/* Was fake developer mode switch; now unused.  Remove in next refactor. */
#define EC_SWITCH_IGNORE0                0x20

/* Wireless switch flags */
#define EC_WIRELESS_SWITCH_WLAN      0x01
#define EC_WIRELESS_SWITCH_BLUETOOTH 0x02

/*
 * The offset of temperature value stored in mapped memory.  This allows
 * reporting a temperature range of 200K to 454K = -73C to 181C.
 */
#define EC_TEMP_SENSOR_OFFSET 200

/*
 * This header file is used in coreboot both in C and ACPI code.  The ACPI code
 * is pre-processed to handle constants but the ASL compiler is unable to
 * handle actual C code so keep it separate.
 */
#ifndef __ACPI__

/* LPC command status byte masks */
/* EC has written a byte in the data register and host hasn't read it yet */
#define EC_LPC_STATUS_TO_HOST     0x01
/* Host has written a command/data byte and the EC hasn't read it yet */
#define EC_LPC_STATUS_FROM_HOST   0x02
/* EC is processing a command */
#define EC_LPC_STATUS_PROCESSING  0x04
/* Last write to EC was a command, not data */
#define EC_LPC_STATUS_LAST_CMD    0x08
/* EC is in burst mode.  Unsupported by Chrome EC, so this bit is never set */
#define EC_LPC_STATUS_BURST_MODE  0x10
/* SCI event is pending (requesting SCI query) */
#define EC_LPC_STATUS_SCI_PENDING 0x20
/* SMI event is pending (requesting SMI query) */
#define EC_LPC_STATUS_SMI_PENDING 0x40
/* (reserved) */
#define EC_LPC_STATUS_RESERVED    0x80

/*
 * EC is busy.  This covers both the EC processing a command, and the host has
 * written a new command but the EC hasn't picked it up yet.
 */
#define EC_LPC_STATUS_BUSY_MASK \
	(EC_LPC_STATUS_FROM_HOST | EC_LPC_STATUS_PROCESSING)

/* Host command response codes */
/* TODO: move these so they don't overlap SCI/SMI data? */
enum ec_status {
	EC_RES_SUCCESS = 0,
	EC_RES_INVALID_COMMAND = 1,
	EC_RES_ERROR = 2,
	EC_RES_INVALID_PARAM = 3,
	EC_RES_ACCESS_DENIED = 4,
};

/*
 * Host event codes.  Note these are 1-based, not 0-based, because ACPI query
 * EC command uses code 0 to mean "no event pending".  We explicitly specify
 * each value in the enum listing so they won't change if we delete/insert an
 * item or rearrange the list (it needs to be stable across platforms, not
 * just within a single compiled instance).
 */
enum host_event_code {
	EC_HOST_EVENT_LID_CLOSED = 1,
	EC_HOST_EVENT_LID_OPEN = 2,
	EC_HOST_EVENT_POWER_BUTTON = 3,
	EC_HOST_EVENT_AC_CONNECTED = 4,
	EC_HOST_EVENT_AC_DISCONNECTED = 5,
	EC_HOST_EVENT_BATTERY_LOW = 6,
	EC_HOST_EVENT_BATTERY_CRITICAL = 7,
	EC_HOST_EVENT_BATTERY = 8,
	EC_HOST_EVENT_THERMAL_THRESHOLD = 9,
	EC_HOST_EVENT_THERMAL_OVERLOAD = 10,
	EC_HOST_EVENT_THERMAL = 11,
	EC_HOST_EVENT_USB_CHARGER = 12,
	EC_HOST_EVENT_KEY_PRESSED = 13,
	/*
	 * EC has finished initializing the host interface.  The host can check
	 * for this event following sending a EC_CMD_REBOOT_EC command to
	 * determine when the EC is ready to accept subsequent commands.
	 */
	EC_HOST_EVENT_INTERFACE_READY = 14,
	/*
	 * The high bit of the event mask is not used as a host event code.  If
	 * it reads back as set, then the entire event mask should be
	 * considered invalid by the host.  This can happen when reading the
	 * raw event status via EC_MEMMAP_HOST_EVENTS but the LPC interface is
	 * not initialized on the EC, or improperly configured on the host.
	 */
	EC_HOST_EVENT_INVALID = 32
};
/* Host event mask */
#define EC_HOST_EVENT_MASK(event_code) (1UL << ((event_code) - 1))

/*
 * Notes on commands:
 *
 * Each command is an 8-byte command value.  Commands which take params or
 * return response data specify structs for that data.  If no struct is
 * specified, the command does not input or output data, respectively.
 * Parameter/response length is implicit in the structs.  Some underlying
 * communication protocols (I2C, SPI) may add length or checksum headers, but
 * those are implementation-dependent and not defined here.
 */

/*****************************************************************************/
/* General / test commands */

/*
 * Get protocol version, used to deal with non-backward compatible protocol
 * changes.
 */
#define EC_CMD_PROTO_VERSION 0x00

struct ec_response_proto_version {
	uint32_t version;
} __packed;

/*
 * Hello.  This is a simple command to test the EC is responsive to
 * commands.
 */
#define EC_CMD_HELLO 0x01

struct ec_params_hello {
	uint32_t in_data;  /* Pass anything here */
} __packed;

struct ec_response_hello {
	uint32_t out_data;  /* Output will be in_data + 0x01020304 */
} __packed;

/* Get version number */
#define EC_CMD_GET_VERSION 0x02

enum ec_current_image {
	EC_IMAGE_UNKNOWN = 0,
	EC_IMAGE_RO,
	EC_IMAGE_RW_A,
	EC_IMAGE_RW_B
};

struct ec_response_get_version {
	/* Null-terminated version strings for RO, RW-A, RW-B */
	char version_string_ro[32];
	char version_string_rw_a[32];
	char version_string_rw_b[32];
	uint32_t current_image;  /* One of ec_current_image */
} __packed;

/* Read test */
#define EC_CMD_READ_TEST 0x03

struct ec_params_read_test {
	uint32_t offset;   /* Starting value for read buffer */
	uint32_t size;     /* Size to read in bytes */
} __packed;

struct ec_response_read_test {
	uint32_t data[32];
} __packed;

/* Get build information */
#define EC_CMD_GET_BUILD_INFO 0x04

struct ec_response_get_build_info {
	char build_string[EC_PARAM_SIZE];
} __packed;

/* Get chip info */
#define EC_CMD_GET_CHIP_INFO 0x05

struct ec_response_get_chip_info {
	/* Null-terminated strings */
	char vendor[32];
	char name[32];
	char revision[32];  /* Mask version */
} __packed;

/* Get board HW version. */
#define EC_CMD_GET_BOARD_VERSION 0x06

struct ec_params_board_version {
	uint16_t board_version;  /* A monotonously incrementing number. */
} __packed;

/*
 * Read memory-mapped data.
 *
 * This is an alternate interface to memory-mapped data for bus protocols
 * which don't support direct-mapped memory - I2C, SPI, etc.
 */
#define EC_CMD_READ_MEMMAP 0x07

struct ec_params_read_memmap {
	uint8_t offset;   /* Offset in memmap (EC_MEMMAP_*) */
	uint8_t size;     /* Size to read in bytes */
} __packed;

struct ec_response_read_memmap {
	uint32_t data[EC_PARAM_SIZE];
} __packed;

/*****************************************************************************/
/* Flash commands */

/* Get flash info */
#define EC_CMD_FLASH_INFO 0x10

struct ec_response_flash_info {
	/* Usable flash size, in bytes */
	uint32_t flash_size;
	/*
	 * Write block size.  Write offset and size must be a multiple
	 * of this.
	 */
	uint32_t write_block_size;
	/*
	 * Erase block size.  Erase offset and size must be a multiple
	 * of this.
	 */
	uint32_t erase_block_size;
	/*
	 * Protection block size.  Protection offset and size must be a
	 * multiple of this.
	 */
	uint32_t protect_block_size;
} __packed;

/* Read flash */
#define EC_CMD_FLASH_READ 0x11

struct ec_params_flash_read {
	uint32_t offset;   /* Byte offset to read */
	uint32_t size;     /* Size to read in bytes */
} __packed;

struct ec_response_flash_read {
	uint8_t data[EC_PARAM_SIZE];
} __packed;

/* Write flash */
#define EC_CMD_FLASH_WRITE 0x12

struct ec_params_flash_write {
	uint32_t offset;   /* Byte offset to write */
	uint32_t size;     /* Size to write in bytes */
	/*
	 * Data to write.  Could really use EC_PARAM_SIZE - 8, but tidiest to
	 * use a power of 2 so writes stay aligned.
	 */
	uint8_t data[64];
} __packed;

/* Erase flash */
#define EC_CMD_FLASH_ERASE 0x13

struct ec_params_flash_erase {
	uint32_t offset;   /* Byte offset to erase */
	uint32_t size;     /* Size to erase in bytes */
} __packed;

/* Flashmap offset */
#define EC_CMD_FLASH_GET_FLASHMAP 0x14

struct ec_response_flash_flashmap {
	uint32_t offset;   /* Flashmap offset */
} __packed;

/* Enable/disable flash write protect */
#define EC_CMD_FLASH_WP_ENABLE 0x15

struct ec_params_flash_wp_enable {
	uint32_t enable_wp;
} __packed;

/* Get flash write protection commit state */
#define EC_CMD_FLASH_WP_GET_STATE 0x16

struct ec_response_flash_wp_enable {
	uint32_t enable_wp;
} __packed;

/* Set/get flash write protection range */
#define EC_CMD_FLASH_WP_SET_RANGE 0x17

struct ec_params_flash_wp_range {
	/* Byte offset aligned to info.protect_block_size */
	uint32_t offset;
	/* Size should be multiply of info.protect_block_size */
	uint32_t size;
} __packed;

#define EC_CMD_FLASH_WP_GET_RANGE 0x18

struct ec_response_flash_wp_range {
	uint32_t offset;
	uint32_t size;
} __packed;

/* Read flash write protection GPIO pin */
#define EC_CMD_FLASH_WP_GET_GPIO 0x19

struct ec_params_flash_wp_gpio {
	uint32_t pin_no;
} __packed;

struct ec_response_flash_wp_gpio {
	uint32_t value;
} __packed;

/*****************************************************************************/
/* PWM commands */

/* Get fan RPM */
#define EC_CMD_PWM_GET_FAN_RPM 0x20

struct ec_response_pwm_get_fan_rpm {
	uint32_t rpm;
} __packed;

/* Set target fan RPM */
#define EC_CMD_PWM_SET_FAN_TARGET_RPM 0x21

struct ec_params_pwm_set_fan_target_rpm {
	uint32_t rpm;
} __packed;

/* Get keyboard backlight */
#define EC_CMD_PWM_GET_KEYBOARD_BACKLIGHT 0x22

struct ec_response_pwm_get_keyboard_backlight {
	uint8_t percent;
	uint8_t enabled;
} __packed;

/* Set keyboard backlight */
#define EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT 0x23

struct ec_params_pwm_set_keyboard_backlight {
	uint8_t percent;
} __packed;

/* Set target fan PWM duty cycle */
#define EC_CMD_PWM_SET_FAN_DUTY 0x24

struct ec_params_pwm_set_fan_duty {
	uint32_t percent;
} __packed;

/*****************************************************************************/
/*
 * Lightbar commands. This looks worse than it is. Since we only use one LPC
 * command to say "talk to the lightbar", we put the "and tell it to do X" part
 * into a subcommand. We'll make separate structs for subcommands with
 * different input args, so that we know how much to expect.
 */
#define EC_CMD_LIGHTBAR_CMD 0x28

struct ec_params_lightbar_cmd {
	union {
		union {
			uint8_t cmd;
			struct {
				uint8_t cmd;
			} dump, off, on, init, get_seq;
			struct num {
				uint8_t cmd;
				uint8_t num;
			} brightness, seq;

			struct reg {
				uint8_t cmd;
				uint8_t ctrl, reg, value;
			} reg;
			struct rgb {
				uint8_t cmd;
				uint8_t led, red, green, blue;
			} rgb;
		} in;
		union {
			struct dump {
				struct {
					uint8_t reg;
					uint8_t ic0;
					uint8_t ic1;
				} vals[23];
			} dump;
			struct get_seq {
				uint8_t num;
			} get_seq;
			struct {
				/* no return params */
			} off, on, init, brightness, seq, reg, rgb;
		} out;
	};
} __packed;

/*****************************************************************************/
/* Verified boot commands */

/*
 * Verified boot uber-command.  Details still evolving.  Like the lightbar
 * command above, this takes sub-commands.
 */
#define EC_CMD_VBOOT_CMD 0x29

struct ec_params_vboot_cmd {
	union {
		union {
			uint8_t cmd;
			struct {
				uint8_t cmd;
				/* no inputs */
			} get_flags;
			struct {
				uint8_t cmd;
				uint8_t val;
			} set_flags;
		} in;
		union {
			struct {
				uint8_t val;
			} get_flags;
			struct {
				/* no outputs */
			} set_flags;
		} out;
	};
} __packed;

/* Verified boot hash command */
#define EC_CMD_VBOOT_HASH 0x2A

struct ec_params_vboot_hash {
	uint8_t cmd;             /* enum ec_vboot_hash_cmd */
	uint8_t hash_type;       /* enum ec_vboot_hash_type */
	uint8_t nonce_size;      /* Nonce size; may be 0 */
	uint8_t reserved0;       /* Reserved; set 0 */
	uint32_t offset;         /* Offset in flash to hash */
	uint32_t size;           /* Number of bytes to hash */
	uint8_t nonce_data[64];  /* Nonce data; ignored if nonce_size=0 */
} __packed;

struct ec_response_vboot_hash {
	uint8_t status;          /* enum ec_vboot_hash_status */
	uint8_t hash_type;       /* enum ec_vboot_hash_type */
	uint8_t digest_size;     /* Size of hash digest in bytes */
	uint8_t reserved0;       /* Ignore; will be 0 */
	uint32_t offset;         /* Offset in flash which was hashed */
	uint32_t size;           /* Number of bytes hashed */
	uint8_t hash_digest[64]; /* Hash digest data */
} __packed;

enum ec_vboot_hash_cmd {
	EC_VBOOT_HASH_GET,     /* Get current hash status */
	EC_VBOOT_HASH_ABORT,   /* Abort calculating current hash */
	EC_VBOOT_HASH_START,   /* Start computing a new hash */
	EC_VBOOT_HASH_RECALC,  /* Synchronously compute a new hash */
};

enum ec_vboot_hash_type {
	EC_VBOOT_HASH_TYPE_SHA256,  /* SHA-256 */
};

enum ec_vboot_hash_status {
	EC_VBOOT_HASH_STATUS_NONE,     /* No hash (not started, or aborted) */
	EC_VBOOT_HASH_STATUS_DONE,     /* Finished computing a hash */
	EC_VBOOT_HASH_STATUS_BUSY,     /* Busy computing a hash */
};

/*****************************************************************************/
/* USB charging control commands */

/* Set USB port charging mode */
#define EC_CMD_USB_CHARGE_SET_MODE 0x30

struct ec_params_usb_charge_set_mode {
	uint8_t usb_port_id;
	uint8_t mode;
} __packed;

/*****************************************************************************/
/* Persistent storage for host */

/* Maximum bytes that can be read/written in a single command */
#define EC_PSTORE_SIZE_MAX 64

/* Get persistent storage info */
#define EC_CMD_PSTORE_INFO 0x40

struct ec_response_pstore_info {
	/* Persistent storage size, in bytes */
	uint32_t pstore_size;
	/* Access size; read/write offset and size must be a multiple of this */
	uint32_t access_size;
} __packed;

/* Read persistent storage */
#define EC_CMD_PSTORE_READ 0x41

struct ec_params_pstore_read {
	uint32_t offset;   /* Byte offset to read */
	uint32_t size;     /* Size to read in bytes */
} __packed;

struct ec_response_pstore_read {
	uint8_t data[EC_PSTORE_SIZE_MAX];
} __packed;

/* Write persistent storage */
#define EC_CMD_PSTORE_WRITE 0x42

struct ec_params_pstore_write {
	uint32_t offset;   /* Byte offset to write */
	uint32_t size;     /* Size to write in bytes */
	uint8_t data[EC_PSTORE_SIZE_MAX];
} __packed;

/*****************************************************************************/
/* Thermal engine commands */

/* Set thershold value */
#define EC_CMD_THERMAL_SET_THRESHOLD 0x50

struct ec_params_thermal_set_threshold {
	uint8_t sensor_type;
	uint8_t threshold_id;
	uint16_t value;
} __packed;

/* Get threshold value */
#define EC_CMD_THERMAL_GET_THRESHOLD 0x51

struct ec_params_thermal_get_threshold {
	uint8_t sensor_type;
	uint8_t threshold_id;
} __packed;

struct ec_response_thermal_get_threshold {
	uint16_t value;
} __packed;

/* Toggling automatic fan control */
#define EC_CMD_THERMAL_AUTO_FAN_CTRL 0x52

/*****************************************************************************/
/* MKBP - Matrix KeyBoard Protocol */

/* Read key state */
#define EC_CMD_MKBP_STATE 0x60

struct ec_response_mkbp_state {
	uint8_t cols[32];
} __packed;

/* Provide information about the matrix : number of rows and columns */
#define EC_CMD_MKBP_INFO 0x61

struct ec_response_mkbp_info {
	uint32_t rows;
	uint32_t cols;
	uint8_t switches;
} __packed;

/* Simulate key press */
#define EC_CMD_MKBP_SIMULATE_KEY 0x62

struct ec_params_mkbp_simulate_key {
	uint8_t col;
	uint8_t row;
	uint8_t pressed;
} __packed;

/*****************************************************************************/
/* Temperature sensor commands */

/* Read temperature sensor info */
#define EC_CMD_TEMP_SENSOR_GET_INFO 0x70

struct ec_params_temp_sensor_get_info {
	uint8_t id;
} __packed;

struct ec_response_temp_sensor_get_info {
	char sensor_name[32];
	uint8_t sensor_type;
} __packed;

/*****************************************************************************/
/* Host event commands */

/*
 * Host event mask params and response structures, shared by all of the host
 * event commands below.
 */
struct ec_params_host_event_mask {
	uint32_t mask;
} __packed;

struct ec_response_host_event_mask {
	uint32_t mask;
} __packed;

/* These all use ec_response_host_event_mask */
#define EC_CMD_HOST_EVENT_GET_SMI_MASK  0x88
#define EC_CMD_HOST_EVENT_GET_SCI_MASK  0x89
#define EC_CMD_HOST_EVENT_GET_WAKE_MASK 0x8d

/* These all use ec_params_host_event_mask */
#define EC_CMD_HOST_EVENT_SET_SMI_MASK  0x8a
#define EC_CMD_HOST_EVENT_SET_SCI_MASK  0x8b
#define EC_CMD_HOST_EVENT_CLEAR         0x8c
#define EC_CMD_HOST_EVENT_SET_WAKE_MASK 0x8e

/*****************************************************************************/
/* GPIO switch commands */

/* Enable/disable LCD backlight */
#define EC_CMD_SWITCH_ENABLE_BKLIGHT 0x90

struct ec_params_switch_enable_backlight {
	uint8_t enabled;
} __packed;

/* Enable/disable WLAN/Bluetooth */
#define EC_CMD_SWITCH_ENABLE_WIRELESS 0x91

struct ec_params_switch_enable_wireless {
	uint8_t enabled;
} __packed;

/*****************************************************************************/
/* System commands */

/*
 * TODO: this is a confusing name, since it doesn't necessarily reboot the EC.
 * Rename to "set image" or something similar.
 */
#define EC_CMD_REBOOT_EC 0xd2

/* Command */
enum ec_reboot_cmd {
	EC_REBOOT_CANCEL = 0,        /* Cancel a pending reboot */
	EC_REBOOT_JUMP_RO,           /* Jump to RO without rebooting */
	EC_REBOOT_JUMP_RW_A,         /* Jump to RW-A without rebooting */
	EC_REBOOT_JUMP_RW_B,         /* Jump to RW-B without rebooting */
	EC_REBOOT_COLD,              /* Cold-reboot */
	EC_REBOOT_DISABLE_JUMP,      /* Disable jump until next reboot */
};

/* Flags for ec_params_reboot_ec.reboot_flags */
#define EC_REBOOT_FLAG_RESERVED0      (1 << 0)  /* Was recovery request */
#define EC_REBOOT_FLAG_ON_AP_SHUTDOWN (1 << 1)
#define EC_REBOOT_FLAG_POWER_ON       (1 << 2)

struct ec_params_reboot_ec {
	uint8_t cmd;           /* enum ec_reboot_cmd */
	uint8_t flags;         /* See EC_REBOOT_FLAG_* */
} __packed;

/*****************************************************************************/
/*
 * Special commands
 *
 * These do not follow the normal rules for commands.  See each command for
 * details.
 */

/*
 * ACPI Query Embedded Controller
 *
 * This clears the lowest-order bit in the currently pending host events, and
 * sets the result code to the 1-based index of the bit (event 0x00000001 = 1,
 * event 0x80000000 = 32), or 0 if no event was pending.
 */
#define EC_CMD_ACPI_QUERY_EVENT 0x84

/*
 * Reboot NOW
 *
 * This command will work even when the EC LPC interface is busy, because the
 * reboot command is processed at interrupt level.  Note that when the EC
 * reboots, the host will reboot too, so there is no response to this command.
 *
 * Use EC_CMD_REBOOT_EC to reboot the EC more politely.
 */
#define EC_CMD_REBOOT 0xd1  /* Think "die" */

#endif  /* !__ACPI__ */

#endif  /* __CROS_EC_COMMANDS_H */
