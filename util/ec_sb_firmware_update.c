/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "comm-host.h"
#include "compile_time_macros.h"
#include "ec_sb_firmware_update.h"
#include "ec_commands.h"
#include "lock/gec_lock.h"
#include "misc_util.h"
#include "powerd_lock.h"

/* Subcommands: [check|update] */
enum {
	OP_UNKNOWN = 0,
	OP_CHECK   = 1,
	OP_UPDATE  = 2,
};

struct delay_value {
	uint32_t steps;
	uint32_t value;
};

/* Default retry counter on errors */
#define SB_FW_UPDATE_DEFAULT_RETRY_CNT 3
/* Default delay value */
#define SB_FW_UPDATE_DEFAULT_DELAY  1000

#define DELAY_US_BEGIN  500000
#define DELAY_US_END   1000000
#define DELAY_US_BUSY  1000000
#define DELAY_US_WRITE_END  50000

static struct delay_value sb_delays[] = {
	{1,             100000},
	{2,            9000000},
	{4,             100000},
	{771,            30000},
	{2200,           10000},
	{0xFFFFFF,       50000},
};

enum fw_update_state {
	S0_READ_STATUS   = 0,
	S1_READ_INFO     = 1,
	S2_WRITE_PREPARE = 2,
	S3_READ_STATUS   = 3,
	S4_WRITE_UPDATE  = 4,
	S5_READ_STATUS   = 5,
	S6_WRITE_BLOCK   = 6,
	S7_READ_STATUS   = 7,
	S8_WRITE_END     = 8,
	S9_READ_STATUS   = 9,
	S10_TERMINAL     = 10
};

#define MAX_FW_IMAGE_NAME_SIZE 80

/* Firmware Update Control Flags */
enum {
	F_AC_PRESENT    = 0x1, /* AC Present */
	F_VERSION_CHECK = 0x2, /* do firmware version check */
	F_UPDATE        = 0x4, /* do firmware update */
	F_NEED_UPDATE   = 0x8,  /* need firmware update */
	F_POWERD_DISABLED = 0x10,  /* powerd is disabled */
	F_LFCC_ZERO =       0x20  /* last full charge is zero */
};

struct fw_update_ctrl {
	uint32_t flags; /* fw update control flags */
	int size;    /* size of battery firmware image */
	char *ptr;   /* current read pointer of the firmware image */
	int  offset; /* current block write offset */
	struct sb_fw_header *fw_img_hdr; /*pointer to firmware image header*/
	struct sb_fw_update_status status;
	struct sb_fw_update_info info;
	int err_retry_cnt;
	int fec_err_retry_cnt;
	int busy_retry_cnt;
	int step_size;
	int rv;
	char image_name[MAX_FW_IMAGE_NAME_SIZE];
	char msg[256];
};

/*
 * Global Firmware Update Control Data Structure
 */
static struct fw_update_ctrl fw_update;

static uint32_t get_delay_value(uint32_t offset, uint32_t step_size)
{
	int sz = ARRAY_SIZE(sb_delays);
	int i;
	for (i = 0; i < sz; i++) {
		if (offset <= sb_delays[i].steps * step_size)
			return sb_delays[i].value;
	}
	return sb_delays[sz-1].value;
}

static void print_battery_firmware_image_hdr(
	struct sb_fw_header *hdr)
{
	printf("Latest Battery Firmware:\n");
	printf("\t%c%c%c%c hdr_ver:%04x major_minor:%04x\n",
		hdr->signature[0],
		hdr->signature[1],
		hdr->signature[2],
		hdr->signature[3],
		hdr->hdr_version, hdr->pkg_version_major_minor);

	printf("\tmaker:0x%04x hwid:0x%04x fw_ver:0x%04x tbl_ver:0x%04x\n",
		hdr->vendor_id, hdr->battery_type, hdr->fw_version,
		hdr->data_table_version);

	printf("\tbinary offset:0x%08x size:0x%08x chk_sum:0x%02x\n",
		hdr->fw_binary_offset, hdr->fw_binary_size, hdr->checksum);
}

static void print_info(struct sb_fw_update_info *info)
{
	printf("\nCurrent Battery Firmware:\n");
	printf("\tmaker:0x%04x hwid:0x%04x fw_ver:0x%04x tbl_ver:0x%04x\n",
		info->maker_id,
		info->hardware_id,
		info->fw_version,
		info->data_version);
	return;
}

static void print_status(struct sb_fw_update_status *sts)
{
	printf("f_maker_id:%d f_hw_id:%d f_fw_ver:%d f_permnent:%d\n",
		sts->v_fail_maker_id,
		sts->v_fail_hw_id,
		sts->v_fail_fw_version,
		sts->v_fail_permanent);
	printf("permanent failure:%d abnormal:%d fw_update:%d\n",
		sts->permanent_failure,
		sts->abnormal_condition,
		sts->fw_update_supported);
	printf("fw_update_mode:%d fw_corrupted:%d cmd_reject:%d\n",
		sts->fw_update_mode,
		sts->fw_corrupted,
		sts->cmd_reject);
	printf("invliad data:%d fw_fatal_err:%d fec_err:%d busy:%d\n",
		sts->invalid_data,
		sts->fw_fatal_error,
		sts->fec_error,
		sts->busy);
	printf("\n");
	return;
}

/* @return 1 (True) if img signature is valid */
static int check_battery_firmware_image_signature(
	struct sb_fw_header *hdr)
{
	return (hdr->signature[0] == 'B') &&
		(hdr->signature[1] == 'T') &&
		(hdr->signature[2] == 'F') &&
		(hdr->signature[3] == 'W');
}

/* @return 1 (True) if img checksum is valid. */
static int check_battery_firmware_image_checksum(
	struct sb_fw_header *hdr)
{
	int i;
	uint8_t sum = 0;
	uint8_t *img = (uint8_t *)hdr;

	img += hdr->fw_binary_offset;
	for (i = 0; i < hdr->fw_binary_size; i++)
		sum += img[i];
	sum += hdr->checksum;
	return sum == 0;
}

/* @return 1 (True) if img versions are ok to update. */
static int check_battery_firmware_image_version(
	struct sb_fw_header *hdr,
	struct sb_fw_update_info *p)
{
	/*
	 * If the battery firmware has a newer fw version
	 * or a newer data table version, then it is ok to update.
	 */
	return (hdr->fw_version > p->fw_version)
		|| (hdr->data_table_version > p->data_version);
}


static int check_battery_firmware_ids(
	struct sb_fw_header *hdr,
	struct sb_fw_update_info *p)
{
	return ((hdr->vendor_id == p->maker_id) &&
		(hdr->battery_type == p->hardware_id));
}

/* check_if_need_update_fw
 * @return 1 (true) if need; 0 (false) if not.
 */
static int check_if_valid_fw(
		struct sb_fw_header *hdr,
		struct sb_fw_update_info *info)
{
	return check_battery_firmware_image_signature(hdr)
		&& check_battery_firmware_ids(hdr, info)
		&& check_battery_firmware_image_checksum(hdr);
}

/* check_if_need_update_fw
 * @return 1 (true) if need; 0 (false) if not.
 */
static int check_if_need_update_fw(
		struct sb_fw_header *hdr,
		struct sb_fw_update_info *info)
{
	return check_battery_firmware_image_version(hdr, info);
}

static void log_msg(struct fw_update_ctrl *fw_update,
			enum fw_update_state state, const char *msg)
{
	sprintf(fw_update->msg,
		"Battery Firmware Updater State:%d %s", state, msg);
}


static char *read_fw_image(struct fw_update_ctrl *fw_update)
{
	int size;
	char *buf;
	fw_update->size = 0;
	fw_update->ptr = NULL;
	fw_update->fw_img_hdr = (struct sb_fw_header *)NULL;

	/* Read the input file */
	buf = read_file(fw_update->image_name, &size);
	if (!buf)
		return NULL;

	fw_update->size = size;
	fw_update->ptr = buf;
	fw_update->fw_img_hdr = (struct sb_fw_header *)buf;
	print_battery_firmware_image_hdr(fw_update->fw_img_hdr);

	if (fw_update->fw_img_hdr->fw_binary_offset >= fw_update->size ||
		fw_update->size < 256) {
		printf("Load Firmware Image[%s] Error offset:%d size:%d\n",
			fw_update->image_name,
			fw_update->fw_img_hdr->fw_binary_offset,
			fw_update->size);
		free(buf);
		return NULL;
	}
	return buf;
}

static int get_status(struct sb_fw_update_status *status)
{
	int rv = EC_RES_SUCCESS;
	int cnt = 0;

	struct ec_params_sb_fw_update *param =
		(struct ec_params_sb_fw_update *)ec_outbuf;

	struct ec_response_sb_fw_update *resp =
		(struct ec_response_sb_fw_update *)ec_inbuf;

	param->hdr.subcmd = EC_SB_FW_UPDATE_STATUS;
	do {
		usleep(SB_FW_UPDATE_DEFAULT_DELAY);
		rv = ec_command(EC_CMD_SB_FW_UPDATE, 0,
			param, sizeof(struct ec_sb_fw_update_header),
			resp, SB_FW_UPDATE_CMD_STATUS_SIZE);
	} while ((rv < 0) && (cnt++ < SB_FW_UPDATE_DEFAULT_RETRY_CNT));

	if (rv < 0) {
		memset(status, 0, SB_FW_UPDATE_CMD_STATUS_SIZE);
		return -EC_RES_ERROR;
	}

	memcpy(status, resp->status.data, SB_FW_UPDATE_CMD_STATUS_SIZE);
	return EC_RES_SUCCESS;
}

static int get_info(struct sb_fw_update_info *info)
{
	int rv = EC_RES_SUCCESS;
	int cnt = 0;

	struct ec_params_sb_fw_update *param =
		(struct ec_params_sb_fw_update *)ec_outbuf;

	struct ec_response_sb_fw_update *resp =
		(struct ec_response_sb_fw_update *)ec_inbuf;

	param->hdr.subcmd = EC_SB_FW_UPDATE_INFO;
	do {
		usleep(SB_FW_UPDATE_DEFAULT_DELAY);
		rv = ec_command(EC_CMD_SB_FW_UPDATE, 0,
			param, sizeof(struct ec_sb_fw_update_header),
			resp, SB_FW_UPDATE_CMD_INFO_SIZE);
	} while ((rv < 0) && (cnt++ < SB_FW_UPDATE_DEFAULT_RETRY_CNT));

	if (rv < 0) {
		memset(info, 0, SB_FW_UPDATE_CMD_INFO_SIZE);
		return -EC_RES_ERROR;
	}

	memcpy(info, resp->info.data, SB_FW_UPDATE_CMD_INFO_SIZE);
	return EC_RES_SUCCESS;
}

static int send_subcmd(int subcmd)
{
	int rv = EC_RES_SUCCESS;

	struct ec_params_sb_fw_update *param =
		(struct ec_params_sb_fw_update *)ec_outbuf;

	param->hdr.subcmd = subcmd;
	rv = ec_command(EC_CMD_SB_FW_UPDATE, 0,
		param, sizeof(struct ec_sb_fw_update_header), NULL, 0);
	if (rv < 0) {
		printf("Firmware Update subcmd:%d Error\n", subcmd);
		return -EC_RES_ERROR;
	}
	return EC_RES_SUCCESS;
}

static int write_block(struct fw_update_ctrl *fw_update,
			int offset, int bsize)
{
	int rv;

	struct ec_params_sb_fw_update *param =
		(struct ec_params_sb_fw_update *)ec_outbuf;

	memcpy(param->write.data, fw_update->ptr+offset, bsize);

	param->hdr.subcmd = EC_SB_FW_UPDATE_WRITE;
	rv = ec_command(EC_CMD_SB_FW_UPDATE, 0,
		param, sizeof(struct ec_params_sb_fw_update), NULL, 0);
	if (rv < 0) {
		printf("Firmware Update Write Error ptr:%p offset@%x\n",
			fw_update->ptr, offset);
		return -EC_RES_ERROR;
	}
	return EC_RES_SUCCESS;
}

static void dump_data(uint8_t *data, int offset, int size)
{
	int i = 0;

	printf("Offset:0x%X\n", offset);
	for (i = 0; i < size; i++) {
		if ((i%16) == 0)
			printf("\n");
		printf("%02X ", data[i]);
	}
	printf("\n");
}

static enum fw_update_state s0_read_status(struct fw_update_ctrl *fw_update)
{
	if (fw_update->busy_retry_cnt == 0) {
		fw_update->rv = -1;
		log_msg(fw_update, S0_READ_STATUS, "Busy");
		return S10_TERMINAL;
	}

	fw_update->busy_retry_cnt--;

	fw_update->rv = get_status(&fw_update->status);
	if (fw_update->rv) {
		fw_update->rv = -1;
		log_msg(fw_update, S0_READ_STATUS, "Interface Error");
		return S10_TERMINAL;
	}

	if (!((fw_update->status.abnormal_condition == 0)
		&& (fw_update->status.fw_update_supported == 1))) {
		return S0_READ_STATUS;
	}

	if (fw_update->status.busy) {
		usleep(DELAY_US_BUSY);
		return S0_READ_STATUS;
	} else
		return S1_READ_INFO;
}

static enum fw_update_state s1_read_battery_info(
		struct fw_update_ctrl *fw_update)
{
	int rv;

	if (fw_update->err_retry_cnt == 0) {
		fw_update->rv = -1;
		log_msg(fw_update, S1_READ_INFO, "Retry Error");
		return S10_TERMINAL;
	}

	fw_update->err_retry_cnt--;

	rv = get_info(&fw_update->info);
	if (rv) {
		fw_update->rv = -1;
		log_msg(fw_update, S1_READ_INFO, "Interface Error");
		return S10_TERMINAL;
	}
	print_info(&fw_update->info);

	sprintf(fw_update->image_name,
			"/lib/firmware/battery/maker.%04x.hwid.%04x.bin",
			fw_update->info.maker_id,
			fw_update->info.hardware_id);

	if (NULL == read_fw_image(fw_update)) {
		fw_update->rv = 0;
		log_msg(fw_update, S1_READ_INFO, "Open Image File");
		return S10_TERMINAL;
	}

	rv = get_status(&fw_update->status);
	if (rv) {
		fw_update->rv = -1;
		log_msg(fw_update, S1_READ_INFO, "Interface Error");
		return S10_TERMINAL;
	}

	rv = check_if_valid_fw(fw_update->fw_img_hdr, &fw_update->info);
	if (rv == 0) {
		fw_update->rv = -EC_RES_INVALID_PARAM;
		log_msg(fw_update, S1_READ_INFO, "Invalid Firmware");
		return S10_TERMINAL;
	}

	rv = check_if_need_update_fw(fw_update->fw_img_hdr, &fw_update->info);
	if (rv == 0 && (fw_update->flags & F_VERSION_CHECK)) {
		fw_update->rv = 0;
		log_msg(fw_update, S1_READ_INFO, "Latest Firmware");
		return S10_TERMINAL;
	}

	fw_update->flags |= F_NEED_UPDATE;

	if (!(fw_update->flags & F_UPDATE)) {
		fw_update->rv = 0;
		return S10_TERMINAL;
	}

	if (!(fw_update->flags & F_AC_PRESENT)) {
		fw_update->rv = 0;
		log_msg(fw_update, S1_READ_INFO,
			"Require AC Adapter Counnected.");
		return S10_TERMINAL;
	}
	return S2_WRITE_PREPARE;
}

static enum fw_update_state s2_write_prepare(struct fw_update_ctrl *fw_update)
{
	int rv;

	rv = disable_power_management();
	if (0 == rv)
		fw_update->flags |= F_POWERD_DISABLED;

	rv = send_subcmd(EC_SB_FW_UPDATE_PREPARE);
	if (rv) {
		fw_update->rv = -1;
		log_msg(fw_update, S2_WRITE_PREPARE, "Interface Error");
		return S10_TERMINAL;
	}
	return S3_READ_STATUS;
}

static enum fw_update_state s3_read_status(struct fw_update_ctrl *fw_update)
{
	int rv;

	rv = get_status(&fw_update->status);
	if (rv) {
		fw_update->rv = -1;
		log_msg(fw_update, S3_READ_STATUS, "Interface Error");
		return S10_TERMINAL;
	}
	return S4_WRITE_UPDATE;

}

static enum fw_update_state s4_write_update(struct fw_update_ctrl *fw_update)
{
	int rv;

	rv = send_subcmd(EC_SB_FW_UPDATE_BEGIN);
	if (rv) {
		fw_update->rv = -1;
		log_msg(fw_update, S4_WRITE_UPDATE, "Interface Error");
		return S10_TERMINAL;
	}
	usleep(DELAY_US_BEGIN);
	return S5_READ_STATUS;
}

static enum fw_update_state s5_read_status(struct fw_update_ctrl *fw_update)
{
	int rv = get_status(&fw_update->status);

	if (rv) {
		fw_update->rv = -1;
		log_msg(fw_update, S5_READ_STATUS, "Interface Error");
		return S10_TERMINAL;
	}
	if (fw_update->status.fw_update_mode == 0)
		return S2_WRITE_PREPARE;

	/* Init Write Block Loop Controls */
	fw_update->ptr += fw_update->fw_img_hdr->fw_binary_offset;
	fw_update->size -= fw_update->fw_img_hdr->fw_binary_offset;
	fw_update->offset = 0;

	return S6_WRITE_BLOCK;
}

static enum fw_update_state s6_write_block(struct fw_update_ctrl *fw_update)
{
	int rv;
	int bsize;
	int offset = fw_update->offset;

	if (offset >= fw_update->size)
		return S8_WRITE_END;

	bsize = fw_update->step_size;

	if ((offset & 0xFFFF) == 0x0)
		printf("\n%X\n", offset);

	if (fw_update->fec_err_retry_cnt == 0) {
		fw_update->rv = -1;
		log_msg(fw_update, S6_WRITE_BLOCK, "FEC Retry Error");
		return S10_TERMINAL;
	}
	fw_update->fec_err_retry_cnt--;

	rv = write_block(fw_update, offset, bsize);
	if (rv) {
		fw_update->rv = -1;
		log_msg(fw_update, S6_WRITE_BLOCK, "Interface Error");
		return S10_TERMINAL;
	}

	/*
	 * Add more detays after the last a few block (3) writes.
	 * 3 is chosen based on current test results.
	 */
	if ((offset + 3*fw_update->step_size) >= fw_update->size)
		usleep(DELAY_US_WRITE_END);

	usleep(get_delay_value(offset, fw_update->step_size));

	return S7_READ_STATUS;
}

static enum fw_update_state s7_read_status(struct fw_update_ctrl *fw_update)
{
	int rv;
	int offset = fw_update->offset;
	int bsize;
	int cnt = 0;

	bsize = fw_update->step_size;
	do {
		usleep(SB_FW_UPDATE_DEFAULT_DELAY);
		rv = get_status(&fw_update->status);
		if (rv) {
			dump_data(fw_update->ptr+offset, offset, bsize);
			print_status(&fw_update->status);
			fw_update->rv = -1;
			log_msg(fw_update, S7_READ_STATUS, "Interface Error");
			return S10_TERMINAL;
		}
	} while (fw_update->status.busy &&
			(cnt++ < SB_FW_UPDATE_DEFAULT_RETRY_CNT));

	if (fw_update->status.fec_error) {
		dump_data(fw_update->ptr+offset, offset, bsize);
		print_status(&fw_update->status);
		fw_update->rv = 0;
		return S6_WRITE_BLOCK;
	}
	if (fw_update->status.permanent_failure ||
		fw_update->status.v_fail_permanent) {
		dump_data(fw_update->ptr+offset, offset, bsize);
		print_status(&fw_update->status);
		fw_update->rv = -1;
		log_msg(fw_update, S7_READ_STATUS, "Battery Permanent Error");
		return S8_WRITE_END;
	}
	if (fw_update->status.v_fail_maker_id ||
		fw_update->status.v_fail_hw_id ||
		fw_update->status.v_fail_fw_version ||
		fw_update->status.fw_corrupted ||
		fw_update->status.cmd_reject ||
		fw_update->status.invalid_data ||
		fw_update->status.fw_fatal_error) {

		dump_data(fw_update->ptr+offset, offset, bsize);
		print_status(&fw_update->status);
		fw_update->rv = 0;
		return S1_READ_INFO;
	}

	fw_update->fec_err_retry_cnt = SB_FW_UPDATE_FEC_ERROR_RETRY_CNT;
	fw_update->offset += fw_update->step_size;
	return S6_WRITE_BLOCK;
}


static enum fw_update_state s8_write_end(struct fw_update_ctrl *fw_update)
{
	int rv;

	rv = send_subcmd(EC_SB_FW_UPDATE_END);
	if (rv && (0 == fw_update->rv)) {
		fw_update->rv = -1;
		log_msg(fw_update, S8_WRITE_END, "Interface Error");
	}

	if (fw_update->rv)
		return S10_TERMINAL;

	usleep(DELAY_US_END);
	fw_update->busy_retry_cnt = SB_FW_UPDATE_BUSY_ERROR_RETRY_CNT;
	return S9_READ_STATUS;
}

static enum fw_update_state s9_read_status(struct fw_update_ctrl *fw_update)
{
	int rv;

	if (fw_update->busy_retry_cnt == 0) {
		fw_update->rv = -1;
		log_msg(fw_update, S9_READ_STATUS, "Busy");
		return S10_TERMINAL;
	}

	rv = get_status(&fw_update->status);
	if (rv) {
		fw_update->rv = -1;
		log_msg(fw_update, S9_READ_STATUS, "Interface Error");
		return S10_TERMINAL;
	}
	if ((fw_update->status.fw_update_mode == 1)
		|| (fw_update->status.busy == 1)) {
		usleep(SB_FW_UPDATE_DEFAULT_DELAY);
		fw_update->busy_retry_cnt--;
		return S9_READ_STATUS;
	}
	log_msg(fw_update, S9_READ_STATUS, "Complete");
	fw_update->flags &= ~F_NEED_UPDATE;
	return S10_TERMINAL;
}


typedef enum fw_update_state (*fw_state_func)(struct fw_update_ctrl *fw_update);

fw_state_func state_table[] = {
	s0_read_status,
	s1_read_battery_info,
	s2_write_prepare,
	s3_read_status,
	s4_write_update,
	s5_read_status,
	s6_write_block,
	s7_read_status,
	s8_write_end,
	s9_read_status
};


/**
 * Update Smart Battery Firmware
 *
 * @param fw_update struct fw_update_ctrl
 *
 * @return 0 if success, negative if error.
 */
static int ec_sb_firmware_update(struct fw_update_ctrl *fw_update)
{
	enum fw_update_state state;

	fw_update->err_retry_cnt = SB_FW_UPDATE_ERROR_RETRY_CNT;
	fw_update->fec_err_retry_cnt = SB_FW_UPDATE_FEC_ERROR_RETRY_CNT;
	fw_update->busy_retry_cnt = SB_FW_UPDATE_BUSY_ERROR_RETRY_CNT;
	fw_update->step_size = SB_FW_UPDATE_CMD_WRITE_BLOCK_SIZE;

	state = S0_READ_STATUS;
	while (state != S10_TERMINAL)
		state = state_table[state](fw_update);

	if (fw_update->fw_img_hdr)
		free(fw_update->fw_img_hdr);

	return fw_update->rv;
}

#define GEC_LOCK_TIMEOUT_SECS   30  /* 30 secs */
void usage(char *argv[])
{
	printf("Usage: %s [check|update]\n"
		"	check: check if AC Adaptor is connected.\n"
		"	update: trigger battery firmware update.\n",
		argv[0]);
}

int main(int argc, char *argv[])
{
	int rv = 0, interfaces = COMM_ALL;
	int op = OP_UNKNOWN;
	uint8_t val = 0;

	if (argc != 2) {
		usage(argv);
		return -1;
	}

	if (!strcmp(argv[1], "check"))
		op = OP_CHECK;
	else if (!strcmp(argv[1], "update"))
		op = OP_UPDATE;
	else {
		op = OP_UNKNOWN;
		usage(argv);
		return -1;
	}

	if (acquire_gec_lock(GEC_LOCK_TIMEOUT_SECS) < 0) {
		printf("Could not acquire GEC lock.\n");
		return -1;
	}

	if (comm_init(interfaces, NULL)) {
		printf("Couldn't find EC\n");
		goto out;
	}

	fw_update.flags = 0;
	rv = ec_readmem(EC_MEMMAP_BATT_FLAG, sizeof(val), &val);
	if (rv <= 0) {
		printf("EC Memmap read error:%d\n", rv);
		goto out;
	}

	if (val & EC_BATT_FLAG_AC_PRESENT) {
		fw_update.flags |= F_AC_PRESENT;
		printf("AC_PRESENT\n");
	}
	rv = ec_readmem(EC_MEMMAP_BATT_LFCC, sizeof(val), &val);
	if (rv <= 0) {
		printf("EC Memmap read error:%d\n", rv);
		goto out;
	}
	if (val == 0)
		fw_update.flags |= F_LFCC_ZERO;

	if (op == OP_UPDATE)
		fw_update.flags |= F_UPDATE;

	fw_update.flags |= F_VERSION_CHECK;

	rv = ec_sb_firmware_update(&fw_update);
	printf("Battery Firmware Update:0x%02x %s\n%s\n",
			fw_update.flags,
			((rv) ? "FAIL " : " "),
			fw_update.msg);

	/* Update battery firmware update interface to be protected */
	if (!(fw_update.flags & F_NEED_UPDATE))
		rv |= send_subcmd(EC_SB_FW_UPDATE_PROTECT);

	if (fw_update.flags & F_POWERD_DISABLED)
		rv |= restore_power_management();
out:
	release_gec_lock();
	if (rv)
		return -1;
	else
		return fw_update.flags & (F_LFCC_ZERO | F_NEED_UPDATE);
}
