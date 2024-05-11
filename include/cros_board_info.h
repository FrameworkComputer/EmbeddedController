/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cros Board Info
 */
#ifndef __CROS_EC_CROS_BOARD_INFO_H
#define __CROS_EC_CROS_BOARD_INFO_H

#include "common.h"
#include "ec_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CBI_VERSION_MAJOR 0
#define CBI_VERSION_MINOR 0

#define CBI_IMAGE_SIZE_EEPROM 256

#ifdef CONFIG_CBI_GPIO
/*
 * if CBI is sourced from GPIO, the CBI cache only needs to accomondate
 * BOARD_VERSION and SKU_ID
 */
#define CBI_IMAGE_SIZE               \
	(sizeof(struct cbi_header) + \
	 (2 * (sizeof(struct cbi_data) + sizeof(uint32_t))))
#elif defined(CONFIG_CBI_FLASH)
#define CBI_IMAGE_SIZE DT_PROP(DT_NODELABEL(cbi_flash), image_size)
#else
#define CBI_IMAGE_SIZE CBI_IMAGE_SIZE_EEPROM
#endif

static const uint8_t cbi_magic[] = { 0x43, 0x42, 0x49 }; /* 'C' 'B' 'I' */

struct cbi_header {
	uint8_t magic[3];
	/* CRC of 'struct board_info' excluding magic and crc */
	uint8_t crc;
	/* Data format version. Parsers are expected to process data as long
	 * as major version is equal or younger. */
	union {
		struct {
			uint8_t minor_version;
			uint8_t major_version;
		};
		uint16_t version;
	};
	/* Total size of data. It can be larger than sizeof(struct board_info)
	 * if future versions add additional fields. */
	uint16_t total_size;
	/* List of data items (i.e. struct cbi_data[]) */
	uint8_t data[];
} __attribute__((packed));

struct cbi_data {
	uint8_t tag; /* enum cbi_data_tag */
	uint8_t size; /* size of value[] */
	uint8_t value[]; /* data value */
} __attribute__((packed));

enum cbi_cache_status {
	CBI_CACHE_STATUS_SYNCED = 0,
	CBI_CACHE_STATUS_INVALID = 1
};

enum cbi_storage_type {
	CBI_STORAGE_TYPE_EEPROM = 0,
	CBI_STORAGE_TYPE_GPIO = 1,
	CBI_STORAGE_TYPE_FLASH = 2,
};

/*
 * Driver for storage media access
 */
struct cbi_storage_driver {
	/* Write the whole CBI from RAM to storage media (i.e. sync) */
	int (*store)(uint8_t *cache);
	/*
	 * Read blocks from storage media to RAM. Note that the granularity
	 * of load function is asymmetrical to that of the store function.
	 */
	int (*load)(uint8_t offset, uint8_t *data, int len);
	/* Return write protect status for the storage media */
	int (*is_protected)(void);
};

struct cbi_storage_config_t {
	enum cbi_storage_type storage_type;
	const struct cbi_storage_driver *drv;
};

extern const struct cbi_storage_config_t *cbi_config;

/**
 * Board info accessors
 *
 * @param version/sku_id/oem_id/id/fw_config/pcb_supplier/ssfc/rework_id [OUT]
 *        Data_read from EEPROM.
 * @return EC_SUCCESS on success or EC_ERROR_* otherwise.
 *         EC_ERROR_BUSY to indicate data is not ready.
 */
int cbi_get_board_version(uint32_t *version);
int cbi_get_sku_id(uint32_t *sku_id);
int cbi_get_oem_id(uint32_t *oem_id);
int cbi_get_model_id(uint32_t *id);
int cbi_get_fw_config(uint32_t *fw_config);
int cbi_get_pcb_supplier(uint32_t *pcb_supplier);
int cbi_get_ssfc(uint32_t *ssfc);
int cbi_get_rework_id(uint64_t *id);
int cbi_get_factory_calibration_data(uint32_t *calibration_data);
int cbi_get_common_control(union ec_common_control *ctrl);

/**
 * Get data from CBI store
 *
 * @param tag   Tag of the target data.
 * @param buf   Buffer where data is passed.
 * @param size  (IN) Size of <buf>. (OUT) Size of the data returned.
 * @return EC_SUCCESS on success or EC_ERROR_* otherwise.
 *         EC_ERROR_BUSY to indicate data is not ready.
 */
int cbi_get_board_info(enum cbi_data_tag tag, uint8_t *buf, uint8_t *size);

/**
 * Set data in CBI store
 *
 * @param tag   Tag of the target data.
 * @param buf   Buffer where data is passed.
 * @param size  (IN) Size of <buf>. (OUT) Size of the data returned.
 * @return EC_SUCCESS on success or EC_ERROR_* otherwise.
 */
int cbi_set_board_info(enum cbi_data_tag tag, const uint8_t *buf, uint8_t size);

/*
 * Utility functions
 */

/**
 * Calculate 8-bit CRC of CBI
 *
 * @param h	Pointer to CBI header
 * @return	CRC value
 */
uint8_t cbi_crc8(const struct cbi_header *h);

/**
 * Store data in memory in CBI data format
 *
 * @param p	Pointer to the buffer where a new data item will be stored. It
 * 		should be pointing to the data section of CBI.
 * @param tag	Tag of the data item
 * @param buf	Pointer to the buffer containing the data being copied.
 * @param size	Size of the data in bytes. Must be 0 < size < 256.
 * @return	Address of the byte following the stored data in the
 * 		destination buffer
 */
uint8_t *cbi_set_data(uint8_t *p, enum cbi_data_tag tag, const void *buf,
		      int size);

/**
 * Store string data in memory in CBI data format.
 *
 * @param p	Pointer to the buffer where a new data item will be stored. It
 * 		should be pointing to the data section of CBI.
 * @param tag	Tag of the data item
 * @param str	Pointer to the string data being copied. If pointer is NULL,
 * 		this function will ignore adding the tag as well. Else, the
 * 		string data will be added to CBI using size of strlen + 1. This
 * 		string is assumed to be NUL-terminated and NUL gets stored in
 * 		CBI along with the string data.
 * @return	Address of the byte following the stored data in the destination
 * 		buffer.
 */
uint8_t *cbi_set_string(uint8_t *p, enum cbi_data_tag tag, const char *str);

/**
 * Find a data field in CBI
 *
 * @param cbi	Buffer containing CBI struct
 * @param tag	Tag of the data field to search
 * @return	Pointer to the data or NULL if not found.
 */
struct cbi_data *cbi_find_tag(const void *cbi, enum cbi_data_tag tag);

/**
 * Callback implemented by board to manipulate data
 *
 * Note that this is part of the APIs (cbi_get_*) which can be called in any
 * order any time. Your callback should return EC_SUCCESS only after it has all
 * the data needed for manipulation. Until then, it should return EC_ERROR_BUSY.
 * That'll provide a consistent view to the callers, which is critical for CBI
 * to be functional.
 *
 * @param tag	Tag of the data field to be manipulated
 * @param buf	Pointer to the buffer containing the data being manipulated.
 * @param size	size of the date in bytes
 * @return EC_SUCCESS to indicate the data is ready.
 *         EC_ERROR_BUSY to indicate supplemental data is not ready.
 */
int cbi_board_override(enum cbi_data_tag tag, uint8_t *buf, uint8_t *size);

/**
 * Set and update FW_CONFIG tag field
 *
 * This function is only included when CONFIG_AP_POWER_CONTROL is disabled. It
 * is intended to be used for projects which want CBI functions, but do not
 * have an AP and ectool host command access.
 *
 * @param fw_config	updated value for FW_CONFIG tag
 * @return EC_SUCCESS to indicate the field was written correctly.
 *         EC_ERROR_ACCESS_DENIED to indicate WP is active
 *         EC_ERROR_UNKNOWN to indicate that the write operation failed
 */
int cbi_set_fw_config(uint32_t fw_config);

/**
 * Set and update SSFC tag field
 *
 * This function is only included when CONFIG_AP_POWER_CONTROL is disabled. It
 * is intended to be used for projects which want CBI functions, but do not
 * have an AP and ectool host command access.
 *
 * @param ssfc	updated value for SSFC tag
 * @return EC_SUCCESS to indicate the field was written correctly.
 *         EC_ERROR_ACCESS_DENIED to indicate WP is active
 *         EC_ERROR_UNKNOWN to indicate that the write operation failed
 */
int cbi_set_ssfc(uint32_t ssfc);

/**
 * Initialize CBI cache
 */
int cbi_create(void);

/**
 * Override CBI cache status to EC_CBI_CACHE_INVALID
 */
void cbi_invalidate_cache(void);

/**
 * Return CBI cache status
 */
int cbi_get_cache_status(void);

/**
 * Latch the CBI EEPROM WP
 *
 * This function assumes that the EC has a pin to set the CBI EEPROM WP signal
 * (GPIO_EC_CBI_WP).  Note that once the WP is set, the EC must be reset via
 * EC_RST_ODL in order for the WP to become unset since the signal is latched.
 */
void cbi_latch_eeprom_wp(void);

#ifdef TEST_BUILD
/**
 * Write the locally cached CBI to EEPROM.
 *
 * @return EC_RES_SUCCESS on success or EC_RES_* otherwise.
 */
int cbi_write(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_CROS_BOARD_INFO_H */
