/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lock/gec_lock.h"
#include "comm-host.h"
#include "misc_util.h"
#include "ec_sb_firmware_update.h"
#include "ec_commands.h"
#include <unistd.h>

#define DPRINTF(fmt, ...) \
	do {\
		if (debug)\
			printf("SBFW: " fmt, ## __VA_ARGS__);\
	} while (0)

/* Debug EC Smart Battery Firmwarwe Update */
static int debug;

/*
 * Simplo Battery: Required 10 seconds delay for 1st 10 block write
 * unit in seconds
 */
static int delay_x_us = 9000000;

/*
 *  Simplo Battery: Additional delays are required after each 32-byte write
 *  unit in useconds
 */
static int delay_y_us = 50000;

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

struct fw_update_ctrl {
	int size;    /* size of battery firmware image */
	char *ptr;   /* current pointer to the firmware image */
	int  offset; /* current block write offset */
	struct sb_fw_header *fw_img_hdr; /*pointer to firmware image header*/
	struct sb_fw_update_status status;
	struct sb_fw_update_info info;
	int err_retry_cnt;
	int fec_err_retry_cnt;
	int busy_retry_cnt;
	int step_size;
	int rv;
	char msg[256];
};

/*
 * Global Firmware Update Control Data Structure
 */
static struct fw_update_ctrl fw_update;

static void print_battery_firmware_image_hdr(
	struct sb_fw_header *hdr)
{
	printf("%c%c%c%c hdr_ver:%04X major_minor:%04X\n",
		hdr->signature[0],
		hdr->signature[1],
		hdr->signature[2],
		hdr->signature[3],
		hdr->hdr_version, hdr->pkg_version_major_minor);

	printf("vendor_id:%04X battery_type:%04X fw_ver:%04X tbl_ver:%04X\n",
		hdr->vendor_id, hdr->battery_type, hdr->fw_version,
		hdr->data_table_version);

	printf("bin off:%08X size:%08X chk_sum:%02X\n",
		hdr->fw_binary_offset, hdr->fw_binary_size, hdr->checksum);
}

static void print_info(struct sb_fw_update_info *info)
{
	printf("maker_id:0x%X hw_id:0x%X fw_ver:0x%X d_ver:0x%X\n",
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
	return (((hdr->fw_version == 0xFFFF)
			|| (hdr->fw_version > p->fw_version)) &&
		((hdr->data_table_version == 0xFFFF)
			|| (hdr->data_table_version > p->data_version)));
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
static int check_if_need_update_fw(
		struct sb_fw_header *hdr,
		struct sb_fw_update_info *info)
{
	return check_battery_firmware_image_signature(hdr)

	&& check_battery_firmware_ids(hdr, info)

	&& check_battery_firmware_image_version(hdr, info)

	&& check_battery_firmware_image_checksum(hdr);
}

static int get_status(struct sb_fw_update_status *status)
{
	int rv = EC_RES_SUCCESS;
	int i = 0;
	struct ec_params_sb_fw_update *param =
		(struct ec_params_sb_fw_update *)ec_outbuf;

	struct ec_response_sb_fw_update *resp =
		(struct ec_response_sb_fw_update *)ec_inbuf;

	param->hdr.subcmd = EC_SB_FW_UPDATE_STATUS;
	do {
		rv = ec_command(EC_CMD_SB_FW_UPDATE, 0,
			param, sizeof(struct ec_sb_fw_update_header),
			resp, SB_FW_UPDATE_CMD_STATUS_SIZE);
	} while ((rv < 0) && (i++ < 3));

	if (rv < 0) {
		fprintf(stderr,
			"Firmware Update Get Status Error\n");
		return -EC_RES_ERROR;
	}
	memcpy(status, resp->status.data, SB_FW_UPDATE_CMD_STATUS_SIZE);
	return EC_RES_SUCCESS;
}

static int get_info(struct sb_fw_update_info *info)
{
	int rv = EC_RES_SUCCESS;

	struct ec_params_sb_fw_update *param =
		(struct ec_params_sb_fw_update *)ec_outbuf;

	struct ec_response_sb_fw_update *resp =
		(struct ec_response_sb_fw_update *)ec_inbuf;

	param->hdr.subcmd = EC_SB_FW_UPDATE_INFO;
	rv = ec_command(EC_CMD_SB_FW_UPDATE, 0,
		param, sizeof(struct ec_sb_fw_update_header),
		resp, SB_FW_UPDATE_CMD_INFO_SIZE);
	if (rv < 0) {
		fprintf(stderr,
			"Firmware Update Get Info Error\n");
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
		fprintf(stderr,
			"Firmware Update subcmd:%d Error\n", subcmd);
		return -EC_RES_ERROR;
	}
	return EC_RES_SUCCESS;
}

static int write_block(const uint8_t *ptr, int bsize)
{
	int rv;
	struct ec_params_sb_fw_update *param =
		(struct ec_params_sb_fw_update *)ec_outbuf;

	memcpy(param->write.data, ptr, bsize);

	param->hdr.subcmd = EC_SB_FW_UPDATE_WRITE;
	rv = ec_command(EC_CMD_SB_FW_UPDATE, 0,
		param, sizeof(struct ec_params_sb_fw_update), NULL, 0);
	if (rv < 0) {
		fprintf(stderr,
		"Firmware Update Write Error offset@%p\n", ptr);
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
		sprintf(fw_update->msg,
			"Firmware Udpate interface busy retry error!\n");
		return S10_TERMINAL;
	}

	fw_update->busy_retry_cnt--;

	fw_update->rv = get_status(&fw_update->status);
	if (fw_update->rv) {
		fw_update->rv = -1;
		sprintf(fw_update->msg,
			"Firmware Udpate interface protected!\n");
		return S10_TERMINAL;
	}

	if (debug)
		print_status(&fw_update->status);

	if (!((fw_update->status.abnormal_condition == 0)
		&& (fw_update->status.fw_update_supported == 1))) {
		sprintf(fw_update->msg,
			"Firmware Udpate is not supported!\n");
		return S10_TERMINAL;
	}
	if (fw_update->status.busy)
		return S0_READ_STATUS;
	else
		return S1_READ_INFO;
}

static enum fw_update_state s1_read_battery_info(
		struct fw_update_ctrl *fw_update)
{
	int rv;
	if (fw_update->err_retry_cnt == 0) {
		fw_update->rv = -1;
		sprintf(fw_update->msg,
			"Firmware Udpate interface busy retry error!\n");
		return S10_TERMINAL;
	}

	fw_update->err_retry_cnt--;

	rv = get_info(&fw_update->info);
	if (rv) {
		fw_update->rv = -1;
		return S10_TERMINAL;
	}

	if (debug)
		print_info(&fw_update->info);

	rv = get_status(&fw_update->status);
	if (rv) {
		fw_update->rv = -1;
		return S10_TERMINAL;
	}

	rv = check_if_need_update_fw(fw_update->fw_img_hdr, &fw_update->info);
	if (rv == 0) {
		printf("ERROR:Battery firmware is not valid!\n");
		print_info(&fw_update->info);
		print_battery_firmware_image_hdr(fw_update->fw_img_hdr);
		fw_update->rv = EC_RES_INVALID_PARAM;
		return S10_TERMINAL;
	}
	return S2_WRITE_PREPARE;
}

static enum fw_update_state s2_write_prepare(struct fw_update_ctrl *fw_update)
{
	int rv;
	DPRINTF("cmd.0x35 write word 0x1000\n");
	rv = send_subcmd(EC_SB_FW_UPDATE_PREPARE);
	if (rv) {
		fw_update->rv = -1;
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
		return S10_TERMINAL;
	}
	return S4_WRITE_UPDATE;

}

static enum fw_update_state s4_write_update(struct fw_update_ctrl *fw_update)
{
	int rv;
	DPRINTF("cmd.0x35 write word 0xF000\n");
	rv = send_subcmd(EC_SB_FW_UPDATE_BEGIN);
	if (rv) {
		fw_update->rv = -1;
		return S10_TERMINAL;
	}
	usleep(500000);
	return S5_READ_STATUS;
}

static enum fw_update_state s5_read_status(struct fw_update_ctrl *fw_update)
{
	int rv = get_status(&fw_update->status);
	if (rv) {
		fw_update->rv = -1;
		return S10_TERMINAL;
	}
	if (fw_update->status.fw_update_mode == 0)
		return S2_WRITE_PREPARE;

	/* Init Write Block Loop Controls */
	fw_update->ptr += fw_update->fw_img_hdr->fw_binary_offset;
	fw_update->size -= fw_update->fw_img_hdr->fw_binary_offset;
	fw_update->offset = 0;

	DPRINTF("Write size 0x%X total_size:0x%X\n",
		fw_update->step_size, fw_update->size);

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

	if ((offset & 0x1FFF) == 0x000)
		printf("\n%X\n", offset);
	else
		printf(".");

	if (fw_update->fec_err_retry_cnt == 0) {
		fw_update->rv = -1;
		return S10_TERMINAL;
	}
	fw_update->fec_err_retry_cnt--;

	rv = write_block(fw_update->ptr+offset, bsize);
	if (rv) {
		fw_update->rv = -1;
		return S10_TERMINAL;
	}

	if (delay_x_us || delay_y_us) {
		if (offset <= fw_update->step_size * 10)
			usleep(delay_x_us);
		else
			usleep(delay_y_us);
	}
	return S7_READ_STATUS;
}

static enum fw_update_state s7_read_status(struct fw_update_ctrl *fw_update)
{
	int rv;
	int offset = fw_update->offset;
	int bsize;

	bsize = fw_update->step_size;
	do {
		rv = get_status(&fw_update->status);
		if (rv) {
			dump_data(fw_update->ptr+offset, offset, bsize);
			print_status(&fw_update->status);
			fw_update->rv = -1;
			return S10_TERMINAL;
		}
	} while (fw_update->status.busy);

	if (fw_update->status.fec_error) {
		dump_data(fw_update->ptr+offset, offset, bsize);
		print_status(&fw_update->status);
		fw_update->rv = -1;
		return S6_WRITE_BLOCK;
	}
	if (fw_update->status.fw_fatal_error) {
		dump_data(fw_update->ptr+offset, offset, bsize);
		print_status(&fw_update->status);
		fw_update->rv = -1;
		return S2_WRITE_PREPARE;
	}
	if (fw_update->status.permanent_failure ||
		fw_update->status.v_fail_permanent) {
		dump_data(fw_update->ptr+offset, offset, bsize);
		print_status(&fw_update->status);
		fw_update->rv = -1;
		return S8_WRITE_END;
	}
	if (fw_update->status.v_fail_maker_id ||
		fw_update->status.v_fail_hw_id    ||
		fw_update->status.v_fail_fw_version ||
		fw_update->status.fw_corrupted   ||
		fw_update->status.cmd_reject ||
		fw_update->status.invalid_data) {

		dump_data(fw_update->ptr+offset, offset, bsize);
		print_status(&fw_update->status);
		fw_update->rv = -1;
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
	if (rv) {
		fw_update->rv = -1;
		sprintf(fw_update->msg, "SB FW Update End Error\n");
		return S10_TERMINAL;
	}

	/* Note: Sleep is required! */
	usleep(1000000);
	return S9_READ_STATUS;
}

static enum fw_update_state s9_read_status(struct fw_update_ctrl *fw_update)
{
	int rv;
	/* Poll for completion */
	rv = get_status(&fw_update->status);
	if (rv) {
		fw_update->rv = -1;
		sprintf(fw_update->msg,
			"SB FW Update End get status Error: rv:%d\n", rv);
		return S10_TERMINAL;
	}
	if ((fw_update->status.fw_update_mode == 1)
		|| (fw_update->status.busy == 1)) {
		return S9_READ_STATUS;
	}
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

int ec_sb_firmware_update(const char *fw_image_name)
{
	enum fw_update_state state;
	int size;
	char *buf;

	fw_update.err_retry_cnt = SB_FW_UPDATE_ERROR_RETRY_CNT;
	fw_update.fec_err_retry_cnt = SB_FW_UPDATE_FEC_ERROR_RETRY_CNT;
	fw_update.busy_retry_cnt = SB_FW_UPDATE_BUSY_ERROR_RETRY_CNT;
	fw_update.step_size = SB_FW_UPDATE_CMD_WRITE_BLOCK_SIZE;

	/* Read the input file */
	DPRINTF("\n\n==> Read File:%s\n", fw_image_name);
	buf = read_file(fw_image_name, &size);
	if (!buf) {
		fprintf(stderr,
			"Firmware Update: Load Firmware Image[%s] Error\n",
			fw_image_name);
		return -1;
	}
	fw_update.size = size;
	fw_update.ptr = buf;
	fw_update.fw_img_hdr = (struct sb_fw_header *)buf;
	if (debug)
		print_battery_firmware_image_hdr(fw_update.fw_img_hdr);

	if (fw_update.fw_img_hdr->fw_binary_offset >= fw_update.size ||
		fw_update.size < 256) {
		fprintf(stderr,
			"Load Firmware Image[%s] Error offset:%d size:%d\n",
			fw_image_name,
			fw_update.fw_img_hdr->fw_binary_offset,
			fw_update.size);
		return -1;
	}

	state = S0_READ_STATUS;
	while (state != S10_TERMINAL)
		state = state_table[state](&fw_update);

	free(buf);
	if (fw_update.rv)
		printf("\n\n==> Firmware:%s Update Failed:%d [%s]\n",
			fw_image_name,
			fw_update.rv,
			fw_update.msg);
	else
		printf("\n\n==> Firmware:%s Update Complete.\n",
			fw_image_name);

	return fw_update.rv;
}

#define GEC_LOCK_TIMEOUT_SECS   30  /* 30 secs */

int main(int argc, char *argv[])
{
	int rv = 0, interfaces = COMM_LPC;
	const char *test = "normal";
	if (argc < 2) {
		fprintf(stderr,
			"Usage: %s <fw_filename> <test> "
			"[initial_tx_delay] [tx_delay] [debug]\n", argv[0]);
		return -1;
	}

	if (argc >= 3)
		test = argv[2];

	if (argc >= 4)
		delay_x_us = atoi(argv[3]);

	if (argc >= 5)
		delay_y_us = atoi(argv[4]);

	if (argc >= 6)
		debug = atoi(argv[5]);

	if (acquire_gec_lock(GEC_LOCK_TIMEOUT_SECS) < 0) {
		fprintf(stderr, "Could not acquire GEC lock.\n");
		exit(1);
	}

	if (comm_init(interfaces, NULL)) {
		fprintf(stderr, "Couldn't find EC\n");
		goto out;
	}

	DPRINTF("fw_filename:%s\n", argv[1]);
	rv = ec_sb_firmware_update(argv[1]);

	/* set to protect mode if not running a fw update test */
	if (strcmp(test, "test"))
		rv |= send_subcmd(EC_SB_FW_UPDATE_PROTECT);
out:
	release_gec_lock();
	return rv;
}
